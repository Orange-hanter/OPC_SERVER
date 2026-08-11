#include "adapters/opc_ua_server.hpp"

#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <charconv>
#include <sstream>
#include <variant>

namespace opc::adapters {

struct OpcUaWriteNodeContext {
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

void on_client_write(UA_Server* /*server*/,
                     const UA_NodeId* /*session_id*/,
                     void* /*session_context*/,
                     const UA_NodeId* /*node_id*/,
                     void* node_context,
                     const UA_NumericRange* /*range*/,
                     const UA_DataValue* data) {
    auto* ctx = static_cast<OpcUaWriteNodeContext*>(node_context);
    if (ctx == nullptr || ctx->self == nullptr || data == nullptr || !data->hasValue) {
        return;
    }
    if (!ctx->writable) {
        return;
    }
    auto scalar = variant_to_scalar(ctx->type, data->value);
    if (!scalar) {
        return;
    }
    ctx->self->handle_client_write(ctx->tag_id, ctx->type, std::move(*scalar));
}

}  // namespace

OpcUaServer::OpcUaServer(ports::ILog* log) : log_(log) {}

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

    if (project_->opcua.security_policy != project::SecurityPolicy::None ||
        project_->opcua.security_mode != project::SecurityMode::None) {
        log_msg(log_, ports::LogLevel::Warn,
                "security Sign/Encrypt requested but stage-3 uses None; continuing with None");
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

    auto status = UA_ServerConfig_setMinimal(config, port, nullptr);
    if (status != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server_);
        server_ = nullptr;
        return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                             "UA_ServerConfig_setMinimal failed",
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
        store_ = nullptr;
    }
    if (server_ == nullptr) {
        return;
    }
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    server_ = nullptr;
    folder_ids_.clear();
    tag_node_ids_.clear();
    tag_types_.clear();
    node_contexts_.clear();
    {
        std::lock_guard lock(pending_mutex_);
        pending_.clear();
    }
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

    auto ctx = std::make_unique<OpcUaWriteNodeContext>();
    ctx->self = this;
    ctx->tag_id = tag_id;
    ctx->type = tag.type;
    ctx->writable = tag.writable;
    UA_Server_setNodeContext(server_, out_id, ctx.get());

    UA_ValueCallback callback;
    callback.onRead = nullptr;
    callback.onWrite = on_client_write;
    UA_Server_setVariableNode_valueCallback(server_, out_id, callback);

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

domain::Result<void> OpcUaServer::bind_index(const core::RuntimeIndex& index, ports::ITagStore& store) {
    if (server_ == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Internal, "OPC UA server not started", "adapters.opcua", false});
    }

    for (const auto& binding : index.tags()) {
        if (binding.tag.node_path.empty()) {
            log_msg(log_, ports::LogLevel::Warn,
                    "skip tag without nodePath: " + binding.tag.name);
            continue;
        }
        std::uint32_t parent_id = 0;
        if (auto r = ensure_path(binding.tag.node_path, parent_id); !r) {
            return r;
        }
        const auto parts = split_path(binding.tag.node_path);
        if (auto r = add_variable(binding.id, parts.back(), parent_id, binding.tag); !r) {
            return r;
        }
        if (auto existing = store.get(binding.id)) {
            enqueue_update(binding.id, *existing);
        }
    }

    if (store_subscription_ != 0 && store_ != nullptr) {
        store_->unsubscribe(store_subscription_);
        store_subscription_ = 0;
    }
    store_ = &store;
    store_subscription_ = store.subscribe([this](domain::TagId id, const domain::TagValue& value) {
        enqueue_update(id, value);
    });
    serve_async();
    return {};
}

void OpcUaServer::serve_async() {
    if (server_ == nullptr || pump_thread_.joinable()) {
        return;
    }
    pump_running_ = true;
    pump_thread_ = std::thread([this] { pump_loop(); });
    log_msg(log_, ports::LogLevel::Info, "OPC UA event loop thread started");
}

void OpcUaServer::set_write_handler(WriteHandler handler) {
    write_handler_ = std::move(handler);
}

std::optional<std::uint32_t> OpcUaServer::node_numeric_id(domain::TagId id) const {
    const auto it = tag_node_ids_.find(id);
    if (it == tag_node_ids_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void OpcUaServer::handle_client_write(domain::TagId id,
                                      project::TagType /*type*/,
                                      domain::ScalarValue value) {
    if (applying_store_update_) {
        return;
    }
    if (!write_handler_) {
        log_msg(log_, ports::LogLevel::Warn, "UA write ignored: no write handler");
        return;
    }

    if (store_ != nullptr) {
        domain::TagValue pending;
        pending.value = value;
        pending.quality = domain::Quality::Uncertain;
        pending.reason = domain::QualityReason::WritePending;
        // Avoid feedback loop: publish would enqueue UA update; skip timestamps for now.
        pending.server_ts = 0;
        pending.source_ts = 0;
        store_->publish(id, pending);
    }

    auto result = write_handler_(id, std::move(value));
    if (!result) {
        log_msg(log_, ports::LogLevel::Warn, "UA write enqueue failed: " + result.error().message);
        if (store_ != nullptr) {
            domain::TagValue bad;
            bad.quality = domain::Quality::Bad;
            bad.reason = domain::QualityReason::WriteRejected;
            store_->publish(id, bad);
        }
    }
}

void OpcUaServer::pump_loop() {
    while (pump_running_ && server_ != nullptr) {
        flush_updates();
        UA_Server_run_iterate(server_, true);
    }
}

void OpcUaServer::enqueue_update(domain::TagId id, const domain::TagValue& value) {
    std::lock_guard lock(pending_mutex_);
    pending_[id] = value;
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

void OpcUaServer::flush_updates() {
    if (server_ == nullptr) {
        return;
    }
    std::unordered_map<domain::TagId, domain::TagValue> batch;
    {
        std::lock_guard lock(pending_mutex_);
        batch.swap(pending_);
    }
    for (const auto& [id, value] : batch) {
        const auto node_it = tag_node_ids_.find(id);
        const auto type_it = tag_types_.find(id);
        if (node_it == tag_node_ids_.end() || type_it == tag_types_.end()) {
            continue;
        }

        UA_DataValue dv;
        UA_DataValue_init(&dv);
        dv.hasStatus = true;
        dv.status = static_cast<UA_StatusCode>(quality_to_status(value.quality, value.reason));

        if (!std::holds_alternative<std::monostate>(value.value)) {
            if (fill_variant(dv.value, type_it->second, value.value)) {
                dv.hasValue = true;
            }
        }

        if (value.source_ts > 0) {
            dv.hasSourceTimestamp = true;
            dv.sourceTimestamp = ms_to_ua_datetime(value.source_ts);
        }
        if (value.server_ts > 0) {
            dv.hasServerTimestamp = true;
            dv.serverTimestamp = ms_to_ua_datetime(value.server_ts);
        }

        UA_NodeId node = UA_NODEID_NUMERIC(ns_index_, node_it->second);
        applying_store_update_ = true;
        const auto status = UA_Server_writeDataValue(server_, node, dv);
        applying_store_update_ = false;
        UA_DataValue_clear(&dv);
        if (status != UA_STATUSCODE_GOOD) {
            log_msg(log_, ports::LogLevel::Warn,
                    "writeDataValue failed for tag " + std::to_string(id));
        }
    }
}

void OpcUaServer::iterate() {
    // When the background pump owns the UA thread, only nudge pending flush via queue.
    if (pump_thread_.joinable()) {
        return;
    }
    flush_updates();
    if (server_ != nullptr) {
        UA_Server_run_iterate(server_, true);
    }
}

}  // namespace opc::adapters
