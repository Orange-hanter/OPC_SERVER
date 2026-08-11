#pragma once

#include "domain/types.hpp"

#include <memory>
#include <string_view>

namespace opc::project {
struct Project;
}

namespace opc::ports {

/// Northbound facade. Adapter owns open62541 details.
class IOpcUaFacade {
public:
    virtual ~IOpcUaFacade() = default;

    virtual domain::Result<void> start(std::shared_ptr<const project::Project> project) = 0;
    virtual void stop() = 0;

    /// Bind tag ids already present in ITagStore to UA variables.
    virtual domain::Result<void> bind_tag(domain::TagId id, std::string_view node_path) = 0;

    /// Apply pending store updates and pump the UA event loop (main thread).
    virtual void iterate() = 0;
};

}  // namespace opc::ports
