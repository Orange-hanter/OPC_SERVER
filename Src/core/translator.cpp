#include "core/translator.hpp"

namespace opc::core {

domain::Result<domain::ScalarValue>
Translator::decode(const project::Tag&, std::span<const std::uint16_t>) {
    return std::unexpected(domain::Error{
        domain::ErrorCode::NotImplemented,
        "Translator::decode not implemented yet (stage 2)",
        "core.translator",
        false,
    });
}

domain::Result<std::vector<std::uint16_t>>
Translator::encode(const project::Tag&, const domain::ScalarValue&) {
    return std::unexpected(domain::Error{
        domain::ErrorCode::NotImplemented,
        "Translator::encode not implemented yet (stage 2)",
        "core.translator",
        false,
    });
}

}  // namespace opc::core
