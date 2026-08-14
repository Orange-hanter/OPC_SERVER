#include "monitor_client.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#ifdef UA_ENABLE_ENCRYPTION
#include <open62541/plugin/create_certificate.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/pki_default.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opc::monitor {
namespace {

constexpr auto kReconnectDelay = std::chrono::seconds{2};

void discard_open62541_log(void*, UA_LogLevel, UA_LogCategory, const char*, va_list) {}

std::string ua_string(const UA_String& value) {
    return {reinterpret_cast<const char*>(value.data), value.length};
}

std::string node_id_string(const UA_NodeId& node_id) {
    UA_String encoded;
    UA_String_init(&encoded);
    if (UA_NodeId_print(&node_id, &encoded) != UA_STATUSCODE_GOOD) {
        return {};
    }
    auto result = ua_string(encoded);
    UA_String_clear(&encoded);
    return result;
}

bool parse_node_id(std::string_view text, UA_NodeId& node_id) {
    UA_NodeId_init(&node_id);
    const UA_String input{.length = text.size(),
                          .data = reinterpret_cast<UA_Byte*>(
                              const_cast<char*>(text.data()))};
    return UA_NodeId_parse(&node_id, input) == UA_STATUSCODE_GOOD;
}

std::string node_class_name(UA_NodeClass node_class) {
    switch (node_class) {
    case UA_NODECLASS_OBJECT:
        return "Object";
    case UA_NODECLASS_VARIABLE:
        return "Variable";
    case UA_NODECLASS_METHOD:
        return "Method";
    case UA_NODECLASS_OBJECTTYPE:
        return "ObjectType";
    case UA_NODECLASS_VARIABLETYPE:
        return "VariableType";
    case UA_NODECLASS_REFERENCETYPE:
        return "ReferenceType";
    case UA_NODECLASS_DATATYPE:
        return "DataType";
    case UA_NODECLASS_VIEW:
        return "View";
    default:
        return "Unspecified";
    }
}

nlohmann::json scalar_json(const UA_Variant& value) {
    if (!UA_Variant_isScalar(&value) || value.data == nullptr || value.type == nullptr) {
        return nullptr;
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        return *static_cast<const UA_Boolean*>(value.data) != 0;
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_SBYTE])) {
        return *static_cast<const UA_SByte*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BYTE])) {
        return *static_cast<const UA_Byte*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT16])) {
        return *static_cast<const UA_Int16*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT16])) {
        return *static_cast<const UA_UInt16*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32])) {
        return *static_cast<const UA_Int32*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
        return *static_cast<const UA_UInt32*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT64])) {
        return *static_cast<const UA_Int64*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT64])) {
        return *static_cast<const UA_UInt64*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
        return *static_cast<const UA_Float*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        return *static_cast<const UA_Double*>(value.data);
    }
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_STRING])) {
        return ua_string(*static_cast<const UA_String*>(value.data));
    }
    return nullptr;
}

std::int64_t unix_ms(UA_DateTime value) {
    if (value <= UA_DATETIME_UNIX_EPOCH) {
        return 0;
    }
    return static_cast<std::int64_t>((value - UA_DATETIME_UNIX_EPOCH) / UA_DATETIME_MSEC);
}

std::string subscription_key(const nlohmann::json& id) {
    if (id.is_string()) {
        return "s:" + id.get<std::string>();
    }
    if (id.is_number_unsigned()) {
        return "u:" + std::to_string(id.get<std::uint64_t>());
    }
    if (id.is_number_integer()) {
        return "i:" + std::to_string(id.get<std::int64_t>());
    }
    throw std::invalid_argument("subscriptionId must be a string or integer");
}

void copy_request_id(const nlohmann::json& command, nlohmann::json& event) {
    if (const auto it = command.find("requestId"); it != command.end()) {
        event["requestId"] = *it;
    }
}

}  // namespace

struct MonitorClient::Impl {
    struct Item {
        Impl* owner{};
        nlohmann::json external_id;
        std::string node_id;
        double sampling_interval_ms{250.0};
        UA_UInt32 monitored_item_id{0};
    };

    explicit Impl(EventSink output) : sink(std::move(output)) {
        reset_client();
    }

    ~Impl() {
        close(false);
        if (client != nullptr) {
            UA_Client_delete(client);
        }
    }

    void reset_client() {
        if (client != nullptr) {
            UA_Client_delete(client);
        }
        client = UA_Client_new();
        if (client == nullptr) {
            throw std::runtime_error("UA_Client_new failed");
        }
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));
        auto* config = UA_Client_getConfig(client);
        if (config->logging != nullptr) {
            config->logging->log = discard_open62541_log;
        }
        config->timeout = 3000;
        config->connectivityCheckInterval = 2000;
        config->clientContext = this;
        security_ok = true;
        apply_security(config);
    }

#ifdef UA_ENABLE_ENCRYPTION
    static UA_ByteString bytes_from_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return UA_BYTESTRING_NULL;
        }
        const std::vector<char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UA_ByteString out;
        UA_ByteString_allocBuffer(&out, buf.size());
        if (out.data != nullptr && !buf.empty()) {
            std::memcpy(out.data, buf.data(), buf.size());
        }
        return out;
    }
#endif

    void apply_security(UA_ClientConfig* config) {
        if (security_mode == "None" || security_mode.empty()) {
            return;
        }
#ifndef UA_ENABLE_ENCRYPTION
        security_ok = false;
        emit_error("opc-monitor was built without UA encryption support");
        return;
#else
        UA_ByteString cert = UA_BYTESTRING_NULL;
        UA_ByteString key = UA_BYTESTRING_NULL;
        if (!certificate_path.empty() && !private_key_path.empty()) {
            cert = bytes_from_file(certificate_path);
            key = bytes_from_file(private_key_path);
        } else {
            UA_String subject[3] = {UA_STRING_STATIC("C=RU"), UA_STRING_STATIC("O=OPC_SERVER"),
                                    UA_STRING_STATIC("CN=opc-monitor")};
            UA_String san[2] = {UA_STRING_STATIC("DNS:localhost"),
                                UA_STRING_STATIC("URI:urn:opc-server:monitor")};
            const auto gen = UA_CreateCertificate(UA_Log_Stdout, subject, 3, san, 2,
                                                  UA_CERTIFICATEFORMAT_DER, nullptr, &key, &cert);
            if (gen != UA_STATUSCODE_GOOD) {
                security_ok = false;
                emit_error(std::string("failed to generate monitor certificate: ") +
                               UA_StatusCode_name(gen),
                           gen);
                return;
            }
        }
        UA_String_clear(&config->clientDescription.applicationUri);
        config->clientDescription.applicationUri = UA_STRING_ALLOC("urn:opc-server:monitor");
        const auto st =
            UA_ClientConfig_setDefaultEncryption(config, cert, key, nullptr, 0, nullptr, 0);
        UA_ByteString_clear(&cert);
        UA_ByteString_clear(&key);
        if (st != UA_STATUSCODE_GOOD) {
            security_ok = false;
            emit_error(std::string("failed to set client encryption: ") + UA_StatusCode_name(st), st);
            return;
        }
        config->securityMode = (security_mode == "Sign") ? UA_MESSAGESECURITYMODE_SIGN
                                                         : UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        if (security_policy == "Basic256Sha256" || security_policy == "None" ||
            security_policy.empty()) {
            UA_String_clear(&config->securityPolicyUri);
            config->securityPolicyUri =
                UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
        }
        UA_CertificateVerification_AcceptAll(&config->certificateVerification);
#endif
    }

    void emit(nlohmann::json event) {
        sink(std::move(event));
    }

    void emit_error(std::string message,
                    UA_StatusCode status = UA_STATUSCODE_BADUNEXPECTEDERROR,
                    const nlohmann::json* command = nullptr) {
        nlohmann::json event{{"event", "error"},
                             {"message", std::move(message)},
                             {"statusCode", status},
                             {"statusName", UA_StatusCode_name(status)}};
        if (command != nullptr) {
            copy_request_id(*command, event);
        }
        emit(std::move(event));
    }

    void emit_connection(std::string state,
                         UA_StatusCode status,
                         const nlohmann::json* command = nullptr) {
        nlohmann::json event{{"event", "connection"},
                             {"state", std::move(state)},
                             {"endpoint", endpoint},
                             {"statusCode", status},
                             {"statusName", UA_StatusCode_name(status)}};
        if (command != nullptr) {
            copy_request_id(*command, event);
        }
        emit(std::move(event));
    }

    bool create_server_subscription() {
        auto request = UA_CreateSubscriptionRequest_default();
        request.requestedPublishingInterval = 100.0;
        const auto response =
            UA_Client_Subscriptions_create(client, request, this, nullptr, nullptr);
        if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
            emit_error("create OPC UA subscription failed",
                       response.responseHeader.serviceResult);
            return false;
        }
        server_subscription_id = response.subscriptionId;
        return true;
    }

    static void on_data_change(UA_Client*,
                               UA_UInt32,
                               void*,
                               UA_UInt32,
                               void* context,
                               UA_DataValue* value) {
        auto* item = static_cast<Item*>(context);
        if (item == nullptr || item->owner == nullptr || value == nullptr) {
            return;
        }
        nlohmann::json event{{"event", "dataChange"},
                             {"subscriptionId", item->external_id},
                             {"nodeId", item->node_id},
                             {"value", value->hasValue ? scalar_json(value->value)
                                                       : nlohmann::json(nullptr)},
                             {"statusCode", value->hasStatus ? value->status
                                                             : UA_STATUSCODE_GOOD},
                             {"statusName", UA_StatusCode_name(
                                                value->hasStatus ? value->status
                                                                 : UA_STATUSCODE_GOOD)}};
        if (value->hasSourceTimestamp) {
            event["sourceTimestamp"] = unix_ms(value->sourceTimestamp);
        }
        if (value->hasServerTimestamp) {
            event["serverTimestamp"] = unix_ms(value->serverTimestamp);
        }
        item->owner->emit(std::move(event));
    }

    bool create_monitored_item(Item& item) {
        UA_NodeId node;
        if (!parse_node_id(item.node_id, node)) {
            emit_error("invalid nodeId: " + item.node_id, UA_STATUSCODE_BADNODEIDINVALID);
            return false;
        }
        auto request = UA_MonitoredItemCreateRequest_default(node);
        request.requestedParameters.samplingInterval =
            std::clamp(item.sampling_interval_ms, 10.0, 60'000.0);
        const auto result = UA_Client_MonitoredItems_createDataChange(
            client, server_subscription_id, UA_TIMESTAMPSTORETURN_BOTH, request, &item,
            &Impl::on_data_change, nullptr);
        UA_NodeId_clear(&node);
        if (result.statusCode != UA_STATUSCODE_GOOD) {
            emit_error("create monitored item failed for " + item.node_id, result.statusCode);
            return false;
        }
        item.monitored_item_id = result.monitoredItemId;
        return true;
    }

    bool establish(const nlohmann::json* command = nullptr) {
        if (!security_ok) {
            connected = false;
            emit_connection("disconnected", UA_STATUSCODE_BADSECURITYCHECKSFAILED, command);
            return false;
        }

        UA_StatusCode status = UA_STATUSCODE_GOOD;
        if (!user_certificate_path.empty() || !user_private_key_path.empty()) {
#if defined(UA_ENABLE_ENCRYPTION_OPENSSL) || defined(UA_ENABLE_ENCRYPTION_MBEDTLS)
            if (user_certificate_path.empty() || user_private_key_path.empty()) {
                emit_error("userCertificate and userPrivateKey are both required for X509IdentityToken",
                           UA_STATUSCODE_BADINVALIDARGUMENT, command);
                connected = false;
                emit_connection("disconnected", UA_STATUSCODE_BADINVALIDARGUMENT, command);
                return false;
            }
            UA_ClientConfig* config = UA_Client_getConfig(client);
            UA_ByteString cert = bytes_from_file(user_certificate_path);
            UA_ByteString key = bytes_from_file(user_private_key_path);
            if (cert.data == nullptr || key.data == nullptr) {
                UA_ByteString_clear(&cert);
                UA_ByteString_clear(&key);
                emit_error("failed to read user identity certificate/key",
                           UA_STATUSCODE_BADINVALIDARGUMENT, command);
                connected = false;
                emit_connection("disconnected", UA_STATUSCODE_BADINVALIDARGUMENT, command);
                return false;
            }
            const auto auth = UA_ClientConfig_setAuthenticationCert(config, cert, key);
            UA_ByteString_clear(&cert);
            UA_ByteString_clear(&key);
            if (auth != UA_STATUSCODE_GOOD) {
                emit_error(std::string("failed to set X509IdentityToken: ") + UA_StatusCode_name(auth),
                           auth, command);
                connected = false;
                emit_connection("disconnected", auth, command);
                return false;
            }
            status = UA_Client_connect(client, endpoint.c_str());
#else
            emit_error("opc-monitor was built without certificate authentication support",
                       UA_STATUSCODE_BADNOTIMPLEMENTED, command);
            connected = false;
            emit_connection("disconnected", UA_STATUSCODE_BADNOTIMPLEMENTED, command);
            return false;
#endif
        } else if (!username.empty()) {
            status = UA_Client_connectUsername(client, endpoint.c_str(), username.c_str(),
                                              password.c_str());
        } else {
            status = UA_Client_connect(client, endpoint.c_str());
        }
        if (status != UA_STATUSCODE_GOOD) {
            connected = false;
            emit_connection("disconnected", status, command);
            return false;
        }
        connected = true;
        reconnect_at = {};
        emit_connection("connected", UA_STATUSCODE_GOOD, command);
        if (!items.empty() && create_server_subscription()) {
            for (auto& [_, item] : items) {
                create_monitored_item(*item);
            }
        }
        return true;
    }

    void connect(const nlohmann::json& command) {
        if (!command.contains("endpoint") || !command["endpoint"].is_string()) {
            emit_error("connect requires string endpoint", UA_STATUSCODE_BADINVALIDARGUMENT,
                       &command);
            return;
        }
        close(false);
        endpoint = command["endpoint"].get<std::string>();
        security_mode = command.value("securityMode", "None");
        security_policy = command.value("securityPolicy", "None");
        certificate_path = command.value("certificate", "");
        private_key_path = command.value("privateKey", "");
        user_certificate_path = command.value("userCertificate", "");
        user_private_key_path = command.value("userPrivateKey", "");
        username.clear();
        password.clear();
        if (command.contains("username") && command["username"].is_string()) {
            username = command["username"].get<std::string>();
        }
        if (command.contains("password") && command["password"].is_string()) {
            password = command["password"].get<std::string>();
        }
        want_connected = true;
        reset_client();
        establish(&command);
        if (!connected) {
            reconnect_at = std::chrono::steady_clock::now() + kReconnectDelay;
        }
    }

    nlohmann::json browse_children(const UA_NodeId& node,
                                   int depth,
                                   std::size_t& node_count,
                                   std::unordered_set<std::string>& visited,
                                   UA_StatusCode& result_status) {
        UA_BrowseRequest request;
        UA_BrowseRequest_init(&request);
        request.nodesToBrowse = UA_BrowseDescription_new();
        request.nodesToBrowseSize = 1;
        UA_NodeId_copy(&node, &request.nodesToBrowse[0].nodeId);
        request.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
        request.nodesToBrowse[0].referenceTypeId =
            UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
        request.nodesToBrowse[0].includeSubtypes = true;
        request.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
        auto response = UA_Client_Service_browse(client, request);
        UA_BrowseRequest_clear(&request);

        if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD ||
            response.resultsSize != 1 ||
            response.results[0].statusCode != UA_STATUSCODE_GOOD) {
            result_status = response.responseHeader.serviceResult != UA_STATUSCODE_GOOD
                                ? response.responseHeader.serviceResult
                                : (response.resultsSize == 1
                                       ? response.results[0].statusCode
                                       : UA_STATUSCODE_BADUNEXPECTEDERROR);
            UA_BrowseResponse_clear(&response);
            return nlohmann::json::array();
        }

        nlohmann::json children = nlohmann::json::array();
        for (std::size_t i = 0;
             i < response.results[0].referencesSize && node_count < 10'000;
             ++i) {
            const auto& ref = response.results[0].references[i];
            const auto id = node_id_string(ref.nodeId.nodeId);
            nlohmann::json child{
                {"nodeId", id},
                {"browseName", ua_string(ref.browseName.name)},
                {"namespaceIndex", ref.browseName.namespaceIndex},
                {"displayName", ua_string(ref.displayName.text)},
                {"nodeClass", node_class_name(ref.nodeClass)},
            };
            ++node_count;
            const bool can_recurse =
                depth > 1 && ref.nodeId.serverIndex == 0 &&
                ref.nodeId.namespaceUri.length == 0 &&
                (ref.nodeClass == UA_NODECLASS_OBJECT || ref.nodeClass == UA_NODECLASS_VIEW);
            if (can_recurse && visited.insert(id).second) {
                UA_StatusCode child_status = UA_STATUSCODE_GOOD;
                child["children"] = browse_children(
                    ref.nodeId.nodeId, depth - 1, node_count, visited, child_status);
            }
            children.push_back(std::move(child));
        }
        UA_BrowseResponse_clear(&response);
        return children;
    }

    void browse(const nlohmann::json& command) {
        if (!connected) {
            emit_error("browse requires an active connection",
                       UA_STATUSCODE_BADNOTCONNECTED, &command);
            return;
        }
        if (!command.contains("nodeId") || !command["nodeId"].is_string()) {
            emit_error("browse requires string nodeId", UA_STATUSCODE_BADINVALIDARGUMENT,
                       &command);
            return;
        }
        UA_NodeId node;
        const auto input = command["nodeId"].get<std::string>();
        if (!parse_node_id(input, node)) {
            emit_error("invalid nodeId: " + input, UA_STATUSCODE_BADNODEIDINVALID, &command);
            return;
        }

        int max_depth = 1;
        if (const auto depth = command.find("maxDepth");
            depth != command.end() && depth->is_number_integer()) {
            max_depth = std::clamp(depth->get<int>(), 1, 16);
        }
        std::size_t node_count = 0;
        std::unordered_set<std::string> visited{input};
        UA_StatusCode browse_status = UA_STATUSCODE_GOOD;
        auto children =
            browse_children(node, max_depth, node_count, visited, browse_status);
        UA_NodeId_clear(&node);

        if (browse_status != UA_STATUSCODE_GOOD) {
            emit_error("browse failed for " + input, browse_status, &command);
            return;
        }

        nlohmann::json event{{"event", "browseResult"},
                             {"nodeId", input},
                             {"children", std::move(children)}};
        copy_request_id(command, event);
        emit(std::move(event));
    }

    void subscribe(const nlohmann::json& command) {
        if (!connected) {
            emit_error("subscribe requires an active connection",
                       UA_STATUSCODE_BADNOTCONNECTED, &command);
            return;
        }
        if (!command.contains("nodeId") || !command["nodeId"].is_string() ||
            !command.contains("subscriptionId")) {
            emit_error("subscribe requires nodeId and subscriptionId",
                       UA_STATUSCODE_BADINVALIDARGUMENT, &command);
            return;
        }
        try {
            const auto key = subscription_key(command["subscriptionId"]);
            if (items.contains(key)) {
                emit_error("subscriptionId already exists",
                           UA_STATUSCODE_BADENTRYEXISTS, &command);
                return;
            }
            if (server_subscription_id == 0 && !create_server_subscription()) {
                return;
            }
            auto item = std::make_unique<Item>();
            item->owner = this;
            item->external_id = command["subscriptionId"];
            item->node_id = command["nodeId"].get<std::string>();
            if (const auto interval = command.find("samplingIntervalMs");
                interval != command.end() && interval->is_number()) {
                item->sampling_interval_ms = interval->get<double>();
            }
            if (!create_monitored_item(*item)) {
                return;
            }
            items.emplace(key, std::move(item));
        } catch (const std::exception& error) {
            emit_error(error.what(), UA_STATUSCODE_BADINVALIDARGUMENT, &command);
        }
    }

    void unsubscribe(const nlohmann::json& command) {
        if (!command.contains("subscriptionId")) {
            emit_error("unsubscribe requires subscriptionId",
                       UA_STATUSCODE_BADINVALIDARGUMENT, &command);
            return;
        }
        try {
            const auto key = subscription_key(command["subscriptionId"]);
            const auto found = items.find(key);
            if (found == items.end()) {
                emit_error("subscriptionId not found", UA_STATUSCODE_BADNOTFOUND, &command);
                return;
            }
            if (connected && found->second->monitored_item_id != 0) {
                const auto status = UA_Client_MonitoredItems_deleteSingle(
                    client, server_subscription_id, found->second->monitored_item_id);
                if (status != UA_STATUSCODE_GOOD) {
                    emit_error("unsubscribe failed", status, &command);
                    return;
                }
            }
            items.erase(found);
            if (items.empty() && connected && server_subscription_id != 0) {
                UA_Client_Subscriptions_deleteSingle(client, server_subscription_id);
                server_subscription_id = 0;
            }
        } catch (const std::exception& error) {
            emit_error(error.what(), UA_STATUSCODE_BADINVALIDARGUMENT, &command);
        }
    }

    void close(bool notify, const nlohmann::json* command = nullptr) {
        want_connected = false;
        if (client != nullptr && connected) {
            UA_Client_disconnect(client);
        }
        connected = false;
        server_subscription_id = 0;
        for (auto& [_, item] : items) {
            item->monitored_item_id = 0;
        }
        if (notify && !endpoint.empty()) {
            emit_connection("disconnected", UA_STATUSCODE_GOOD, command);
        }
    }

    void tick(std::chrono::milliseconds timeout) {
        if (client == nullptr) {
            return;
        }
        if (connected) {
            const auto status = UA_Client_run_iterate(
                client, static_cast<UA_UInt32>(std::max<std::int64_t>(0, timeout.count())));
            UA_SecureChannelState channel;
            UA_SessionState session;
            UA_StatusCode connection_status;
            UA_Client_getState(client, &channel, &session, &connection_status);
            if (status != UA_STATUSCODE_GOOD || session != UA_SESSIONSTATE_ACTIVATED) {
                connected = false;
                server_subscription_id = 0;
                for (auto& [_, item] : items) {
                    item->monitored_item_id = 0;
                }
                emit_connection("disconnected", status != UA_STATUSCODE_GOOD
                                                     ? status
                                                     : connection_status);
                reconnect_at = std::chrono::steady_clock::now() + kReconnectDelay;
            }
            return;
        }
        if (want_connected && std::chrono::steady_clock::now() >= reconnect_at) {
            emit_connection("reconnecting", UA_STATUSCODE_GOOD);
            reset_client();
            if (!establish()) {
                reconnect_at = std::chrono::steady_clock::now() + kReconnectDelay;
            }
        }
    }

    void dispatch(const nlohmann::json& command) {
        if (!command.is_object() || !command.contains("command") ||
            !command["command"].is_string()) {
            emit_error("JSON object with string command is required",
                       UA_STATUSCODE_BADINVALIDARGUMENT, &command);
            return;
        }
        const auto name = command["command"].get<std::string>();
        if (name == "connect") {
            connect(command);
        } else if (name == "browse") {
            browse(command);
        } else if (name == "subscribe") {
            subscribe(command);
        } else if (name == "unsubscribe") {
            unsubscribe(command);
        } else if (name == "disconnect") {
            close(true, &command);
        } else if (name == "shutdown") {
            close(true, &command);
        } else {
            emit_error("unknown command: " + name, UA_STATUSCODE_BADNOTSUPPORTED, &command);
        }
    }

    EventSink sink;
    UA_Client* client{nullptr};
    std::string endpoint;
    std::string security_mode{"None"};
    std::string security_policy{"None"};
    std::string certificate_path;
    std::string private_key_path;
    std::string user_certificate_path;
    std::string user_private_key_path;
    std::string username;
    std::string password;
    bool security_ok{true};
    bool connected{false};
    bool want_connected{false};
    UA_UInt32 server_subscription_id{0};
    std::unordered_map<std::string, std::unique_ptr<Item>> items;
    std::chrono::steady_clock::time_point reconnect_at{};
};

MonitorClient::MonitorClient(EventSink sink) : impl_(std::make_unique<Impl>(std::move(sink))) {}

MonitorClient::~MonitorClient() = default;

void MonitorClient::handle_command(const nlohmann::json& command) {
    impl_->dispatch(command);
}

void MonitorClient::iterate(std::chrono::milliseconds timeout) {
    impl_->tick(timeout);
}

void MonitorClient::shutdown() {
    impl_->close(false);
}

bool MonitorClient::connected() const {
    return impl_->connected;
}

}  // namespace opc::monitor
