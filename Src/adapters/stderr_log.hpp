#pragma once

#include "ports/i_log.hpp"

#include <iostream>
#include <mutex>

namespace opc::adapters {

class StderrLog final : public ports::ILog {
public:
    explicit StderrLog(ports::LogLevel min_level = ports::LogLevel::Info)
        : min_level_(min_level) {}

    void log(ports::LogLevel level,
             std::string_view component,
             std::string_view message) override {
        if (static_cast<int>(level) < static_cast<int>(min_level_)) {
            return;
        }
        std::lock_guard lock(mutex_);
        std::cerr << level_name(level) << " [" << component << "] " << message << '\n';
    }

private:
    static const char* level_name(ports::LogLevel level) {
        switch (level) {
        case ports::LogLevel::Trace:
            return "TRACE";
        case ports::LogLevel::Debug:
            return "DEBUG";
        case ports::LogLevel::Info:
            return "INFO";
        case ports::LogLevel::Warn:
            return "WARN";
        case ports::LogLevel::Error:
            return "ERROR";
        }
        return "LOG";
    }

    ports::LogLevel min_level_;
    std::mutex mutex_;
};

}  // namespace opc::adapters
