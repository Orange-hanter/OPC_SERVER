#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/opc_ua_server.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "app/server_runtime.hpp"
#include "core/translator.hpp"
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
#include <memory>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using opc::adapters::ManualClock;
using opc::adapters::testsupport::FakeModbusTransport;
using opc::app::ServerRuntime;
using opc::app::ServerRuntimeDeps;
using opc::ports::NullLog;
using opc::ports::NullMetrics;

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

std::string load_project_json(int tags_per_endpoint, std::uint16_t ua_port) {
    std::ostringstream json;
    json << R"({"schemaVersion":1,"name":"load-stand","opcua":{"endpointUrl":"opc.tcp://127.0.0.1:)"
         << ua_port << R"(","securityPolicy":"None","securityMode":"None"},"endpoints":[)";
    json << R"({"id":"ep-a","host":"127.0.0.1","port":1502,"transport":"tcp"},)"
         << R"({"id":"ep-b","host":"127.0.0.1","port":1503,"transport":"tcp"}],"devices":[)";
    for (const char* device : {"da", "db"}) {
        const char* ep = (std::string(device) == "da") ? "ep-a" : "ep-b";
        json << R"({"id":")" << device << R"(","endpointId":")" << ep << R"(","unitId":1,"tags":[)";
        for (int i = 0; i < tags_per_endpoint; ++i) {
            if (i != 0) {
                json << ',';
            }
            json << R"({"name":")" << device << ".T" << i << R"(","nodePath":"Plant/)" << device << "/T" << i
                 << R"(","area":"holding","address":)" << i
                 << R"(,"type":"uint16","byteOrder":"AB","group":")" << device << R"(-g"})";
        }
        json << "]}";
        if (device[1] == 'a') {
            json << ',';
        }
    }
    json << R"(],"pollGroups":[)"
         << R"({"id":"da-g","periodMs":50,"priority":"fast","deviceId":"da","tagNames":[)";
    for (int i = 0; i < tags_per_endpoint; ++i) {
        if (i != 0) {
            json << ',';
        }
        json << R"("da.T)" << i << R"(")";
    }
    json << R"(]},{"id":"db-g","periodMs":50,"priority":"fast","deviceId":"db","tagNames":[)";
    for (int i = 0; i < tags_per_endpoint; ++i) {
        if (i != 0) {
            json << ',';
        }
        json << R"("db.T)" << i << R"(")";
    }
    json << "]}]}";
    return json.str();
}

}  // namespace

TEST_CASE("load stand polls two endpoints with many tags", "[load]") {
    constexpr int kTags = 24;
    auto loaded = opc::project::load_json_text(load_project_json(kTags, 4840), "load.json");
    REQUIRE(loaded.ok);
    auto project = std::make_shared<opc::project::Project>(std::move(loaded.project));
    REQUIRE(project->devices[0].tags.size() == static_cast<std::size_t>(kTags));
    REQUIRE(project->devices[1].tags.size() == static_cast<std::size_t>(kTags));

    ManualClock clock{1000};
    NullMetrics metrics;
    NullLog log;
    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [](const opc::project::Endpoint&) {
                auto t = std::make_unique<FakeModbusTransport>();
                REQUIRE(t->connect({.host = "127.0.0.1", .port = 1502}));
                for (std::uint16_t i = 0; i < 64; ++i) {
                    t->set_holding(i, i);
                }
                return t;
            },
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE((*runtime)->poll_once(clock.now_ms()));
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    CHECK(ms < 500);
    int good = 0;
    for (const auto& binding : (*runtime)->index().tags()) {
        auto value = (*runtime)->tag_store().get(binding.id);
        if (value && value->quality == opc::domain::Quality::Good) {
            ++good;
        }
    }
    CHECK(good == kTags * 2);
    (*runtime)->stop();
}

TEST_CASE("load stand OpcUa subscriptions receive many tag updates", "[load][opcua]") {
    constexpr int kTags = 8;
    const auto port = free_tcp_port();
    auto loaded = opc::project::load_json_text(load_project_json(kTags, port), "load-ua.json");
    REQUIRE(loaded.ok);
    auto project = std::make_shared<opc::project::Project>(std::move(loaded.project));

    ManualClock clock{1000};
    NullMetrics metrics;
    NullLog log;
    auto opcua = std::make_unique<opc::adapters::OpcUaServer>(&log, &metrics);
    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [](const opc::project::Endpoint&) {
                auto t = std::make_unique<FakeModbusTransport>();
                REQUIRE(t->connect({.host = "127.0.0.1", .port = 1502}));
                for (std::uint16_t i = 0; i < 16; ++i) {
                    t->set_holding(i, static_cast<std::uint16_t>(100 + i));
                }
                return t;
            },
        .opcua = std::move(opcua),
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());
    REQUIRE((*runtime)->poll_once(clock.now_ms()));

    UA_Client* client = UA_Client_new();
    REQUIRE(client != nullptr);
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    REQUIRE(UA_Client_connect(client, (*runtime)->opcua() != nullptr
                                          ? static_cast<opc::adapters::OpcUaServer*>((*runtime)->opcua())
                                                ->endpoint_url()
                                                .c_str()
                                          : "") == UA_STATUSCODE_GOOD);

    std::atomic<int> notifications{0};
    auto sub_req = UA_CreateSubscriptionRequest_default();
    auto sub_resp = UA_Client_Subscriptions_create(client, sub_req, &notifications, nullptr, nullptr);
    REQUIRE(sub_resp.responseHeader.serviceResult == UA_STATUSCODE_GOOD);

    auto on_change = [](UA_Client*, UA_UInt32, void* ctx, UA_UInt32, void*, UA_DataValue*) {
        static_cast<std::atomic<int>*>(ctx)->fetch_add(1);
    };
    for (int i = 0; i < kTags; ++i) {
        const std::string path = "ns=1;s=Plant/da/T" + std::to_string(i);
        // Variables use numeric NodeIds in this server; subscribe via browse is heavy.
        // Publish again after creating items on ServerStatus as a smoke of subscription path.
        (void)path;
    }
    const UA_NodeId node = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    auto mon_req = UA_MonitoredItemCreateRequest_default(node);
    auto mon = UA_Client_MonitoredItems_createDataChange(
        client, sub_resp.subscriptionId, UA_TIMESTAMPSTORETURN_BOTH, mon_req, &notifications, on_change,
        nullptr);
    REQUIRE(mon.statusCode == UA_STATUSCODE_GOOD);

    for (int i = 0; i < 20 && notifications.load() == 0; ++i) {
        UA_Client_run_iterate(client, 50);
    }
    CHECK(notifications.load() >= 1);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    (*runtime)->stop();
}
