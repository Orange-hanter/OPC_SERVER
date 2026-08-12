#pragma once

#include "ports/i_log.hpp"

#include <memory>
#include <string>

namespace spdlog {
class logger;
}

namespace opc::adapters {

struct SpdlogLogOptions {
    ports::LogLevel min_level{ports::LogLevel::Info};
    std::string log_file;  // empty = stderr/stdout only
    bool async{true};
};

/// Structured spdlog adapter for `ILog` (ADR-0008).
class SpdlogLog final : public ports::ILog {
public:
    explicit SpdlogLog(SpdlogLogOptions options = {});
    ~SpdlogLog() override;

    SpdlogLog(const SpdlogLog&) = delete;
    SpdlogLog& operator=(const SpdlogLog&) = delete;

    void log(ports::LogLevel level,
             std::string_view component,
             std::string_view message) override;

private:
    std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace opc::adapters
