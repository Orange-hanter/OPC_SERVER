#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/memory_metrics.hpp"
#include "adapters/opc_ua_server.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "app/cli_options.hpp"
#include "app/server_runtime.hpp"
#include "core/translator.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "ports/i_opc_ua_facade.hpp"

using opc::adapters::ManualClock;
using opc::adapters::OpcUaServer;
using opc::adapters::testsupport::FakeModbusTransport;
using opc::app::ServerRuntime;
using opc::app::ServerRuntimeDeps;
using opc::core::Translator;
using opc::ports::NullLog;
using opc::ports::NullMetrics;

namespace {

std::uint16_t free_tcp_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(fd);
        return static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
    }
    const auto port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

std::shared_ptr<opc::project::Project> ua_sample_project(std::uint16_t port) {
    const std::string json = R"({
      "schemaVersion": 1,
      "name": "ua-demo",
      "opcua": {
        "endpointUrl": "opc.tcp://127.0.0.1:)" +
                             std::to_string(port) + R"(",
        "applicationName": "OPC_SERVER UA Test",
        "securityPolicy": "None",
        "securityMode": "None",
        "namespaceUri": "urn:opc-server:ua-test"
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Tank1.Level", "nodePath": "Plant/Tank1/Level", "area": "holding",
           "address": 0, "type": "float32", "byteOrder": "ABCD", "group": "g1"},
          {"name": "Tank1.Status", "nodePath": "Plant/Tank1/Status", "area": "holding",
           "address": 2, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1",
         "tagNames": ["Tank1.Level", "Tank1.Status"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(json, "ua.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("parse_cli recognizes --no-opcua", "[app][cli]") {
    const char* argv[] = {"OPC_SERVER", "--no-opcua", "--once"};
    auto opts = opc::app::parse_cli(3, argv);
    REQUIRE(opts.errors.empty());
    CHECK_FALSE(opts.enable_opcua);
    CHECK(opts.once);
}

TEST_CASE("OpcUaServer exposes TagStore values via Read", "[opcua][read]") {
    const auto port = free_tcp_port();
    auto project = ua_sample_project(port);
    NullLog log;
    opc::adapters::MemoryMetrics metrics;
    OpcUaServer server{&log, &metrics};

    REQUIRE(server.start(project));

    opc::core::RuntimeIndex index = opc::core::RuntimeIndex::build(project);
    opc::core::TagStore store;
    std::vector<opc::ports::OpcUaTagSpec> specs;
    for (const auto& b : index.tags()) {
        specs.push_back({.id = b.id, .tag = b.tag});
    }
    REQUIRE(server.bind_tags(store, specs));

    auto level = index.find_by_name("Tank1.Level");
    auto status = index.find_by_name("Tank1.Status");
    REQUIRE(level);
    REQUIRE(status);

    store.publish(level->id,
                  opc::domain::TagValue{.value = 12.5f,
                                        .quality = opc::domain::Quality::Good,
                                        .reason = opc::domain::QualityReason::None,
                                        .source_ts = 1'700'000'000'000,
                                        .server_ts = 1'700'000'000'100});
    store.publish(status->id,
                  opc::domain::TagValue{.value = static_cast<std::uint16_t>(7),
                                        .quality = opc::domain::Quality::Good,
                                        .reason = opc::domain::QualityReason::None,
                                        .source_ts = 1'700'000'000'000,
                                        .server_ts = 1'700'000'000'100});

    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    UA_Client* client = UA_Client_new();
    REQUIRE(client != nullptr);
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    const auto endpoint = server.endpoint_url();
    auto connect = UA_Client_connect(client, endpoint.c_str());
    REQUIRE(connect == UA_STATUSCODE_GOOD);
    CHECK(metrics.gauge("ua_sessions") >= 1.0);
    CHECK(metrics.gauge("tag_quality.good") == 2.0);
    CHECK(metrics.gauge("tag_quality") == Catch::Approx(1.0));

    // Walk Objects → Plant → Tank1 → Level
    auto find_child = [&](UA_NodeId parent, const char* name) -> UA_NodeId {
        UA_BrowseRequest request;
        UA_BrowseRequest_init(&request);
        request.requestedMaxReferencesPerNode = 0;
        request.nodesToBrowse = UA_BrowseDescription_new();
        request.nodesToBrowseSize = 1;
        request.nodesToBrowse[0].nodeId = parent;
        request.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
        request.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
        request.nodesToBrowse[0].referenceTypeId =
            UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
        request.nodesToBrowse[0].includeSubtypes = true;

        UA_BrowseResponse response = UA_Client_Service_browse(client, request);
        UA_NodeId found;
        UA_NodeId_init(&found);
        if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD &&
            response.resultsSize == 1) {
            for (size_t i = 0; i < response.results[0].referencesSize; ++i) {
                const auto& ref = response.results[0].references[i];
                if (ref.browseName.name.length == std::strlen(name) &&
                    std::strncmp(reinterpret_cast<const char*>(ref.browseName.name.data), name,
                                 ref.browseName.name.length) == 0) {
                    UA_NodeId_copy(&ref.nodeId.nodeId, &found);
                    break;
                }
            }
        }
        UA_BrowseRequest_clear(&request);
        UA_BrowseResponse_clear(&response);
        return found;
    };

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId plant = find_child(objects, "Plant");
    REQUIRE(plant.identifierType == UA_NODEIDTYPE_NUMERIC);
    UA_NodeId tank = find_child(plant, "Tank1");
    REQUIRE(tank.identifierType == UA_NODEIDTYPE_NUMERIC);
    UA_NodeId level_node = find_child(tank, "Level");
    REQUIRE(level_node.identifierType == UA_NODEIDTYPE_NUMERIC);

    UA_Variant value;
    UA_Variant_init(&value);
    auto read_status = UA_Client_readValueAttribute(client, level_node, &value);
    REQUIRE(read_status == UA_STATUSCODE_GOOD);
    REQUIRE(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT]));
    CHECK(*static_cast<UA_Float*>(value.data) == Catch::Approx(12.5f));
    UA_Variant_clear(&value);

    UA_NodeId_clear(&plant);
    UA_NodeId_clear(&tank);
    UA_NodeId_clear(&level_node);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    server.stop();
}

TEST_CASE("ServerRuntime with OPC UA publishes polled values", "[opcua][runtime]") {
    const auto port = free_tcp_port();
    auto project = ua_sample_project(port);
    ManualClock clock{1000};
    NullMetrics metrics;
    NullLog log;

    auto fake = std::make_shared<FakeModbusTransport>();
    auto level_tag = project->devices[0].tags[0];
    auto encoded = Translator::encode(level_tag, 21.5f);
    REQUIRE(encoded);
    REQUIRE(fake->connect({.host = "127.0.0.1", .port = 1502}));
    fake->set_holding(0, (*encoded)[0]);
    fake->set_holding(1, (*encoded)[1]);
    fake->set_holding(2, 42);

    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [fake](const opc::project::Endpoint&) -> std::unique_ptr<opc::ports::IModbusTransport> {
                auto t = std::make_unique<FakeModbusTransport>();
                (void)t->connect({.host = "127.0.0.1", .port = 1502});
                auto regs = fake->read_holding_registers(1, 0, 3);
                if (regs) {
                    t->set_holding(0, (*regs)[0]);
                    t->set_holding(1, (*regs)[1]);
                    t->set_holding(2, (*regs)[2]);
                }
                return t;
            },
        .opcua = std::make_unique<OpcUaServer>(&log),
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());
    REQUIRE((*runtime)->poll_once(clock.now_ms()));

    auto* ua = dynamic_cast<OpcUaServer*>((*runtime)->opcua());
    REQUIRE(ua != nullptr);

    UA_Client* client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    REQUIRE(UA_Client_connect(client, ua->endpoint_url().c_str()) == UA_STATUSCODE_GOOD);

    auto find_child = [&](UA_NodeId parent, const char* name) -> UA_NodeId {
        UA_BrowseRequest request;
        UA_BrowseRequest_init(&request);
        request.requestedMaxReferencesPerNode = 0;
        request.nodesToBrowse = UA_BrowseDescription_new();
        request.nodesToBrowseSize = 1;
        request.nodesToBrowse[0].nodeId = parent;
        request.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
        request.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
        request.nodesToBrowse[0].referenceTypeId =
            UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
        request.nodesToBrowse[0].includeSubtypes = true;
        UA_BrowseResponse response = UA_Client_Service_browse(client, request);
        UA_NodeId found;
        UA_NodeId_init(&found);
        if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD &&
            response.resultsSize == 1) {
            for (size_t i = 0; i < response.results[0].referencesSize; ++i) {
                const auto& ref = response.results[0].references[i];
                if (ref.browseName.name.length == std::strlen(name) &&
                    std::strncmp(reinterpret_cast<const char*>(ref.browseName.name.data), name,
                                 ref.browseName.name.length) == 0) {
                    UA_NodeId_copy(&ref.nodeId.nodeId, &found);
                    break;
                }
            }
        }
        UA_BrowseRequest_clear(&request);
        UA_BrowseResponse_clear(&response);
        return found;
    };

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId plant = find_child(objects, "Plant");
    UA_NodeId tank = find_child(plant, "Tank1");
    UA_NodeId level_node = find_child(tank, "Level");
    REQUIRE(level_node.identifierType == UA_NODEIDTYPE_NUMERIC);

    UA_Variant value;
    UA_Variant_init(&value);
    REQUIRE(UA_Client_readValueAttribute(client, level_node, &value) == UA_STATUSCODE_GOOD);
    REQUIRE(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT]));
    CHECK(*static_cast<UA_Float*>(value.data) == Catch::Approx(21.5f));
    UA_Variant_clear(&value);

    UA_NodeId_clear(&plant);
    UA_NodeId_clear(&tank);
    UA_NodeId_clear(&level_node);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    (*runtime)->stop();
}
