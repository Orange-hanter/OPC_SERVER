#include <catch2/catch_test_macros.hpp>

#include "adapters/opc_ua_server.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "monitor_client.hpp"
#include "ports/i_log.hpp"
#include "project/load.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::uint16_t monitor_free_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    const auto port = ntohs(address.sin_port);
    ::close(fd);
    return port;
}

std::shared_ptr<opc::project::Project> monitor_project(std::uint16_t port) {
    const auto json = std::string{R"({
      "schemaVersion": 1,
      "name": "monitor-test",
      "opcua": {
        "endpointUrl": "opc.tcp://127.0.0.1:)"} +
                      std::to_string(port) + R"(",
        "securityPolicy": "None",
        "securityMode": "None",
        "namespaceUri": "urn:opc-server:monitor-test"
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "GoodTag", "nodePath": "Plant/GoodTag", "area": "holding",
           "address": 0, "type": "uint16", "group": "g1"},
          {"name": "UncertainTag", "nodePath": "Plant/UncertainTag", "area": "holding",
           "address": 1, "type": "uint16", "group": "g1"},
          {"name": "BadTag", "nodePath": "Plant/BadTag", "area": "holding",
           "address": 2, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast",
         "deviceId": "d1", "tagNames": ["GoodTag", "UncertainTag", "BadTag"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(json, "monitor.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

std::string child_id(const std::vector<nlohmann::json>& events,
                     std::string_view request_id,
                     std::string_view browse_name) {
    for (const auto& event : events) {
        if (event.value("event", "") != "browseResult" ||
            event.value("requestId", "") != request_id) {
            continue;
        }
        for (const auto& child : event["children"]) {
            if (child.value("browseName", "") == browse_name) {
                return child.value("nodeId", "");
            }
        }
    }
    return {};
}

}  // namespace

TEST_CASE("opc-monitor validates read-only JSON commands", "[opc-monitor]") {
    std::vector<nlohmann::json> events;
    opc::monitor::MonitorClient client{
        [&](nlohmann::json event) { events.push_back(std::move(event)); }};

    client.handle_command({{"command", "write"}, {"requestId", "denied"}});
    REQUIRE(events.size() == 1);
    CHECK(events.back()["event"] == "error");
    CHECK(events.back()["requestId"] == "denied");

    client.handle_command({{"command", "browse"},
                           {"nodeId", "ns=0;i=85"},
                           {"requestId", "offline"}});
    REQUIRE(events.size() == 2);
    CHECK(events.back()["event"] == "error");
    CHECK(events.back()["requestId"] == "offline");
}

TEST_CASE("opc-monitor browses and subscribes to diagnostics", "[opc-monitor][opcua]") {
    const auto project = monitor_project(monitor_free_port());
    opc::ports::NullLog log;
    opc::adapters::OpcUaServer server{&log};
    REQUIRE(server.start(project));
    opc::core::RuntimeIndex index = opc::core::RuntimeIndex::build(project);
    opc::core::TagStore store;
    std::vector<opc::ports::OpcUaTagSpec> specs;
    for (const auto& b : index.tags()) {
        specs.push_back({.id = b.id, .tag = b.tag});
    }
    REQUIRE(server.bind_tags(store, specs));

    std::vector<nlohmann::json> events;
    opc::monitor::MonitorClient client{
        [&](nlohmann::json event) { events.push_back(std::move(event)); }};
    client.handle_command({{"command", "connect"}, {"endpoint", server.endpoint_url()}});
    REQUIRE(client.connected());

    client.handle_command({{"command", "browse"},
                           {"nodeId", "ns=0;i=85"},
                           {"requestId", "objects"}});
    const auto server_id = child_id(events, "objects", "OPC_SERVER");
    REQUIRE_FALSE(server_id.empty());

    client.handle_command(
        {{"command", "browse"}, {"nodeId", server_id}, {"requestId", "server"}});
    const auto diagnostics_id = child_id(events, "server", "Diagnostics");
    REQUIRE_FALSE(diagnostics_id.empty());

    client.handle_command({{"command", "browse"},
                           {"nodeId", diagnostics_id},
                           {"requestId", "diagnostics"}});
    const auto state_id = child_id(events, "diagnostics", "State");
    const auto good_count_id = child_id(events, "diagnostics", "GoodCount");
    const auto uncertain_count_id = child_id(events, "diagnostics", "UncertainCount");
    const auto bad_count_id = child_id(events, "diagnostics", "BadCount");
    const auto last_error_id = child_id(events, "diagnostics", "LastError");
    REQUIRE_FALSE(state_id.empty());
    REQUIRE_FALSE(good_count_id.empty());
    REQUIRE_FALSE(uncertain_count_id.empty());
    REQUIRE_FALSE(bad_count_id.empty());
    REQUIRE_FALSE(last_error_id.empty());

    const auto subscribe = [&](std::string id, const std::string& node_id) {
        client.handle_command({{"command", "subscribe"},
                               {"subscriptionId", std::move(id)},
                               {"nodeId", node_id},
                               {"samplingIntervalMs", 20}});
    };
    subscribe("state", state_id);
    subscribe("good-count", good_count_id);
    subscribe("uncertain-count", uncertain_count_id);
    subscribe("bad-count", bad_count_id);
    subscribe("last-error", last_error_id);

    const auto good_tag = index.find_by_name("GoodTag");
    const auto uncertain_tag = index.find_by_name("UncertainTag");
    const auto bad_tag = index.find_by_name("BadTag");
    REQUIRE(good_tag);
    REQUIRE(uncertain_tag);
    REQUIRE(bad_tag);
    store.publish(good_tag->id,
                  {.value = std::uint16_t{9},
                   .quality = opc::domain::Quality::Good,
                   .reason = opc::domain::QualityReason::None,
                   .server_ts = 100});
    store.publish(uncertain_tag->id,
                  {.value = std::uint16_t{8},
                   .quality = opc::domain::Quality::Uncertain,
                   .reason = opc::domain::QualityReason::Stale,
                   .server_ts = 100});
    store.publish(bad_tag->id,
                  {.value = std::uint16_t{7},
                   .quality = opc::domain::Quality::Bad,
                   .reason = opc::domain::QualityReason::Timeout,
                   .server_ts = 100});

    auto saw_value = [&](std::string_view id, const nlohmann::json& expected) {
        for (const auto& event : events) {
            if (event.value("event", "") == "dataChange" &&
                event.value("subscriptionId", "") == id && event["value"] == expected) {
                return true;
            }
        }
        return false;
    };
    bool saw_diagnostics = false;
    for (int attempt = 0; attempt < 50 && !saw_diagnostics; ++attempt) {
        client.iterate(std::chrono::milliseconds{20});
        saw_diagnostics = saw_value("state", "Running") &&
                          saw_value("good-count", 1) &&
                          saw_value("uncertain-count", 1) &&
                          saw_value("bad-count", 1) &&
                          saw_value("last-error",
                                    "Tag " + std::to_string(bad_tag->id) + ": Timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    CHECK(saw_diagnostics);

    client.handle_command({{"command", "unsubscribe"}, {"subscriptionId", "state"}});
    client.handle_command({{"command", "unsubscribe"}, {"subscriptionId", "good-count"}});
    client.handle_command(
        {{"command", "unsubscribe"}, {"subscriptionId", "uncertain-count"}});
    client.handle_command({{"command", "unsubscribe"}, {"subscriptionId", "bad-count"}});
    client.handle_command({{"command", "unsubscribe"}, {"subscriptionId", "last-error"}});
    client.handle_command({{"command", "disconnect"}});
    server.stop();
}
