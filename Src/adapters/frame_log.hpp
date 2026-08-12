#pragma once

#include "ports/i_frame_log.hpp"

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace opc::adapters {

/// Text frame journal: one line per transaction (hex TX/RX + RTT + endpoint).
class FileFrameLog final : public ports::IFrameLog {
public:
    explicit FileFrameLog(std::string path);
    ~FileFrameLog() override;

    [[nodiscard]] bool is_open() const { return out_.is_open(); }
    void log_frame(const ports::FrameRecord& frame) override;

private:
    std::string path_;
    std::ofstream out_;
    std::mutex mutex_;
};

/// In-memory frame log for tests and short debug sessions.
class MemoryFrameLog final : public ports::IFrameLog {
public:
    void log_frame(const ports::FrameRecord& frame) override;
    [[nodiscard]] std::vector<ports::FrameRecord> frames() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::vector<ports::FrameRecord> frames_;
};

}  // namespace opc::adapters
