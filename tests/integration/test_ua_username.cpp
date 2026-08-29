#include <catch2/catch_test_macros.hpp>

#include "adapters/opc_ua_server.hpp"
#include "app/cli_options.hpp"
#include "ports/i_log.hpp"
#include "project/load.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

#include <arpa/inet.h>
#include <chrono>
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

std::shared_ptr<opc::project::Project> username_project(std::uint16_t port,
                                                       bool allow_none_password,
                                                       bool allow_anonymous) {
    const std::string json = R"({
      "schemaVersion": 1,
      "name": "ua-users",
      "opcua": {
        "endpointUrl": "opc.tcp://127.0.0.1:)" +
                             std::to_string(port) + R"(",
        "applicationName": "OPC_SERVER Users",
        "securityPolicy": "None",
        "securityMode": "None",
        "namespaceUri": "urn:opc-server:ua-users",
        "allowAnonymous": )" +
                             std::string(allow_anonymous ? "true" : "false") + R"(,
        "allowNonePassword": )" +
                             std::string(allow_none_password ? "true" : "false") + R"(,
        "users": [
          {"username": "operator", "password": "secret"}
        ]
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
    auto loaded = opc::project::load_json_text(json, "users.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("parse_cli recognizes username identity flags", "[app][cli]") {
    const char* argv[] = {"OPC_SERVER",
                          "--ua-user",
                          "alice:p:ass",
                          "--ua-user",
                          "bob:x",
                          "--ua-deny-anonymous",
                          "--ua-allow-none-password"};
    auto opts = opc::app::parse_cli(7, argv);
    REQUIRE(opts.errors.empty());
    REQUIRE(opts.ua_users.size() == 2);
    CHECK(opts.ua_users[0].username == "alice");
    CHECK(opts.ua_users[0].password == "p:ass");
    CHECK(opts.ua_users[1].username == "bob");
    CHECK(opts.ua_users[1].password == "x");
    CHECK(opts.ua_deny_anonymous);
    CHECK(opts.ua_allow_none_password);
}

TEST_CASE("project load parses opcua users and fail-closed anonymous default", "[project][opcua]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "users-load",
      "opcua": {
        "endpointUrl": "opc.tcp://0.0.0.0:4840",
        "users": [{"username": "u1", "password": "p1"}]
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1", "tagNames": ["A"]}
      ]
    })";
    const auto loaded = opc::project::load_json_text(kJson, "users-load.json");
    REQUIRE(loaded.ok);
    REQUIRE(loaded.project.opcua.users.size() == 1);
    CHECK(loaded.project.opcua.users[0].username == "u1");
    CHECK(loaded.project.opcua.users[0].password == "p1");
    CHECK_FALSE(loaded.project.opcua.allow_anonymous);
    CHECK_FALSE(loaded.project.opcua.allow_none_password);
}

TEST_CASE("OpcUaServer refuses username over None without allowNonePassword", "[opcua][username]") {
    const auto port = free_tcp_port();
    auto project = username_project(port, /*allow_none_password=*/false, /*allow_anonymous=*/false);
    NullLog log;
    OpcUaServer server{&log};
    auto started = server.start(project);
    REQUIRE_FALSE(started);
    CHECK(started.error().message.find("allowNonePassword") != std::string::npos);
}

TEST_CASE("OpcUaServer username token accepts good credentials and rejects bad", "[opcua][username]") {
    const auto port = free_tcp_port();
    auto project = username_project(port, /*allow_none_password=*/true, /*allow_anonymous=*/false);
    NullLog log;
    OpcUaServer server{&log};
    REQUIRE(server.start(project));
    server.serve_async();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    {
        UA_Client* client = UA_Client_new();
        REQUIRE(client != nullptr);
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));
        const auto bad = UA_Client_connectUsername(client, server.endpoint_url().c_str(), "operator",
                                                   "wrong");
        CHECK(bad != UA_STATUSCODE_GOOD);
        UA_Client_delete(client);
    }

    {
        UA_Client* anon = UA_Client_new();
        REQUIRE(anon != nullptr);
        UA_ClientConfig_setDefault(UA_Client_getConfig(anon));
        const auto denied = UA_Client_connect(anon, server.endpoint_url().c_str());
        CHECK(denied != UA_STATUSCODE_GOOD);
        UA_Client_delete(anon);
    }

    {
        UA_Client* client = UA_Client_new();
        REQUIRE(client != nullptr);
        UA_ClientConfig* cc = UA_Client_getConfig(client);
        UA_ClientConfig_setDefault(cc);
        cc->timeout = 3000;
        const auto ok = UA_Client_connectUsername(client, server.endpoint_url().c_str(), "operator",
                                                  "secret");
        REQUIRE(ok == UA_STATUSCODE_GOOD);
        UA_Variant value;
        UA_Variant_init(&value);
        const UA_NodeId node = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
        REQUIRE(UA_Client_readValueAttribute(client, node, &value) == UA_STATUSCODE_GOOD);
        UA_Variant_clear(&value);
        UA_Client_disconnect(client);
        UA_Client_delete(client);
    }

    server.stop();
}
