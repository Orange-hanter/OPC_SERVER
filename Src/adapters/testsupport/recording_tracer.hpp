#pragma once

#include "ports/i_tracer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace opc::adapters::testsupport {

struct RecordedSpan {
    std::string name;
    std::unordered_map<std::string, std::string> attributes;
    bool error{false};
    std::string error_message;
};

/// In-process tracer for Dispatcher tests (no OpenTelemetry).
class RecordingTracer final : public ports::ITracer {
public:
    std::unique_ptr<ports::ISpan> start_span(std::string_view name) override;

    [[nodiscard]] std::vector<RecordedSpan> snapshot() const {
        std::lock_guard lock(mutex_);
        return finished_;
    }

private:
    class Span final : public ports::ISpan {
    public:
        Span(RecordingTracer* owner, std::string name) : owner_(owner) {
            current_.name = std::move(name);
        }
        ~Span() override {
            if (owner_ != nullptr) {
                owner_->finish(std::move(current_));
            }
        }
        void set_attribute(std::string_view key, std::string_view value) override {
            current_.attributes[std::string(key)] = std::string(value);
        }
        void set_attribute(std::string_view key, std::int64_t value) override {
            current_.attributes[std::string(key)] = std::to_string(value);
        }
        void set_error(std::string_view message) override {
            current_.error = true;
            current_.error_message = std::string(message);
        }

    private:
        RecordingTracer* owner_{nullptr};
        RecordedSpan current_;
    };

    void finish(RecordedSpan span) {
        std::lock_guard lock(mutex_);
        finished_.push_back(std::move(span));
    }

    mutable std::mutex mutex_;
    std::vector<RecordedSpan> finished_;
};

inline std::unique_ptr<ports::ISpan> RecordingTracer::start_span(std::string_view name) {
    return std::make_unique<Span>(this, std::string(name));
}

}  // namespace opc::adapters::testsupport
