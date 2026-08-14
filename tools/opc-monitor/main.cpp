#include "monitor_client.hpp"

#include <json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr std::size_t kMaxLineBytes = std::size_t{1024} * 1024;
std::mutex output_mutex;

void write_event(nlohmann::json event) {
    std::lock_guard lock(output_mutex);
    std::cout << event.dump() << '\n' << std::flush;
}

void write_input_error(std::string message) {
    write_event({{"event", "error"}, {"message", std::move(message)}});
}

}  // namespace

int main() {
    std::mutex queue_mutex;
    std::condition_variable queue_ready;
    std::deque<nlohmann::json> commands;
    bool input_closed = false;

    std::thread worker([&] {
        opc::monitor::MonitorClient client{write_event};
        bool running = true;
        while (running) {
            std::deque<nlohmann::json> pending;
            bool done = false;
            {
                std::unique_lock lock(queue_mutex);
                queue_ready.wait_for(lock, std::chrono::milliseconds{20}, [&] {
                    return input_closed || !commands.empty();
                });
                pending.swap(commands);
                done = input_closed && pending.empty();
            }
            if (done) {
                break;
            }
            for (const auto& command : pending) {
                client.handle_command(command);
                if (command.is_object() && command.value("command", "") == "shutdown") {
                    running = false;
                    break;
                }
            }
            if (running) {
                client.iterate(std::chrono::milliseconds{0});
            }
        }
        client.shutdown();
    });

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line.size() > kMaxLineBytes) {
            write_input_error("input line exceeds 1 MiB");
            continue;
        }
        try {
            auto command = nlohmann::json::parse(line);
            const bool shutdown =
                command.is_object() && command.value("command", "") == "shutdown";
            {
                std::lock_guard lock(queue_mutex);
                commands.push_back(std::move(command));
            }
            queue_ready.notify_one();
            if (shutdown) {
                break;
            }
        } catch (const nlohmann::json::exception& error) {
            write_input_error(std::string("invalid JSON: ") + error.what());
        }
    }

    {
        std::lock_guard lock(queue_mutex);
        input_closed = true;
    }
    queue_ready.notify_one();
    worker.join();
    return EXIT_SUCCESS;
}
