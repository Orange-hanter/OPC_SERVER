#pragma once

#include "domain/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace opc::ports {

struct HistorianSample {
    domain::TagId id{0};
    domain::TagValue value{};
};

class IHistorian {
public:
    virtual ~IHistorian() = default;
    virtual void record(domain::TagId id, const domain::TagValue& value) = 0;
    virtual domain::Result<void> flush() = 0;

    /// Newest-first snapshot of the hot ring (up to `max` samples).
    [[nodiscard]] virtual std::vector<HistorianSample> recent(std::size_t max) const = 0;
    [[nodiscard]] virtual std::uint64_t dropped() const = 0;
};

class NullHistorian final : public IHistorian {
public:
    void record(domain::TagId, const domain::TagValue&) override {}
    domain::Result<void> flush() override { return {}; }
    [[nodiscard]] std::vector<HistorianSample> recent(std::size_t) const override { return {}; }
    [[nodiscard]] std::uint64_t dropped() const override { return 0; }
};

}  // namespace opc::ports
