#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace opc::ports {

/// One tracing span. Destructor ends the span (ADR-0008).
class ISpan {
public:
    virtual ~ISpan() = default;

    virtual void set_attribute(std::string_view key, std::string_view value) = 0;
    virtual void set_attribute(std::string_view key, std::int64_t value) = 0;
    virtual void set_error(std::string_view message) = 0;
};

class ITracer {
public:
    virtual ~ITracer() = default;

    [[nodiscard]] virtual std::unique_ptr<ISpan> start_span(std::string_view name) = 0;
};

class NullSpan final : public ISpan {
public:
    void set_attribute(std::string_view, std::string_view) override {}
    void set_attribute(std::string_view, std::int64_t) override {}
    void set_error(std::string_view) override {}
};

class NullTracer final : public ITracer {
public:
    std::unique_ptr<ISpan> start_span(std::string_view) override {
        return std::make_unique<NullSpan>();
    }
};

}  // namespace opc::ports
