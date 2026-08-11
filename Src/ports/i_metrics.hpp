#pragma once

#include <cstdint>
#include <string_view>

namespace opc::ports {

class IMetrics {
public:
    virtual ~IMetrics() = default;

    virtual void counter_add(std::string_view name, double value = 1.0) = 0;
    virtual void gauge_set(std::string_view name, double value) = 0;
    virtual void histogram_observe(std::string_view name, double value) = 0;
};

/// No-op metrics for tests and early bring-up.
class NullMetrics final : public IMetrics {
public:
    void counter_add(std::string_view, double) override {}
    void gauge_set(std::string_view, double) override {}
    void histogram_observe(std::string_view, double) override {}
};

}  // namespace opc::ports
