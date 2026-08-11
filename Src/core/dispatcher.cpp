#include "core/dispatcher.hpp"

namespace opc::core {

Dispatcher::Dispatcher(Dependencies deps) : deps_(std::move(deps)) {}

void Dispatcher::bind_transport(std::string, ports::IModbusTransport*) {
    // Stage 2: register endpoint worker.
}

domain::Result<void> Dispatcher::poll_due(std::string_view, domain::TimestampMs) {
    return std::unexpected(domain::Error{
        domain::ErrorCode::NotImplemented,
        "Dispatcher::poll_due not implemented yet (stage 2)",
        "core.dispatcher",
        false,
    });
}

domain::Result<void> Dispatcher::enqueue_write(domain::TagId, domain::ScalarValue) {
    return std::unexpected(domain::Error{
        domain::ErrorCode::NotImplemented,
        "Dispatcher::enqueue_write not implemented yet (stage 2)",
        "core.dispatcher",
        false,
    });
}

}  // namespace opc::core
