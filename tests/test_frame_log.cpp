#include <catch2/catch_test_macros.hpp>

#include "adapters/frame_log.hpp"
#include "adapters/frame_replay.hpp"
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
    const auto frames = log.frames();
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].endpoint_id == "ep1");
    CHECK(frames[0].tx.size() == 4);
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

TEST_CASE("FileFrameLog round-trips through parser", "[adapters][framelog]") {
    const auto path = (std::filesystem::temp_directory_path() / "opc_frame_log_parse.txt").string();
    std::filesystem::remove(path);
    {
        opc::adapters::FileFrameLog log(path);
        opc::ports::FrameRecord frame;
        frame.ts_ms = 100;
        frame.endpoint_id = "ep1";
        frame.tx = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
        frame.rx = {0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02, 0x00, 0x2A};
        frame.rtt_ms = 1.25;
        log.log_frame(frame);
    }
    auto loaded = opc::adapters::load_frame_log_file(path);
    REQUIRE(loaded);
    REQUIRE(loaded->size() == 1);
    CHECK((*loaded)[0].endpoint_id == "ep1");
    CHECK((*loaded)[0].tx.size() == 12);
    CHECK((*loaded)[0].rx.back() == 0x2A);
    std::filesystem::remove(path);
}
