#include "adapters/opc_ua_server.hpp"
#include "adapters/ua_pki.hpp"

#include "domain/tag_value_util.hpp"

#include <open62541/plugin/accesscontrol.h>
#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/pki_default.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#ifdef UA_ENABLE_ENCRYPTION
#include <open62541/config.h>
#endif

#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <variant>

namespace opc::adapters {

struct OpcUaNodeContext {
    OpcUaServer* self{nullptr};
    domain::TagId tag_id{0};
    project::TagType type{project::TagType::UInt16};
    bool writable{false};
};

namespace {

void log_msg(ports::ILog* log, ports::LogLevel level, std::string_view msg) {
    if (log != nullptr) {
        log->log(level, "adapters.opcua", msg);
    }
}

[[nodiscard]] domain::TimestampMs wall_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void keep_security_endpoints(UA_ServerConfig* config,
                             UA_MessageSecurityMode mode,
                             std::string_view policy_uri) {
    size_t write = 0;
    for (size_t i = 0; i < config->endpointsSize; ++i) {
        UA_EndpointDescription& ep = config->endpoints[i];
        UA_String want = UA_STRING_ALLOC(std::string(policy_uri).c_str());
        const bool keep =
            ep.securityMode == mode && UA_String_equal(&ep.securityPolicyUri, &want);
        UA_String_clear(&want);
        if (keep) {
            if (write != i) {
                config->endpoints[write] = ep;
                UA_EndpointDescription_init(&ep);
            }
            ++write;
        } else {
            UA_EndpointDescription_clear(&ep);
        }
    }
    config->endpointsSize = write;
}

[[nodiscard]] domain::Result<void> apply_identity_access_control(
    UA_ServerConfig* config, const project::OpcUaSettings& opcua, ports::ILog* log) {
    const bool customize = !opcua.users.empty() || !opcua.allow_anonymous;
    if (!customize) {
        return {};
    }

    if (!opcua.users.empty() && opcua.security_mode == project::SecurityMode::None &&
        !opcua.allow_none_password) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::InvalidArgument,
            "username/password with SecurityMode None requires opcua.allowNonePassword "
            "or --ua-allow-none-password (credentials would be plaintext)",
            "adapters.opcua",
            false});
    }

    std::vector<UA_UsernamePasswordLogin> logins;
    logins.reserve(opcua.users.size());
    for (const auto& user : opcua.users) {
        UA_UsernamePasswordLogin entry{};
        entry.username = UA_STRING_ALLOC(user.username.c_str());
        entry.password = UA_STRING_ALLOC(user.password.c_str());
        logins.push_back(entry);
    }

    const UA_StatusCode status = UA_AccessControl_default(
        config, opcua.allow_anonymous ? UA_TRUE : UA_FALSE, nullptr, logins.size(),
        logins.empty() ? nullptr : logins.data());

    for (auto& entry : logins) {
        UA_String_clear(&entry.username);
        UA_String_clear(&entry.password);
    }

    if (status != UA_STATUSCODE_GOOD) {
        return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                             "UA_AccessControl_default failed",
                                             "adapters.opcua",
                                             false});
    }

    if (!opcua.users.empty() && opcua.security_mode == project::SecurityMode::None &&
        opcua.allow_none_password) {
        config->allowNonePolicyPassword = true;
        log_msg(log, ports::LogLevel::Warn,
                "username/password allowed over SecurityMode None (allowNonePassword)");
    }

    log_msg(log, ports::LogLevel::Info,
            opcua.allow_anonymous
                ? "AccessControl: username tokens configured (anonymous allowed)"
                : "AccessControl: username tokens configured (anonymous denied)");
    return {};
}

struct SessionHooks {
    opc::adapters::OpcUaServer* self{nullptr};
    UA_StatusCode (*orig_activate)(UA_Server*,
                                   UA_AccessControl*,
                                   const UA_EndpointDescription*,
                                   const UA_ByteString*,
                                   const UA_NodeId*,
                                   const UA_ExtensionObject*,
                                   void**){nullptr};
    void (*orig_close)(UA_Server*, UA_AccessControl*, const UA_NodeId*, void*){nullptr};
};

std::mutex g_session_hooks_mu;
std::unordered_map<UA_Server*, SessionHooks> g_session_hooks;

[[nodiscard]] std::uint32_t session_key(const UA_NodeId* id) {
    if (id == nullptr) {
        return 0;
    }
    if (id->identifierType == UA_NODEIDTYPE_NUMERIC) {
        return id->identifier.numeric;
    }
    return static_cast<std::uint32_t>(id->identifierType) << 24;
}

UA_StatusCode activate_session_hook(UA_Server* server,
                                    UA_AccessControl* ac,
                                    const UA_EndpointDescription* endpoint_description,
                                    const UA_ByteString* remote_certificate,
                                    const UA_NodeId* session_id,
                                    const UA_ExtensionObject* user_identity_token,
                                    void** session_context) {
    SessionHooks hooks;
    {
        std::lock_guard lock(g_session_hooks_mu);
        const auto it = g_session_hooks.find(server);
        if (it != g_session_hooks.end()) {
            hooks = it->second;
        }
    }
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    if (hooks.orig_activate != nullptr) {
        status = hooks.orig_activate(server, ac, endpoint_description, remote_certificate, session_id,
                                     user_identity_token, session_context);
    }
    if (status == UA_STATUSCODE_GOOD && hooks.self != nullptr) {
        hooks.self->note_session_activate(session_key(session_id));
    }
    return status;
}

void close_session_hook(UA_Server* server,
                        UA_AccessControl* ac,
                        const UA_NodeId* session_id,
                        void* session_context) {
    SessionHooks hooks;
    {
        std::lock_guard lock(g_session_hooks_mu);
        const auto it = g_session_hooks.find(server);
        if (it != g_session_hooks.end()) {
            hooks = it->second;
        }
    }
    if (hooks.orig_close != nullptr) {
        hooks.orig_close(server, ac, session_id, session_context);
    }
    if (hooks.self != nullptr) {
        hooks.self->note_session_close(session_key(session_id));
    }
}

void install_session_hooks(UA_Server* server, opc::adapters::OpcUaServer* self) {
    UA_ServerConfig* config = UA_Server_getConfig(server);
    if (config == nullptr) {
        return;
    }
    std::lock_guard lock(g_session_hooks_mu);
    g_session_hooks[server] = SessionHooks{
        .self = self,
        .orig_activate = config->accessControl.activateSession,
        .orig_close = config->accessControl.closeSession,
    };
    config->accessControl.activateSession = &activate_session_hook;
    config->accessControl.closeSession = &close_session_hook;
}

void uninstall_session_hooks(UA_Server* server) {
    UA_ServerConfig* config = server != nullptr ? UA_Server_getConfig(server) : nullptr;
    std::lock_guard lock(g_session_hooks_mu);
    const auto it = g_session_hooks.find(server);
    if (it == g_session_hooks.end()) {
        return;
    }
    // Restore AccessControl callbacks before erase so a late ActivateSession cannot
    // see missing hooks and skip username checks (fail-open).
    if (config != nullptr) {
        if (it->second.orig_activate != nullptr) {
            config->accessControl.activateSession = it->second.orig_activate;
        }
        if (it->second.orig_close != nullptr) {
            config->accessControl.closeSession = it->second.orig_close;
        }
    }
    g_session_hooks.erase(it);
}

}  // namespace

void OpcUaServer::note_session_activate(std::uint32_t session_id) {
    std::size_t count = 0;
    {
        std::lock_guard lock(diagnostics_mutex_);
        active_sessions_.insert(session_id);
        count = active_sessions_.size();
    }
    if (metrics_ != nullptr) {
        metrics_->gauge_set("ua_sessions", static_cast<double>(count));
    }
}

void OpcUaServer::note_session_close(std::uint32_t session_id) {
    std::size_t count = 0;
    {
        std::lock_guard lock(diagnostics_mutex_);
        active_sessions_.erase(session_id);
        count = active_sessions_.size();
    }
    if (metrics_ != nullptr) {
        metrics_->gauge_set("ua_sessions", static_cast<double>(count));
    }
}

namespace {

[[nodiscard]] std::uint16_t parse_endpoint_port(std::string_view url, std::uint16_t fallback) {
    const auto colon = url.rfind(':');
    if (colon == std::string_view::npos) {
        return fallback;
    }
    const auto port_str = url.substr(colon + 1);
    unsigned port = 0;
    const auto* begin = port_str.data();
    const auto* end = begin + port_str.size();
    const auto [ptr, ec] = std::from_chars(begin, end, port);
    if (ec != std::errc{} || ptr != end || port == 0 || port > 65535) {
        return fallback;
    }
    return static_cast<std::uint16_t>(port);
}

/// Host for DiscoveryUrl / EndpointDescription (must match what clients dial).
[[nodiscard]] std::string parse_endpoint_host(std::string_view url) {
    constexpr std::string_view kPrefix = "opc.tcp://";
    std::string_view rest = url;
    if (rest.starts_with(kPrefix)) {
        rest.remove_prefix(kPrefix.size());
    }
    const auto slash = rest.find('/');
    if (slash != std::string_view::npos) {
        rest = rest.substr(0, slash);
    }
    const auto colon = rest.rfind(':');
    if (colon != std::string_view::npos) {
        rest = rest.substr(0, colon);
    }
    if (rest.empty() || rest == "0.0.0.0" || rest == "[::]" || rest == "::") {
        return "127.0.0.1";
    }
    return std::string(rest);
}

[[nodiscard]] std::vector<std::string> split_path(std::string_view path) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

[[nodiscard]] UA_DateTime ms_to_ua_datetime(domain::TimestampMs ms) {
    if (ms <= 0) {
        return 0;
    }
    return static_cast<UA_DateTime>(ms) * UA_DATETIME_MSEC + UA_DATETIME_UNIX_EPOCH;
}

[[nodiscard]] const UA_DataType* type_for(project::TagType type) {
    switch (type) {
    case project::TagType::Bool:
        return &UA_TYPES[UA_TYPES_BOOLEAN];
    case project::TagType::UInt16:
        return &UA_TYPES[UA_TYPES_UINT16];
    case project::TagType::Int16:
        return &UA_TYPES[UA_TYPES_INT16];
    case project::TagType::UInt32:
        return &UA_TYPES[UA_TYPES_UINT32];
    case project::TagType::Int32:
        return &UA_TYPES[UA_TYPES_INT32];
    case project::TagType::Float32:
        return &UA_TYPES[UA_TYPES_FLOAT];
    case project::TagType::Float64:
        return &UA_TYPES[UA_TYPES_DOUBLE];
    }
    return &UA_TYPES[UA_TYPES_FLOAT];
}

[[nodiscard]] std::string_view quality_reason_name(domain::QualityReason reason) {
    switch (reason) {
    case domain::QualityReason::None:
        return "None";
    case domain::QualityReason::NoCommunication:
        return "NoCommunication";
    case domain::QualityReason::DeviceFailure:
        return "DeviceFailure";
    case domain::QualityReason::Timeout:
        return "Timeout";
    case domain::QualityReason::ModbusException:
        return "ModbusException";
    case domain::QualityReason::DecodingError:
        return "DecodingError";
    case domain::QualityReason::Stale:
        return "Stale";
    case domain::QualityReason::WritePending:
        return "WritePending";
    case domain::QualityReason::WriteRejected:
        return "WriteRejected";
    case domain::QualityReason::OutOfRange:
        return "OutOfRange";
    }
    return "Unknown";
}

[[nodiscard]] bool fill_variant(UA_Variant& out,
                                project::TagType type,
                                const domain::ScalarValue& value) {
    UA_Variant_init(&out);
    try {
        switch (type) {
        case project::TagType::Bool: {
            UA_Boolean v = std::get<bool>(value);
            return UA_Variant_setScalarCopy(&out, &v, &UA_TYPES[UA_TYPES_BOOLEAN]) ==
                   UA_STATUSCODE_GOOD;
        }
        case project::TagType::UInt16: {
            UA_UInt16 v = std::get<std::uint16_t>(value);
            return UA_Variant_setScalarCopy(&out, &v, &UA_TYPES[UA_TYPES_UINT16]) ==
                   UA_STATUSCODE_GOOD;
        }
        case project::TagType::Int16: {
            UA_Int16 v = std::get<std::int16_t>(value);
            return UA_Variant_setScalarCopy(&out, &v, &UA_TYPES[UA_TYPES_INT16]) ==
                   UA_STATUSCODE_GOOD;
        }
        case project::TagType::UInt32: {
            UA_UInt32 v = std::get<std::uint32_t>(value);
            return UA_Variant_setScalarCopy(&out, &v, &UA_TYPES[UA_TYPES_UINT32]) ==
                   UA_STATUSCODE_GOOD;
        }
        case project::TagType::Int32: {
            UA_Int32 v = std::get<std::int32_t>(value);
            return UA_Variant_setScalarCopy(&out, &v, &UA_TYPES[UA_TYPES_INT32]) ==
                   UA_STATUSCODE_GOOD;
        }
        case project::TagType::Float32: {
            UA_Float v = std::get<float>(value);
            return UA_Variant_setScalarCopy(&out, &v, &UA_TYPES[UA_TYPES_FLOAT]) ==
                   UA_STATUSCODE_GOOD;
        }
        case project::TagType::Float64: {
            UA_Double v = std::get<double>(value);
            return UA_Variant_setScalarCopy(&out, &v, &UA_TYPES[UA_TYPES_DOUBLE]) ==
                   UA_STATUSCODE_GOOD;
        }
        }
    } catch (const std::bad_variant_access&) {
        return false;
    }
    return false;
}

[[nodiscard]] std::optional<domain::ScalarValue> variant_to_scalar(project::TagType type,
                                                                   const UA_Variant& variant) {
    if (!variant.type || !variant.data) {
        return std::nullopt;
    }
    switch (type) {
    case project::TagType::Bool:
        if (variant.type != &UA_TYPES[UA_TYPES_BOOLEAN]) {
            return std::nullopt;
        }
        return static_cast<bool>(*static_cast<UA_Boolean*>(variant.data));
    case project::TagType::UInt16:
        if (variant.type != &UA_TYPES[UA_TYPES_UINT16]) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(*static_cast<UA_UInt16*>(variant.data));
    case project::TagType::Int16:
        if (variant.type != &UA_TYPES[UA_TYPES_INT16]) {
            return std::nullopt;
        }
        return static_cast<std::int16_t>(*static_cast<UA_Int16*>(variant.data));
    case project::TagType::UInt32:
        if (variant.type != &UA_TYPES[UA_TYPES_UINT32]) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*static_cast<UA_UInt32*>(variant.data));
    case project::TagType::Int32:
        if (variant.type != &UA_TYPES[UA_TYPES_INT32]) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(*static_cast<UA_Int32*>(variant.data));
    case project::TagType::Float32:
        if (variant.type != &UA_TYPES[UA_TYPES_FLOAT]) {
            return std::nullopt;
        }
        return static_cast<float>(*static_cast<UA_Float*>(variant.data));
    case project::TagType::Float64:
        if (variant.type != &UA_TYPES[UA_TYPES_DOUBLE]) {
            return std::nullopt;
        }
        return static_cast<double>(*static_cast<UA_Double*>(variant.data));
    }
    return std::nullopt;
}

UA_StatusCode data_source_read(UA_Server* /*server*/,
                               const UA_NodeId* /*session_id*/,
                               void* /*session_context*/,
                               const UA_NodeId* /*node_id*/,
                               void* node_context,
                               UA_Boolean include_source_timestamp,
                               const UA_NumericRange* /*range*/,
                               UA_DataValue* value) {
    auto* ctx = static_cast<OpcUaNodeContext*>(node_context);
    if (ctx == nullptr || ctx->self == nullptr || value == nullptr) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    UA_DataValue_init(value);
    value->hasStatus = true;

    ports::ITagStore* store = ctx->self->store();
    if (store == nullptr) {
        value->status = UA_STATUSCODE_BADNOCOMMUNICATION;
        return UA_STATUSCODE_GOOD;
    }

    const auto tv = store->get(ctx->tag_id);
    if (!tv) {
        value->status = UA_STATUSCODE_BADNOCOMMUNICATION;
        return UA_STATUSCODE_GOOD;
    }

    value->status = static_cast<UA_StatusCode>(
        OpcUaServer::quality_to_status(tv->quality, tv->reason));

    if (!std::holds_alternative<std::monostate>(tv->value)) {
        if (fill_variant(value->value, ctx->type, tv->value)) {
            value->hasValue = true;
        }
    }

    if (include_source_timestamp && tv->source_ts > 0) {
        value->hasSourceTimestamp = true;
        value->sourceTimestamp = ms_to_ua_datetime(tv->source_ts);
    }
    if (tv->server_ts > 0) {
        value->hasServerTimestamp = true;
        value->serverTimestamp = ms_to_ua_datetime(tv->server_ts);
    }
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode data_source_write(UA_Server* /*server*/,
                                const UA_NodeId* /*session_id*/,
                                void* /*session_context*/,
                                const UA_NodeId* /*node_id*/,
                                void* node_context,
                                const UA_NumericRange* /*range*/,
                                const UA_DataValue* data) {
    auto* ctx = static_cast<OpcUaNodeContext*>(node_context);
    if (ctx == nullptr || ctx->self == nullptr || data == nullptr || !data->hasValue) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }
    if (!ctx->writable) {
        return UA_STATUSCODE_BADNOTWRITABLE;
    }

    auto scalar = variant_to_scalar(ctx->type, data->value);
    if (!scalar) {
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }

    auto& handler = ctx->self->write_handler();
    if (!handler) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    auto result = handler(ctx->tag_id, std::move(*scalar));
    if (!result) {
        log_msg(ctx->self->log(), ports::LogLevel::Warn,
                "UA write enqueue failed: " + result.error().message);
        return static_cast<UA_StatusCode>(OpcUaServer::map_error_to_status(result.error()));
    }

    if (ports::ITagStore* store = ctx->self->store(); store != nullptr) {
        store->publish(ctx->tag_id,
                       domain::with_quality(store->get(ctx->tag_id),
                                            domain::Quality::Uncertain,
                                            domain::QualityReason::WritePending,
                                            wall_now_ms()));
    }
    return UA_STATUSCODE_GOOD;
}

}  // namespace

OpcUaServer::OpcUaServer(ports::ILog* log, ports::IMetrics* metrics, OpcUaSecurityOptions security)
    : log_(log), metrics_(metrics), security_(std::move(security)) {}

OpcUaServer::~OpcUaServer() {
    stop();
}

domain::Result<void> OpcUaServer::start(std::shared_ptr<const project::Project> project) {
    if (server_ != nullptr) {
        return {};
    }
    if (!project) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::InvalidArgument, "project is null", "adapters.opcua", false});
    }
    project_ = std::move(project);

    const bool want_secure = project_->opcua.security_mode != project::SecurityMode::None;
    if (want_secure && !ua_encryption_built()) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::NotImplemented,
            "project requests Sign/Encrypt but open62541 was built without UA_ENABLE_ENCRYPTION",
            "adapters.opcua",
            false});
    }

    server_ = UA_Server_new();
    if (server_ == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Internal, "UA_Server_new failed", "adapters.opcua", false});
    }

    UA_ServerConfig* config = UA_Server_getConfig(server_);
    const auto port = parse_endpoint_port(project_->opcua.endpoint_url, 4840);
    const auto host = parse_endpoint_host(project_->opcua.endpoint_url);
    // parse_endpoint_host maps wildcards to 127.0.0.1; detect original wildcard separately.
    const bool wildcard_bind = [&] {
        constexpr std::string_view kPrefix = "opc.tcp://";
        std::string_view rest = project_->opcua.endpoint_url;
        if (rest.starts_with(kPrefix)) {
            rest.remove_prefix(kPrefix.size());
        }
        const auto slash = rest.find('/');
        if (slash != std::string_view::npos) {
            rest = rest.substr(0, slash);
        }
        const auto colon = rest.rfind(':');
        if (colon != std::string_view::npos) {
            rest = rest.substr(0, colon);
        }
        return rest.empty() || rest == "0.0.0.0" || rest == "[::]" || rest == "::";
    }();

    auto status = UA_STATUSCODE_GOOD;
    if (!want_secure) {
        status = UA_ServerConfig_setMinimal(config, port, nullptr);
    } else {
#ifdef UA_ENABLE_ENCRYPTION
        const auto uri = project_->opcua.namespace_uri.empty() ? std::string{"urn:opc-server:application"}
                                                               : project_->opcua.namespace_uri;
        auto material = load_or_create_application_cert(uri, security_, log_);
        if (!material) {
            UA_Server_delete(server_);
            server_ = nullptr;
            return std::unexpected(material.error());
        }
        UA_ByteString certificate{};
        certificate.length = material->first.size();
        certificate.data = material->first.data();
        UA_ByteString private_key{};
        private_key.length = material->second.size();
        private_key.data = material->second.data();

        std::vector<std::vector<std::uint8_t>> trust_store;
        std::vector<UA_ByteString> trust_list;
        bool trust_ok = true;
        for (const auto& path : security_.trust_list) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                trust_ok = false;
                break;
            }
            trust_store.emplace_back(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
        }
        if (!trust_ok) {
            UA_Server_delete(server_);
            server_ = nullptr;
            return std::unexpected(domain::Error{
                domain::ErrorCode::InvalidArgument,
                "cannot read --ua-trust certificate",
                "adapters.opcua",
                false});
        }
        trust_list.resize(trust_store.size());
        for (std::size_t i = 0; i < trust_store.size(); ++i) {
            trust_list[i].length = trust_store[i].size();
            trust_list[i].data = trust_store[i].empty() ? nullptr : trust_store[i].data();
        }

        std::vector<std::vector<std::uint8_t>> revocation_store;
        std::vector<UA_ByteString> revocation_list;
        bool revocation_ok = true;
        for (const auto& path : security_.revocation_list) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                revocation_ok = false;
                break;
            }
            revocation_store.emplace_back(std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>());
        }
        if (!revocation_ok) {
            UA_Server_delete(server_);
            server_ = nullptr;
            return std::unexpected(domain::Error{
                domain::ErrorCode::InvalidArgument,
                "cannot read --ua-crl revocation list",
                "adapters.opcua",
                false});
        }
        revocation_list.resize(revocation_store.size());
        for (std::size_t i = 0; i < revocation_store.size(); ++i) {
            revocation_list[i].length = revocation_store[i].size();
            revocation_list[i].data =
                revocation_store[i].empty() ? nullptr : revocation_store[i].data();
        }

        status = UA_ServerConfig_setDefaultWithSecurityPolicies(
            config, port, &certificate, &private_key,
            trust_list.empty() ? nullptr : trust_list.data(), trust_list.size(),
            nullptr, 0,
            revocation_list.empty() ? nullptr : revocation_list.data(), revocation_list.size());
        if (status == UA_STATUSCODE_GOOD) {
            if (security_.accept_untrusted) {
                UA_CertificateVerification_AcceptAll(&config->secureChannelPKI);
                UA_CertificateVerification_AcceptAll(&config->sessionPKI);
            }
            const auto mode = static_cast<UA_MessageSecurityMode>(
                ua_message_security_mode(project_->opcua.security_mode));
            const char* policy = ua_security_policy_uri(project_->opcua.security_policy);
            if (project_->opcua.security_policy == project::SecurityPolicy::None) {
                policy = ua_security_policy_uri(project::SecurityPolicy::Basic256Sha256);
            }
            keep_security_endpoints(config, mode, policy);
            if (config->endpointsSize == 0) {
                status = UA_STATUSCODE_BADSECURITYCHECKSFAILED;
            }
        }
#else
        status = UA_STATUSCODE_BADINTERNALERROR;
#endif
    }
    if (status != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server_);
        server_ = nullptr;
        return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                             want_secure ? "UA encrypted config failed"
                                                         : "UA_ServerConfig_setMinimal failed",
                                             "adapters.opcua",
                                             false});
    }

    std::ostringstream advertised;
    advertised << "opc.tcp://" << host << ':' << port;
    endpoint_url_ = advertised.str();

    // For an explicit host (including 127.0.0.1 in tests), bind/advertise that URL so
    // GetEndpoints matches the client dial string. Wildcard URLs keep setMinimal's
    // "listen on all interfaces" behaviour.
    if (!wildcard_bind) {
        if (config->serverUrlsSize > 0) {
            UA_Array_delete(config->serverUrls, config->serverUrlsSize, &UA_TYPES[UA_TYPES_STRING]);
            config->serverUrls = nullptr;
            config->serverUrlsSize = 0;
        }
        UA_String* urls =
            static_cast<UA_String*>(UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]));
        if (urls == nullptr) {
            UA_Server_delete(server_);
            server_ = nullptr;
            return std::unexpected(domain::Error{
                domain::ErrorCode::Internal, "UA_Array_new failed", "adapters.opcua", false});
        }
        urls[0] = UA_STRING_ALLOC(endpoint_url_.c_str());
        config->serverUrls = urls;
        config->serverUrlsSize = 1;
    }

    if (auto identity = apply_identity_access_control(config, project_->opcua, log_); !identity) {
        UA_Server_delete(server_);
        server_ = nullptr;
        return identity;
    }

    install_session_hooks(server_, this);
    if (metrics_ != nullptr) {
        metrics_->gauge_set("ua_sessions", 0.0);
    }

    const auto& app_name = project_->opcua.application_name.empty() ? project_->name
                                                                    : project_->opcua.application_name;
    UA_LocalizedText_clear(&config->applicationDescription.applicationName);
    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", app_name.c_str());

    if (!project_->opcua.namespace_uri.empty()) {
        UA_String_clear(&config->applicationDescription.applicationUri);
        config->applicationDescription.applicationUri =
            UA_STRING_ALLOC(project_->opcua.namespace_uri.c_str());
    }

    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        uninstall_session_hooks(server_);
        UA_Server_delete(server_);
        server_ = nullptr;
        return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                             "UA_Server_run_startup failed",
                                             "adapters.opcua",
                                             false});
    }

    const char* ns_uri = project_->opcua.namespace_uri.empty() ? "urn:opc-server:default"
                                                               : project_->opcua.namespace_uri.c_str();
    ns_index_ = UA_Server_addNamespace(server_, ns_uri);
    if (auto diagnostics = add_diagnostics(); !diagnostics) {
        uninstall_session_hooks(server_);
        UA_Server_run_shutdown(server_);
        UA_Server_delete(server_);
        server_ = nullptr;
        return diagnostics;
    }

    log_msg(log_, ports::LogLevel::Info, "OPC UA listening on " + endpoint_url_);
    return {};
}

void OpcUaServer::stop() {
    pump_running_ = false;
    if (pump_thread_.joinable()) {
        pump_thread_.join();
    }
    if (store_ != nullptr && store_subscription_ != 0) {
        store_->unsubscribe(store_subscription_);
        store_subscription_ = 0;
    }
    store_ = nullptr;
    if (server_ == nullptr) {
        return;
    }
    uninstall_session_hooks(server_);
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    server_ = nullptr;
    folder_ids_.clear();
    tag_node_ids_.clear();
    tag_types_.clear();
    node_contexts_.clear();
    {
        std::lock_guard lock(diagnostics_mutex_);
        latest_quality_.clear();
        good_count_ = 0;
        uncertain_count_ = 0;
        bad_count_ = 0;
        last_error_.clear();
        diagnostics_dirty_ = false;
        active_sessions_.clear();
    }
    if (metrics_ != nullptr) {
        metrics_->gauge_set("ua_sessions", 0.0);
    }
    diagnostics_state_node_ = 0;
    diagnostics_good_node_ = 0;
    diagnostics_uncertain_node_ = 0;
    diagnostics_bad_node_ = 0;
    diagnostics_last_error_node_ = 0;
    log_msg(log_, ports::LogLevel::Info, "OPC UA stopped");
}

domain::Result<void> OpcUaServer::ensure_path(std::string_view node_path, std::uint32_t& out_parent_id) {
    const auto parts = split_path(node_path);
    if (parts.size() < 2) {
        return std::unexpected(domain::Error{domain::ErrorCode::InvalidArgument,
                                             "nodePath must contain at least Folder/Leaf",
                                             "adapters.opcua",
                                             false});
    }

    std::uint32_t parent_id = UA_NS0ID_OBJECTSFOLDER;
    std::string cumulative;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!cumulative.empty()) {
            cumulative.push_back('/');
        }
        cumulative += parts[i];
        if (const auto it = folder_ids_.find(cumulative); it != folder_ids_.end()) {
            parent_id = it->second;
            continue;
        }

        const auto numeric_id = next_numeric_id_++;
        UA_ObjectAttributes attr = UA_ObjectAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", parts[i].c_str());
        attr.description = UA_LOCALIZEDTEXT_ALLOC("en-US", parts[i].c_str());

        UA_NodeId requested = UA_NODEID_NUMERIC(ns_index_, numeric_id);
        UA_NodeId parent = (parent_id == UA_NS0ID_OBJECTSFOLDER)
                               ? UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER)
                               : UA_NODEID_NUMERIC(ns_index_, parent_id);
        UA_NodeId reference = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
        UA_QualifiedName browse = UA_QUALIFIEDNAME_ALLOC(ns_index_, parts[i].c_str());
        UA_NodeId type = UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE);
        UA_NodeId out_id;
        UA_NodeId_init(&out_id);

        const auto status = UA_Server_addObjectNode(server_, requested, parent, reference, browse, type,
                                                    attr, nullptr, &out_id);
        UA_ObjectAttributes_clear(&attr);
        UA_QualifiedName_clear(&browse);
        if (status != UA_STATUSCODE_GOOD) {
            UA_NodeId_clear(&out_id);
            return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                                 "failed to create folder " + parts[i],
                                                 "adapters.opcua",
                                                 false});
        }
        parent_id = out_id.identifier.numeric;
        folder_ids_.emplace(cumulative, parent_id);
        UA_NodeId_clear(&out_id);
    }
    out_parent_id = parent_id;
    return {};
}

domain::Result<void> OpcUaServer::add_variable(domain::TagId tag_id,
                                               std::string_view browse_name,
                                               std::uint32_t parent_id,
                                               const project::Tag& tag) {
    if (tag_node_ids_.contains(tag_id)) {
        return {};
    }

    const auto* data_type = type_for(tag.type);
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", std::string(browse_name).c_str());
    attr.description =
        UA_LOCALIZEDTEXT_ALLOC("en-US", tag.description.empty() ? tag.name.c_str() : tag.description.c_str());
    attr.dataType = data_type->typeId;
    attr.valueRank = UA_VALUERANK_SCALAR;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    if (tag.writable) {
        attr.accessLevel = static_cast<UA_Byte>(attr.accessLevel | UA_ACCESSLEVELMASK_WRITE);
    }
    attr.userAccessLevel = attr.accessLevel;
    UA_Variant_init(&attr.value);
    attr.value.type = data_type;

    const auto numeric_id = next_numeric_id_++;
    UA_NodeId requested = UA_NODEID_NUMERIC(ns_index_, numeric_id);
    // Folders we create live in our namespace; Objects folder is ns=0.
    UA_NodeId parent = (parent_id == UA_NS0ID_OBJECTSFOLDER)
                           ? UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER)
                           : UA_NODEID_NUMERIC(ns_index_, parent_id);

    UA_NodeId reference = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
    const std::string browse_owned{browse_name};
    UA_QualifiedName browse = UA_QUALIFIEDNAME_ALLOC(ns_index_, browse_owned.c_str());
    UA_NodeId type_def = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
    UA_NodeId out_id;
    UA_NodeId_init(&out_id);

    const auto status = UA_Server_addVariableNode(server_, requested, parent, reference, browse, type_def,
                                                  attr, nullptr, &out_id);
    UA_VariableAttributes_clear(&attr);
    UA_QualifiedName_clear(&browse);
    if (status != UA_STATUSCODE_GOOD) {
        UA_NodeId_clear(&out_id);
        std::ostringstream msg;
        msg << "failed to add variable " << browse_owned << " status=0x" << std::hex << status;
        return std::unexpected(
            domain::Error{domain::ErrorCode::Internal, msg.str(), "adapters.opcua", false});
    }

    auto ctx = std::make_unique<OpcUaNodeContext>();
    ctx->self = this;
    ctx->tag_id = tag_id;
    ctx->type = tag.type;
    ctx->writable = tag.writable;
    UA_Server_setNodeContext(server_, out_id, ctx.get());

    UA_DataSource data_source;
    data_source.read = data_source_read;
    data_source.write = data_source_write;
    const auto ds_status = UA_Server_setVariableNode_dataSource(server_, out_id, data_source);
    if (ds_status != UA_STATUSCODE_GOOD) {
        UA_NodeId_clear(&out_id);
        return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                             "failed to set DataSource for " + browse_owned,
                                             "adapters.opcua",
                                             false});
    }

    tag_node_ids_.emplace(tag_id, out_id.identifier.numeric);
    tag_types_.emplace(tag_id, tag.type);
    node_contexts_.push_back(std::move(ctx));
    UA_NodeId_clear(&out_id);
    return {};
}

domain::Result<void> OpcUaServer::bind_tag(domain::TagId id, std::string_view node_path) {
    if (server_ == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Internal, "OPC UA server not started", "adapters.opcua", false});
    }
    if (project_ == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Internal, "project missing", "adapters.opcua", false});
    }

    const project::Tag* found = nullptr;
    for (const auto& device : project_->devices) {
        for (const auto& tag : device.tags) {
            if (tag.node_path == node_path) {
                found = &tag;
                break;
            }
        }
        if (found != nullptr) {
            break;
        }
    }
    if (found == nullptr) {
        return std::unexpected(domain::Error{domain::ErrorCode::NotFound,
                                             "tag with nodePath not found: " + std::string(node_path),
                                             "adapters.opcua",
                                             false});
    }

    std::uint32_t parent_id = 0;
    if (auto r = ensure_path(node_path, parent_id); !r) {
        return r;
    }
    const auto parts = split_path(node_path);
    return add_variable(id, parts.back(), parent_id, *found);
}

domain::Result<void> OpcUaServer::bind_tags(ports::ITagStore& store,
                                            std::span<const ports::OpcUaTagSpec> tags) {
    if (server_ == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Internal, "OPC UA server not started", "adapters.opcua", false});
    }

    if (store_ != nullptr && store_subscription_ != 0) {
        store_->unsubscribe(store_subscription_);
        store_subscription_ = 0;
    }

    store_ = &store;
    for (const auto& spec : tags) {
        if (spec.tag.node_path.empty()) {
            log_msg(log_, ports::LogLevel::Warn, "skip tag without nodePath: " + spec.tag.name);
            continue;
        }
        std::uint32_t parent_id = 0;
        if (auto r = ensure_path(spec.tag.node_path, parent_id); !r) {
            return r;
        }
        const auto parts = split_path(spec.tag.node_path);
        if (auto r = add_variable(spec.id, parts.back(), parent_id, spec.tag); !r) {
            return r;
        }
    }

    {
        std::lock_guard lock(diagnostics_mutex_);
        latest_quality_.clear();
        good_count_ = 0;
        uncertain_count_ = 0;
        bad_count_ = 0;
        last_error_.clear();
        diagnostics_dirty_ = true;
    }
    store_subscription_ = store.subscribe(
        [this](domain::TagId id, const domain::TagValue& value) { note_tag_quality(id, value); });
    for (const auto& spec : tags) {
        if (auto existing = store.get(spec.id)) {
            note_tag_quality(spec.id, *existing);
        }
    }

    serve_async();
    return {};
}

void OpcUaServer::set_write_handler(ports::OpcUaWriteHandler handler) {
    write_handler_ = std::move(handler);
}

void OpcUaServer::serve_async() {
    if (server_ == nullptr || pump_thread_.joinable()) {
        return;
    }
    pump_running_ = true;
    pump_thread_ = std::thread([this] { pump_loop(); });
    log_msg(log_, ports::LogLevel::Info, "OPC UA event loop thread started");
}

std::optional<std::uint32_t> OpcUaServer::node_numeric_id(domain::TagId id) const {
    const auto it = tag_node_ids_.find(id);
    if (it == tag_node_ids_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void OpcUaServer::pump_loop() {
    while (pump_running_ && server_ != nullptr) {
        write_diagnostics();
        UA_Server_run_iterate(server_, true);
    }
}

void OpcUaServer::iterate() {
    if (pump_thread_.joinable()) {
        return;
    }
    write_diagnostics();
    if (server_ != nullptr) {
        UA_Server_run_iterate(server_, true);
    }
}

domain::Result<void> OpcUaServer::add_diagnostics() {
    std::uint32_t parent_id = 0;
    if (auto path = ensure_path("OPC_SERVER/Diagnostics/State", parent_id); !path) {
        return path;
    }

    const auto add = [&](const char* name,
                         const UA_DataType* type,
                         const void* initial_value,
                         std::uint32_t& node_id) -> domain::Result<void> {
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", name);
        attr.description = UA_LOCALIZEDTEXT_ALLOC("en-US", name);
        attr.dataType = type->typeId;
        attr.valueRank = UA_VALUERANK_SCALAR;
        attr.accessLevel = UA_ACCESSLEVELMASK_READ;
        if (UA_Variant_setScalarCopy(&attr.value, initial_value, type) != UA_STATUSCODE_GOOD) {
            UA_VariableAttributes_clear(&attr);
            return std::unexpected(domain::Error{
                domain::ErrorCode::Internal, "failed to initialize diagnostic " + std::string(name),
                "adapters.opcua", false});
        }

        const auto numeric_id = next_numeric_id_++;
        UA_NodeId requested = UA_NODEID_NUMERIC(ns_index_, numeric_id);
        UA_NodeId parent = UA_NODEID_NUMERIC(ns_index_, parent_id);
        UA_NodeId reference = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        UA_QualifiedName browse = UA_QUALIFIEDNAME_ALLOC(ns_index_, name);
        UA_NodeId type_def = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
        UA_NodeId out;
        UA_NodeId_init(&out);
        const auto status = UA_Server_addVariableNode(server_, requested, parent, reference, browse,
                                                      type_def, attr, nullptr, &out);
        UA_VariableAttributes_clear(&attr);
        UA_QualifiedName_clear(&browse);
        if (status != UA_STATUSCODE_GOOD) {
            UA_NodeId_clear(&out);
            return std::unexpected(domain::Error{
                domain::ErrorCode::Internal, "failed to add diagnostic " + std::string(name),
                "adapters.opcua", false});
        }
        node_id = out.identifier.numeric;
        UA_NodeId_clear(&out);
        return {};
    };

    UA_String running = UA_STRING_ALLOC("Running");
    UA_String empty = UA_STRING_ALLOC("");
    UA_UInt64 zero = 0;
    auto result = add("State", &UA_TYPES[UA_TYPES_STRING], &running, diagnostics_state_node_);
    UA_String_clear(&running);
    if (!result) {
        UA_String_clear(&empty);
        return result;
    }
    result = add("GoodCount", &UA_TYPES[UA_TYPES_UINT64], &zero, diagnostics_good_node_);
    if (!result) {
        UA_String_clear(&empty);
        return result;
    }
    result = add("UncertainCount", &UA_TYPES[UA_TYPES_UINT64], &zero, diagnostics_uncertain_node_);
    if (!result) {
        UA_String_clear(&empty);
        return result;
    }
    result = add("BadCount", &UA_TYPES[UA_TYPES_UINT64], &zero, diagnostics_bad_node_);
    if (!result) {
        UA_String_clear(&empty);
        return result;
    }
    result = add("LastError", &UA_TYPES[UA_TYPES_STRING], &empty, diagnostics_last_error_node_);
    UA_String_clear(&empty);
    return result;
}

void OpcUaServer::note_tag_quality(domain::TagId id, const domain::TagValue& value) {
    {
        std::lock_guard lock(diagnostics_mutex_);
        const auto decrement = [this](domain::Quality quality) {
            switch (quality) {
            case domain::Quality::Good:
                --good_count_;
                break;
            case domain::Quality::Uncertain:
                --uncertain_count_;
                break;
            case domain::Quality::Bad:
                --bad_count_;
                break;
            }
        };
        if (const auto previous = latest_quality_.find(id); previous != latest_quality_.end()) {
            decrement(previous->second);
            previous->second = value.quality;
        } else {
            latest_quality_.emplace(id, value.quality);
        }
        switch (value.quality) {
        case domain::Quality::Good:
            ++good_count_;
            break;
        case domain::Quality::Uncertain:
            ++uncertain_count_;
            break;
        case domain::Quality::Bad:
            ++bad_count_;
            last_error_ = "Tag " + std::to_string(id) + ": " +
                          std::string(quality_reason_name(value.reason));
            break;
        }
        diagnostics_dirty_ = true;
    }
    publish_quality_metrics();
}

void OpcUaServer::publish_quality_metrics() {
    if (metrics_ == nullptr) {
        return;
    }
    std::uint64_t good = 0;
    std::uint64_t uncertain = 0;
    std::uint64_t bad = 0;
    {
        std::lock_guard lock(diagnostics_mutex_);
        good = good_count_;
        uncertain = uncertain_count_;
        bad = bad_count_;
    }
    const auto total = good + uncertain + bad;
    metrics_->gauge_set("tag_quality.good", static_cast<double>(good));
    metrics_->gauge_set("tag_quality.uncertain", static_cast<double>(uncertain));
    metrics_->gauge_set("tag_quality.bad", static_cast<double>(bad));
    metrics_->gauge_set("tag_quality", total == 0 ? 0.0 : static_cast<double>(good) / static_cast<double>(total));
}

void OpcUaServer::write_diagnostics() {
    if (server_ == nullptr) {
        return;
    }
    std::uint64_t good = 0;
    std::uint64_t uncertain = 0;
    std::uint64_t bad = 0;
    std::string last_error;
    {
        std::lock_guard lock(diagnostics_mutex_);
        if (!diagnostics_dirty_) {
            return;
        }
        good = good_count_;
        uncertain = uncertain_count_;
        bad = bad_count_;
        last_error = last_error_;
        diagnostics_dirty_ = false;
    }

    const auto write_scalar = [this](std::uint32_t node_id,
                                     const void* value,
                                     const UA_DataType* type) {
        UA_Variant variant;
        UA_Variant_init(&variant);
        if (UA_Variant_setScalarCopy(&variant, value, type) == UA_STATUSCODE_GOOD) {
            const auto node = UA_NODEID_NUMERIC(ns_index_, node_id);
            if (UA_Server_writeValue(server_, node, variant) != UA_STATUSCODE_GOOD) {
                log_msg(log_, ports::LogLevel::Warn, "failed to update OPC UA diagnostics");
            }
        }
        UA_Variant_clear(&variant);
    };

    const UA_UInt64 good_value = good;
    const UA_UInt64 uncertain_value = uncertain;
    const UA_UInt64 bad_value = bad;
    UA_String error_value = UA_STRING_ALLOC(last_error.c_str());
    write_scalar(diagnostics_good_node_, &good_value, &UA_TYPES[UA_TYPES_UINT64]);
    write_scalar(diagnostics_uncertain_node_, &uncertain_value, &UA_TYPES[UA_TYPES_UINT64]);
    write_scalar(diagnostics_bad_node_, &bad_value, &UA_TYPES[UA_TYPES_UINT64]);
    write_scalar(diagnostics_last_error_node_, &error_value, &UA_TYPES[UA_TYPES_STRING]);
    UA_String_clear(&error_value);
}

std::uint32_t OpcUaServer::quality_to_status(domain::Quality quality, domain::QualityReason reason) {
    switch (quality) {
    case domain::Quality::Good:
        return UA_STATUSCODE_GOOD;
    case domain::Quality::Uncertain:
        return UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE;
    case domain::Quality::Bad:
        switch (reason) {
        case domain::QualityReason::Timeout:
        case domain::QualityReason::NoCommunication:
            return UA_STATUSCODE_BADNOCOMMUNICATION;
        case domain::QualityReason::DecodingError:
            return UA_STATUSCODE_BADDECODINGERROR;
        case domain::QualityReason::OutOfRange:
            return UA_STATUSCODE_BADOUTOFRANGE;
        case domain::QualityReason::WriteRejected:
            return UA_STATUSCODE_BADWRITENOTSUPPORTED;
        case domain::QualityReason::ModbusException:
        case domain::QualityReason::DeviceFailure:
            return UA_STATUSCODE_BADDEVICEFAILURE;
        default:
            return UA_STATUSCODE_BADINTERNALERROR;
        }
    }
    return UA_STATUSCODE_BADINTERNALERROR;
}

std::uint32_t OpcUaServer::map_error_to_status(const domain::Error& error) {
    switch (error.code) {
    case domain::ErrorCode::Permission:
        if (error.message.find("writable") != std::string::npos) {
            return UA_STATUSCODE_BADNOTWRITABLE;
        }
        return UA_STATUSCODE_BADUSERACCESSDENIED;
    case domain::ErrorCode::QueueFull:
        return UA_STATUSCODE_BADRESOURCEUNAVAILABLE;
    case domain::ErrorCode::InvalidArgument:
        return UA_STATUSCODE_BADTYPEMISMATCH;
    default:
        return UA_STATUSCODE_BADINTERNALERROR;
    }
}

}  // namespace opc::adapters
