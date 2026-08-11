#pragma once

#include "domain/types.hpp"

namespace opc::ports {

class IHistorian {
public:
    virtual ~IHistorian() = default;
    virtual void record(domain::TagId id, const domain::TagValue& value) = 0;
    virtual domain::Result<void> flush() = 0;
};

class NullHistorian final : public IHistorian {
public:
    void record(domain::TagId, const domain::TagValue&) override {}
    domain::Result<void> flush() override { return {}; }
};

}  // namespace opc::ports
