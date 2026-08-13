#pragma once

#include <json.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace opc::monitor {

class MonitorClient final {
public:
    using EventSink = std::function<void(nlohmann::json)>;

    explicit MonitorClient(EventSink sink);
    ~MonitorClient();

    MonitorClient(const MonitorClient&) = delete;
    MonitorClient& operator=(const MonitorClient&) = delete;

    void handle_command(const nlohmann::json& command);
    void iterate(std::chrono::milliseconds timeout = std::chrono::milliseconds{20});
    void shutdown();

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace opc::monitor
