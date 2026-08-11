#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace opc::domain {

enum class Quality : std::uint8_t {
    Good = 0,
    Uncertain = 1,
    Bad = 2,
};

enum class QualityReason : std::uint16_t {
    None = 0,
    NoCommunication,
    DeviceFailure,
    Timeout,
    ModbusException,
    DecodingError,
    Stale,
    WritePending,
    WriteRejected,
    OutOfRange,
};

using TagId = std::uint32_t;
using TimestampMs = std::int64_t;  // unix epoch milliseconds (UTC)

using ScalarValue = std::variant<std::monostate,
                                 bool,
                                 std::uint16_t,
                                 std::int16_t,
                                 std::uint32_t,
                                 std::int32_t,
                                 float,
                                 double>;

struct TagValue {
    ScalarValue value{};
    Quality quality{Quality::Bad};
    QualityReason reason{QualityReason::NoCommunication};
    TimestampMs source_ts{0};
    TimestampMs server_ts{0};
    std::uint64_t epoch{0};
};

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    Timeout,
    Connection,
    ModbusException,
    Decoding,
    QueueFull,
    Permission,
    Internal,
    NotImplemented,
};

struct Error {
    ErrorCode code{ErrorCode::Internal};
    std::string message;
    std::string component;
    bool retryable{false};
    std::optional<int> protocol_status;  // e.g. Modbus exception code
};

template <typename T>
using Result = std::expected<T, Error>;

}  // namespace opc::domain
