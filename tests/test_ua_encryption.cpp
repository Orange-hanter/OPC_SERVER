#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/opc_ua_server.hpp"
#include "adapters/ua_pki.hpp"
#include "app/cli_options.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "ports/i_log.hpp"
#include "project/load.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/config.h>
#ifdef UA_ENABLE_ENCRYPTION
#include <open62541/plugin/pki_default.h>
#endif

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using opc::adapters::OpcUaServer;
using opc::ports::NullLog;

namespace {

std::uint16_t free_tcp_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    socklen_t len = sizeof(addr);
    REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    const auto port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

std::shared_ptr<opc::project::Project> secure_project(std::uint16_t port, const char* mode) {
    const std::string json = R"({
      "schemaVersion": 1,
      "name": "ua-secure",
      "opcua": {
        "endpointUrl": "opc.tcp://127.0.0.1:)" +
                             std::to_string(port) + R"(",
        "applicationName": "OPC_SERVER Secure",
        "securityPolicy": "Basic256Sha256",
        "securityMode": ")" +
                             std::string(mode) + R"(",
        "namespaceUri": "urn:opc-server:ua-secure"
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Temp", "nodePath": "Plant/Temp", "area": "holding",
           "address": 0, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1", "tagNames": ["Temp"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(json, "secure.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("parse_cli recognizes UA certificate flags", "[app][cli]") {
    const char* argv[] = {"OPC_SERVER", "--ua-cert", "c.der", "--ua-key", "k.der", "--ua-strict-certs"};
    auto opts = opc::app::parse_cli(6, argv);
    REQUIRE(opts.errors.empty());
    CHECK(opts.ua_cert_path == "c.der");
    CHECK(opts.ua_key_path == "k.der");
    CHECK(opts.ua_strict_certs);
}

#ifdef UA_ENABLE_ENCRYPTION
TEST_CASE("OpcUaServer SignAndEncrypt is honored and readable", "[opcua][encryption]") {
    REQUIRE(opc::adapters::ua_encryption_built());
    const auto port = free_tcp_port();
    auto project = secure_project(port, "SignAndEncrypt");
    NullLog log;
    OpcUaServer server{&log, nullptr};
    auto started = server.start(project);
    REQUIRE(started);

    opc::core::RuntimeIndex index = opc::core::RuntimeIndex::build(project);
    opc::core::TagStore store;
    std::vector<opc::ports::OpcUaTagSpec> specs;
    for (const auto& b : index.tags()) {
        specs.push_back({.id = b.id, .tag = b.tag});
    }
    REQUIRE(server.bind_tags(store, specs));
    auto temp = index.find_by_name("Temp");
    REQUIRE(temp);
    store.publish(temp->id, opc::domain::TagValue{.value = static_cast<std::uint16_t>(42),
                                                  .quality = opc::domain::Quality::Good,
                                                  .reason = opc::domain::QualityReason::None,
                                                  .source_ts = 1,
                                                  .server_ts = 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    auto client_material =
        opc::adapters::load_or_create_application_cert("urn:opc-server:ua-secure-client", {}, nullptr);
    REQUIRE(client_material);

    UA_Client* client = UA_Client_new();
    REQUIRE(client != nullptr);
    UA_ClientConfig* cc = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(cc);
    UA_ByteString cert{};
    cert.length = client_material->first.size();
    cert.data = client_material->first.data();
    UA_ByteString key{};
    key.length = client_material->second.size();
    key.data = client_material->second.data();
    UA_String_clear(&cc->clientDescription.applicationUri);
    cc->clientDescription.applicationUri = UA_STRING_ALLOC("urn:opc-server:ua-secure-client");
    REQUIRE(UA_ClientConfig_setDefaultEncryption(cc, cert, key, nullptr, 0, nullptr, 0) ==
            UA_STATUSCODE_GOOD);
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    UA_String_clear(&cc->securityPolicyUri);
    cc->securityPolicyUri =
        UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
    UA_CertificateVerification_AcceptAll(&cc->certificateVerification);

    const auto connect = UA_Client_connect(client, server.endpoint_url().c_str());
    REQUIRE(connect == UA_STATUSCODE_GOOD);

    UA_Variant value;
    UA_Variant_init(&value);
    const UA_NodeId node = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    REQUIRE(UA_Client_readValueAttribute(client, node, &value) == UA_STATUSCODE_GOOD);
    UA_Variant_clear(&value);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    server.stop();
}
#else
TEST_CASE("SignAndEncrypt fails closed without encryption build", "[opcua][encryption]") {
    CHECK_FALSE(opc::adapters::ua_encryption_built());
    const auto port = free_tcp_port();
    auto project = secure_project(port, "SignAndEncrypt");
    NullLog log;
    OpcUaServer server{&log, nullptr};
    auto started = server.start(project);
    REQUIRE_FALSE(started);
    CHECK(started.error().message.find("ENCRYPTION") != std::string::npos);
}
#endif
