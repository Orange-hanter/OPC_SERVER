#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/opc_ua_server.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "core/dispatcher.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using opc::adapters::ManualClock;
using opc::adapters::OpcUaServer;
using opc::adapters::testsupport::FakeModbusTransport;
using opc::ports::NullLog;
using opc::ports::NullMetrics;

namespace {

std::uint16_t free_tcp_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return static_cast<std::uint16_t>(21000 + (::getpid() % 20000));
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return static_cast<std::uint16_t>(21000 + (::getpid() % 20000));
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(fd);
        return static_cast<std::uint16_t>(21000 + (::getpid() % 20000));
    }
    const auto port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

std::shared_ptr<opc::project::Project> stage4_project(std::uint16_t port) {
    const std::string json = R"({
      "schemaVersion": 1,
      "name": "ua-stage4",
      "opcua": {
        "endpointUrl": "opc.tcp://127.0.0.1:)" +
                             std::to_string(port) + R"(",
        "applicationName": "OPC_SERVER Stage4",
        "securityPolicy": "None",
        "securityMode": "None",
        "namespaceUri": "urn:opc-server:ua-stage4"
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Tank1.Level", "nodePath": "Plant/Tank1/Level", "area": "holding",
           "address": 0, "type": "float32", "byteOrder": "ABCD", "group": "g1"},
          {"name": "Tank1.Setpoint", "nodePath": "Plant/Tank1/Setpoint", "area": "holding",
           "address": 2, "type": "uint16", "writable": true, "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1",
         "tagNames": ["Tank1.Level", "Tank1.Setpoint"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(json, "stage4.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

UA_NodeId find_child(UA_Client* client, UA_NodeId parent, const char* name) {
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

struct SubCapture {
    std::atomic<int> notifications{0};
    std::atomic<float> last_float{0.f};
};

void on_data_change(UA_Client* /*client*/,
                    UA_UInt32 /*sub_id*/,
                    void* /*sub_context*/,
                    UA_UInt32 /*mon_id*/,
                    void* mon_context,
                    UA_DataValue* value) {
    auto* capture = static_cast<SubCapture*>(mon_context);
    if (capture == nullptr || value == nullptr || !value->hasValue) {
        return;
    }
    if (UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_FLOAT])) {
        capture->last_float = *static_cast<UA_Float*>(value->value.data);
        capture->notifications.fetch_add(1);
    }
}

}  // namespace

TEST_CASE("UA Write enqueues Dispatcher and reaches Modbus", "[opcua][write]") {
    const auto port = free_tcp_port();
    auto project = stage4_project(port);
    ManualClock clock{1000};
    NullMetrics metrics;
    NullLog log;

    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 1502}));
    transport.set_holding(2, 7);

    auto index = opc::core::RuntimeIndex::build(project);
    opc::core::TagStore store;
    opc::core::Dispatcher dispatcher(opc::core::Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
    });
    dispatcher.bind_transport("ep1", &transport);

    OpcUaServer server{&log};
    server.set_write_handler([&](opc::domain::TagId id, opc::domain::ScalarValue value) {
        return dispatcher.enqueue_write(id, std::move(value));
    });
    REQUIRE(server.start(project));
    std::vector<opc::ports::OpcUaTagSpec> specs;
    for (const auto& b : index.tags()) {
        specs.push_back({.id = b.id, .tag = b.tag});
    }
    REQUIRE(server.bind_tags(store, specs));

    UA_Client* client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    REQUIRE(UA_Client_connect(client, server.endpoint_url().c_str()) == UA_STATUSCODE_GOOD);

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId plant = find_child(client, objects, "Plant");
    UA_NodeId tank = find_child(client, plant, "Tank1");
    UA_NodeId setpoint = find_child(client, tank, "Setpoint");
    REQUIRE(setpoint.identifierType == UA_NODEIDTYPE_NUMERIC);

    UA_Variant write_value;
    UA_Variant_init(&write_value);
    UA_UInt16 sp = 99;
    UA_Variant_setScalarCopy(&write_value, &sp, &UA_TYPES[UA_TYPES_UINT16]);
    REQUIRE(UA_Client_writeValueAttribute(client, setpoint, &write_value) == UA_STATUSCODE_GOOD);
    UA_Variant_clear(&write_value);

    // Allow UA callback → enqueue, then drain writes_first.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(dispatcher.poll_due("ep1", clock.now_ms()));

    auto regs = transport.read_holding_registers(1, 2, 1);
    REQUIRE(regs);
    CHECK((*regs)[0] == 99);

    auto binding = index.find_by_name("Tank1.Setpoint");
    REQUIRE(binding);
    auto stored = store.get(binding->id);
    REQUIRE(stored);
    CHECK(stored->quality == opc::domain::Quality::Good);
    CHECK(std::get<std::uint16_t>(stored->value) == 99);

    UA_NodeId_clear(&plant);
    UA_NodeId_clear(&tank);
    UA_NodeId_clear(&setpoint);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    server.stop();
}

TEST_CASE("UA Subscription notifies on TagStore publish", "[opcua][subscription]") {
    const auto port = free_tcp_port();
    auto project = stage4_project(port);
    NullLog log;
    OpcUaServer server{&log};
    REQUIRE(server.start(project));

    auto index = opc::core::RuntimeIndex::build(project);
    opc::core::TagStore store;
    std::vector<opc::ports::OpcUaTagSpec> specs;
    for (const auto& b : index.tags()) {
        specs.push_back({.id = b.id, .tag = b.tag});
    }
    REQUIRE(server.bind_tags(store, specs));

    auto level = index.find_by_name("Tank1.Level");
    REQUIRE(level);
    store.publish(level->id,
                  opc::domain::TagValue{.value = 1.0f,
                                        .quality = opc::domain::Quality::Good,
                                        .reason = opc::domain::QualityReason::None,
                                        .source_ts = 1000,
                                        .server_ts = 1000});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    UA_Client* client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    REQUIRE(UA_Client_connect(client, server.endpoint_url().c_str()) == UA_STATUSCODE_GOOD);

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId plant = find_child(client, objects, "Plant");
    UA_NodeId tank = find_child(client, plant, "Tank1");
    UA_NodeId level_node = find_child(client, tank, "Level");
    REQUIRE(level_node.identifierType == UA_NODEIDTYPE_NUMERIC);

    UA_CreateSubscriptionRequest sub_req = UA_CreateSubscriptionRequest_default();
    sub_req.requestedPublishingInterval = 100.0;
    UA_CreateSubscriptionResponse sub_res =
        UA_Client_Subscriptions_create(client, sub_req, nullptr, nullptr, nullptr);
    REQUIRE(sub_res.responseHeader.serviceResult == UA_STATUSCODE_GOOD);

    SubCapture capture;
    UA_MonitoredItemCreateRequest mon_req = UA_MonitoredItemCreateRequest_default(level_node);
    mon_req.requestedParameters.samplingInterval = 100.0;
    UA_MonitoredItemCreateResult mon_res = UA_Client_MonitoredItems_createDataChange(
        client, sub_res.subscriptionId, UA_TIMESTAMPSTORETURN_BOTH, mon_req, &capture,
        on_data_change, nullptr);
    REQUIRE(mon_res.statusCode == UA_STATUSCODE_GOOD);

    // Initial publish + wait for notification.
    for (int i = 0; i < 30 && capture.notifications.load() == 0; ++i) {
        UA_Client_run_iterate(client, 50);
    }
    REQUIRE(capture.notifications.load() >= 1);
    CHECK(capture.last_float.load() == Catch::Approx(1.0f));

    const int before = capture.notifications.load();
    store.publish(level->id,
                  opc::domain::TagValue{.value = 42.5f,
                                        .quality = opc::domain::Quality::Good,
                                        .reason = opc::domain::QualityReason::None,
                                        .source_ts = 2000,
                                        .server_ts = 2000});

    for (int i = 0; i < 40 && capture.notifications.load() <= before; ++i) {
        UA_Client_run_iterate(client, 50);
    }
    REQUIRE(capture.notifications.load() > before);
    CHECK(capture.last_float.load() == Catch::Approx(42.5f));

    UA_Client_Subscriptions_deleteSingle(client, sub_res.subscriptionId);
    UA_NodeId_clear(&plant);
    UA_NodeId_clear(&tank);
    UA_NodeId_clear(&level_node);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    server.stop();
}

TEST_CASE("UA write to non-writable tag returns BadNotWritable", "[opcua][write][hardening]") {
    const auto port = free_tcp_port();
    auto project = stage4_project(port);
    NullLog log;
    OpcUaServer server{&log};
    REQUIRE(server.start(project));
    auto index = opc::core::RuntimeIndex::build(project);
    opc::core::TagStore store;
    std::vector<opc::ports::OpcUaTagSpec> specs;
    for (const auto& b : index.tags()) {
        specs.push_back({.id = b.id, .tag = b.tag});
    }
    server.set_write_handler([](opc::domain::TagId, opc::domain::ScalarValue) -> opc::domain::Result<void> {
        return {};
    });
    REQUIRE(server.bind_tags(store, specs));

    UA_Client* client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    REQUIRE(UA_Client_connect(client, server.endpoint_url().c_str()) == UA_STATUSCODE_GOOD);

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId plant = find_child(client, objects, "Plant");
    UA_NodeId tank = find_child(client, plant, "Tank1");
    UA_NodeId level = find_child(client, tank, "Level");
    REQUIRE(level.identifierType == UA_NODEIDTYPE_NUMERIC);

    UA_Variant write_value;
    UA_Variant_init(&write_value);
    UA_Float v = 1.5f;
    UA_Variant_setScalarCopy(&write_value, &v, &UA_TYPES[UA_TYPES_FLOAT]);
    const auto status = UA_Client_writeValueAttribute(client, level, &write_value);
    UA_Variant_clear(&write_value);
    CHECK(status == UA_STATUSCODE_BADNOTWRITABLE);

    UA_NodeId_clear(&plant);
    UA_NodeId_clear(&tank);
    UA_NodeId_clear(&level);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    server.stop();
}
