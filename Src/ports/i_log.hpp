#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace opc::ports {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

class ILog {
public:
    virtual ~ILog() = default;

    virtual void log(LogLevel level,
                     std::string_view component,
                     std::string_view message) = 0;

    void info(std::string_view component, std::string_view message) {
        log(LogLevel::Info, component, message);
    }
    void warn(std::string_view component, std::string_view message) {
        log(LogLevel::Warn, component, message);
    }
    void error(std::string_view component, std::string_view message) {
        log(LogLevel::Error, component, message);
    }
    void debug(std::string_view component, std::string_view message) {
        log(LogLevel::Debug, component, message);
    }
};

class NullLog final : public ILog {
public:
    void log(LogLevel, std::string_view, std::string_view) override {}
};

}  // namespace opc::ports
