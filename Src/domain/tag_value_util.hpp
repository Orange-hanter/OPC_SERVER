#pragma once

#include "domain/types.hpp"

#include <optional>

namespace opc::domain {

/// Flip quality/reason while keeping the last engineering value (ADR-0006).
[[nodiscard]] inline TagValue with_quality(std::optional<TagValue> previous,
                                           Quality quality,
                                           QualityReason reason,
                                           TimestampMs server_ts) {
    TagValue out = previous.value_or(TagValue{});
    out.quality = quality;
    out.reason = reason;
    out.server_ts = server_ts;
    if (out.source_ts == 0 && previous.has_value()) {
        out.source_ts = previous->source_ts;
    }
    return out;
}

}  // namespace opc::domain
