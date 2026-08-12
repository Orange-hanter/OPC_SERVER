#include "adapters/frame_log.hpp"

#include <iomanip>
#include <sstream>

namespace opc::adapters {
namespace {

std::string to_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            oss << ' ';
        }
        oss << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return oss.str();
}

}  // namespace

FileFrameLog::FileFrameLog(std::string path) : path_(std::move(path)) {
    out_.open(path_, std::ios::out | std::ios::app);
    if (out_.is_open()) {
        out_ << "# ts_ms endpoint rtt_ms exception error tx_hex | rx_hex\n";
        out_.flush();
    }
}

FileFrameLog::~FileFrameLog() {
    std::lock_guard lock(mutex_);
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

void FileFrameLog::log_frame(const ports::FrameRecord& frame) {
    std::lock_guard lock(mutex_);
    if (!out_.is_open()) {
        return;
    }
    out_ << frame.ts_ms << ' ' << frame.endpoint_id << ' ' << frame.rtt_ms << ' ';
    if (frame.exception_code) {
        out_ << *frame.exception_code;
    } else {
        out_ << '-';
    }
    out_ << ' ';
    if (frame.error) {
        out_ << '"';
        for (char c : *frame.error) {
            out_ << (c == '"' || c == '\n' ? '_' : c);
        }
        out_ << '"';
    } else {
        out_ << '-';
    }
    out_ << ' ' << to_hex(frame.tx) << " | " << to_hex(frame.rx) << '\n';
}

void MemoryFrameLog::log_frame(const ports::FrameRecord& frame) {
    std::lock_guard lock(mutex_);
    frames_.push_back(frame);
}

std::vector<ports::FrameRecord> MemoryFrameLog::frames() const {
    std::lock_guard lock(mutex_);
    return frames_;
}

void MemoryFrameLog::clear() {
    std::lock_guard lock(mutex_);
    frames_.clear();
}

}  // namespace opc::adapters
