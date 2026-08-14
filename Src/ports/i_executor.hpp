#pragma once

#include <functional>

namespace opc::ports {

/// Lightweight executor for posting completions off I/O threads (ADR-0002 strand).
class IExecutor {
public:
    virtual ~IExecutor() = default;
    virtual void post(std::move_only_function<void()> work) = 0;
};

}  // namespace opc::ports
