#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/modbus_tcp_transport.hpp"
#include "adapters/opc_ua_server.hpp"
#include "app/server_runtime.hpp"
#include "core/translator.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"
#include "support/free_tcp_port.hpp"
#include "support/loopback_modbus_slave.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

using opc::adapters::ManualClock;
using opc::adapters::OpcUaServer;
using opc::app::ServerRuntime;
using opc::app::ServerRuntimeDeps;
using opc::core::Translator;
using opc::ports::NullLog;
using opc::ports::NullMetrics;

namespace {

UA_NodeId find_child(UA_Client* client, UA_NodeId parent, const char* name) {
    UA_BrowseRequest request;
    UA_BrowseRequest_init(&request);
    request.requestedMaxReferencesPerNode = 0;
    request.nodesToBrowse = UA_BrowseDescription_new();
    request.nodesToBrowseSize = 1;
    request.nodesToBrowse[0].nodeId = parent;
    request.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
    request.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    request.nodesToBrowse[0].referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    request.nodesToBrowse[0].includeSubtypes = true;
    UA_BrowseResponse response = UA_Client_Service_browse(client, request);
    UA_NodeId found;
    UA_NodeId_init(&found);
    if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD && response.resultsSize == 1) {
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
}

std::shared_ptr<const opc::project::Project> e2e_project(std::uint16_t ua_port,
                                                         std::uint16_t mb_port) {
    const std::string json = R"({
      "schemaVersion": 1,
      "name": "e2e-mvp",
      "opcua": {
        "endpointUrl": "opc.tcp://127.0.0.1:)" +
                             std::to_string(ua_port) + R"(",
        "applicationName": "OPC_SERVER E2E",
        "securityPolicy": "None",
        "securityMode": "None",
        "namespaceUri": "urn:opc-server:e2e"
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": )" +
                             std::to_string(mb_port) + R"(, "transport": "tcp",
         "responseTimeoutMs": 400}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Tank1.Level", "nodePath": "Plant/Tank1/Level", "area": "holding",
           "address": 0, "type": "float32", "byteOrder": "ABCD", "group": "g1"},
          {"name": "Tank1.Setpoint", "nodePath": "Plant/Tank1/Setpoint", "area": "holding",
           "address": 2, "type": "uint16", "byteOrder": "AB", "writable": true, "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1",
         "tagNames": ["Tank1.Level", "Tank1.Setpoint"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(json, "e2e.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("E2E MVP: Modbus TCP values visible in UA; write; disconnect quality",
          "[e2e][lab][mvp]") {
    if (std::getenv("OPC_E2E") == nullptr) {
        SKIP("set OPC_E2E=1 to run lab end-to-end tests");
    }

    LoopbackModbusSlave slave;
    opc::project::Tag level_tag;
    level_tag.type = opc::project::TagType::Float32;
    level_tag.byte_order = "ABCD";
    auto encoded = Translator::encode(level_tag, 12.5f);
    REQUIRE(encoded);
    slave.set_holding(1, 0, (*encoded)[0]);
    slave.set_holding(1, 1, (*encoded)[1]);
    slave.set_holding(1, 2, 7);

    const auto ua_port = opc_free_tcp_port();
    auto project = e2e_project(ua_port, slave.port());
    ManualClock clock{1'000};
    NullMetrics metrics;
    NullLog log;

    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory = {},
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

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId plant = find_child(client, objects, "Plant");
    UA_NodeId tank = find_child(client, plant, "Tank1");
    UA_NodeId level_node = find_child(client, tank, "Level");
    UA_NodeId setpoint = find_child(client, tank, "Setpoint");
    REQUIRE(level_node.identifierType == UA_NODEIDTYPE_NUMERIC);

    UA_Variant value;
    UA_Variant_init(&value);
    REQUIRE(UA_Client_readValueAttribute(client, level_node, &value) == UA_STATUSCODE_GOOD);
    REQUIRE(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT]));
    CHECK(*static_cast<UA_Float*>(value.data) == Catch::Approx(12.5f));
    UA_Variant_clear(&value);

    UA_UInt16 sp = 99;
    UA_Variant_init(&value);
    UA_Variant_setScalarCopy(&value, &sp, &UA_TYPES[UA_TYPES_UINT16]);
    REQUIRE(UA_Client_writeValueAttribute(client, setpoint, &value) == UA_STATUSCODE_GOOD);
    UA_Variant_clear(&value);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    clock.advance_ms(100);
    REQUIRE((*runtime)->poll_once(clock.now_ms()));
    CHECK(slave.holding(1, 2) == 99);

    auto binding = (*runtime)->index().find_by_name("Tank1.Setpoint");
    REQUIRE(binding);
    auto stored = (*runtime)->tag_store().get(binding->id);
    REQUIRE(stored);
    CHECK(stored->quality == opc::domain::Quality::Good);

    slave.stop();
    clock.advance_ms(200);
    (void)(*runtime)->poll_once(clock.now_ms());
    auto level_binding = (*runtime)->index().find_by_name("Tank1.Level");
    REQUIRE(level_binding);
    auto after = (*runtime)->tag_store().get(level_binding->id);
    REQUIRE(after);
    CHECK(after->quality == opc::domain::Quality::Bad);

    UA_NodeId_clear(&plant);
    UA_NodeId_clear(&tank);
    UA_NodeId_clear(&level_node);
    UA_NodeId_clear(&setpoint);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    (*runtime)->stop();
}
