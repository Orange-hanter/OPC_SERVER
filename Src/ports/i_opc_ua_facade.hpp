#pragma once

#include "domain/types.hpp"
#include "project/types.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace opc::ports {

class ITagStore;

/// Spec for binding a TagStore id to a UA variable (no core::RuntimeIndex).
struct OpcUaTagSpec {
    domain::TagId id{0};
    project::Tag tag;
};

using OpcUaWriteHandler =
    std::function<domain::Result<void>(domain::TagId id, domain::ScalarValue value)>;

/// Northbound facade. Adapter owns open62541 details.
class IOpcUaFacade {
public:
    virtual ~IOpcUaFacade() = default;

    virtual domain::Result<void> start(std::shared_ptr<const project::Project> project) = 0;
    virtual void stop() = 0;

    /// Bind tag ids already present in ITagStore to UA variables.
    virtual domain::Result<void> bind_tag(domain::TagId id, std::string_view node_path) = 0;

    /// Bind all specs, subscribe to store updates, start async pump when needed.
    virtual domain::Result<void> bind_tags(ITagStore& store, std::span<const OpcUaTagSpec> tags) = 0;

    /// Southbound write sink (Dispatcher::enqueue_write). Must be set before client writes.
    virtual void set_write_handler(OpcUaWriteHandler handler) = 0;

    /// Optional nudge when no background pump is running.
    virtual void iterate() = 0;
};

}  // namespace opc::ports
