#pragma once

#include "domain/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opc::ports {

struct FrameRecord {
    domain::TimestampMs ts_ms{0};
    std::string endpoint_id;
    std::vector<std::uint8_t> tx;
    std::vector<std::uint8_t> rx;
    std::optional<int> exception_code;
    double rtt_ms{0.0};
    std::optional<std::string> error;
};

/// PCAP-like Modbus frame journal (ADR-0008 / doc 06).
class IFrameLog {
public:
    virtual ~IFrameLog() = default;
    virtual void log_frame(const FrameRecord& frame) = 0;
};

class NullFrameLog final : public IFrameLog {
public:
    void log_frame(const FrameRecord&) override {}
};

}  // namespace opc::ports
