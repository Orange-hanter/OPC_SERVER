#include <catch2/catch_test_macros.hpp>

#include "adapters/frame_log.hpp"
#include "ports/i_frame_log.hpp"

#include <filesystem>

TEST_CASE("MemoryFrameLog stores frames", "[adapters][framelog]") {
    opc::adapters::MemoryFrameLog log;
    opc::ports::FrameRecord frame;
    frame.ts_ms = 42;
    frame.endpoint_id = "ep1";
    frame.tx = {0x00, 0x01, 0x00, 0x00};
    frame.rx = {0x00, 0x01};
    frame.rtt_ms = 1.5;
    log.log_frame(frame);
    REQUIRE(log.frames().size() == 1);
    CHECK(log.frames()[0].endpoint_id == "ep1");
    CHECK(log.frames()[0].tx.size() == 4);
}

TEST_CASE("FileFrameLog appends text lines", "[adapters][framelog]") {
    const auto path = (std::filesystem::temp_directory_path() / "opc_frame_log_test.txt").string();
    std::filesystem::remove(path);
    {
        opc::adapters::FileFrameLog log(path);
        REQUIRE(log.is_open());
        opc::ports::FrameRecord frame;
        frame.ts_ms = 100;
        frame.endpoint_id = "ep1";
        frame.tx = {0xAB, 0xCD};
        frame.rx = {0x01};
        frame.rtt_ms = 2.0;
        log.log_frame(frame);
    }
    REQUIRE(std::filesystem::file_size(path) > 0);
    std::filesystem::remove(path);
}
