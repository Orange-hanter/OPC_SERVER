#include "adapters/spdlog_log.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <vector>

namespace opc::adapters {
namespace {

spdlog::level::level_enum to_spd(ports::LogLevel level) {
    switch (level) {
    case ports::LogLevel::Trace:
        return spdlog::level::trace;
    case ports::LogLevel::Debug:
        return spdlog::level::debug;
    case ports::LogLevel::Info:
        return spdlog::level::info;
    case ports::LogLevel::Warn:
        return spdlog::level::warn;
    case ports::LogLevel::Error:
        return spdlog::level::err;
    }
    return spdlog::level::info;
}

}  // namespace

SpdlogLog::SpdlogLog(SpdlogLogOptions options) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    if (!options.log_file.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            options.log_file, 5 * 1024 * 1024, 3));
    }

    if (options.async) {
        spdlog::init_thread_pool(8192, 1);
        logger_ = std::make_shared<spdlog::async_logger>(
            "opc", sinks.begin(), sinks.end(), spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest);
    } else {
        logger_ = std::make_shared<spdlog::logger>("opc", sinks.begin(), sinks.end());
    }

    logger_->set_level(to_spd(options.min_level));
    logger_->set_pattern("%Y-%m-%dT%H:%M:%S.%e [%^%l%$] %v");
    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
}

SpdlogLog::~SpdlogLog() {
    if (logger_) {
        logger_->flush();
    }
    spdlog::drop("opc");
    spdlog::shutdown();
}

void SpdlogLog::log(ports::LogLevel level,
                    std::string_view component,
                    std::string_view message) {
    if (!logger_) {
        return;
    }
    logger_->log(to_spd(level), "component={} {}", component, message);
}

}  // namespace opc::adapters
