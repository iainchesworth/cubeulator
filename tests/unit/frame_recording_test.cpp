// The recording format (cube/vision/frame_recording.hpp) is the contract
// between two things that never run in the same process: the app's
// CUBE_VISION_RECORD_DIR recorder (scan_session.cpp) and the offline
// [.track-replay] harness (frame_debug_test.cpp). These tests pin the
// format itself - filename ring behavior and index.csv parsing, including
// the malformed inputs a recording interrupted mid-write produces - plus
// one end-to-end round trip through a synthetic on-disk recording, so a
// format regression surfaces here instead of as a silently empty replay of
// a corpus clip that took real hardware time to record.

#include <catch2/catch_test_macros.hpp>

#include <cube/vision/frame_recording.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace cube::vision::recording;

TEST_CASE("recording frame filenames are zero-padded and wrap at the ring size", "[replay]") {
    CHECK(frame_filename(0) == "rec_000000.jpg");
    CHECK(frame_filename(42) == "rec_000042.jpg");
    CHECK(frame_filename(kRecordRingFrames - 1) == "rec_003599.jpg");
    // The ring cap is what bounds a forgotten recorder's disk use: pass N
    // and pass N + kRecordRingFrames deliberately collide.
    CHECK(frame_filename(kRecordRingFrames) == frame_filename(0));
    CHECK(frame_filename(kRecordRingFrames + 7) == frame_filename(7));
}

TEST_CASE("index rows round-trip through parse_index_line", "[replay]") {
    const auto row = parse_index_line("17,123456789012,640,360,1920,1080");
    REQUIRE(row.has_value());
    CHECK(row->pass == 17);
    CHECK(row->t_ms == 123456789012LL);
    CHECK(row->w == 640);
    CHECK(row->h == 360);
    CHECK(row->full_w == 1920);
    CHECK(row->full_h == 1080);
}

TEST_CASE("parse_index_line rejects the header, blanks, and malformed rows", "[replay]") {
    CHECK_FALSE(parse_index_line(kIndexCsvHeader).has_value());
    CHECK_FALSE(parse_index_line("").has_value());
    CHECK_FALSE(parse_index_line("\r\n").has_value());
    // A pass killed mid-append leaves a truncated final line.
    CHECK_FALSE(parse_index_line("18,123456").has_value());
    CHECK_FALSE(parse_index_line("18,123456,640,360,1920,1080,extra").has_value());
    CHECK_FALSE(parse_index_line("x,123456,640,360,1920,1080").has_value());
    // A bare trailing delimiter is the sneaky variant of the seven-field
    // row: the leftover after the sixth field is empty, so only an explicit
    // check keeps "malformed -> nullopt" honest here.
    CHECK_FALSE(parse_index_line("18,123456,640,360,1920,1080,").has_value());
    CHECK_FALSE(parse_index_line("18,123456,640,360,1920,1080,\r\n").has_value());
    // Windows line endings must not poison the last field.
    const auto crlf = parse_index_line("3,99,640,360,1280,720\r\n");
    REQUIRE(crlf.has_value());
    CHECK(crlf->full_h == 720);
}

TEST_CASE("rows a full ring lap behind the newest are flagged overwritten", "[replay]") {
    // The boundary case IS the contract: pass 0's file survives until the
    // exact moment pass kRecordRingFrames reuses its slot.
    CHECK_FALSE(frame_overwritten(0, 0));
    CHECK_FALSE(frame_overwritten(0, kRecordRingFrames - 1));
    CHECK(frame_overwritten(0, kRecordRingFrames));
    CHECK(frame_overwritten(5, kRecordRingFrames + 5));
    CHECK_FALSE(frame_overwritten(6, kRecordRingFrames + 5));
}

TEST_CASE("a same-resolution recording longer than the ring drops its overwritten head", "[replay]") {
    // Within one app run the analysis resolution never changes, so every
    // row here records the same (w, h) - the case where the replay
    // harness's size cross-check is blind and the index-derived
    // frame_overwritten predicate is the ONLY thing standing between a
    // wrapped recording and silently pairing early rows' timestamps with
    // late-clip pixels. Mirrors the harness's replay loop: read the index,
    // take max_pass, exclude what frame_overwritten flags.
    std::ostringstream csv;
    csv << kIndexCsvHeader << '\n';
    const int total_rows = kRecordRingFrames + 2;
    for (int pass = 0; pass < total_rows; ++pass) {
        csv << pass << ',' << 1000 + 33LL * pass << ",640,360,1920,1080\n";
    }
    std::istringstream stream(csv.str());
    const auto rows = read_index(stream);
    REQUIRE(rows.size() == static_cast<std::size_t>(total_rows));

    int max_pass = 0;
    for (const auto& row : rows) {
        max_pass = std::max(max_pass, row.pass);
    }
    CHECK(max_pass == total_rows - 1);

    // Exactly the first two passes' slots were reused (by passes 3600 and
    // 3601); everything from pass 2 on is the intact final lap.
    for (const auto& row : rows) {
        CAPTURE(row.pass);
        CHECK(frame_overwritten(row.pass, max_pass) == (row.pass < 2));
    }
}

TEST_CASE("read_index keeps every parseable row and drops the rest", "[replay]") {
    std::istringstream stream(std::string(kIndexCsvHeader) +
                              "\n0,1000,640,360,1920,1080\n1,1033,640,360,1920,1080\n2,10");
    const auto rows = read_index(stream);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].pass == 0);
    CHECK(rows[1].t_ms == 1033);
}

TEST_CASE("a synthetic recording round-trips from disk via the shared format", "[replay]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "cube_frame_recording_test";
    fs::create_directories(dir);

    // Write exactly what the app-side recorder writes: header + one row per
    // frame, frames named by the shared filename helper.
    {
        std::ofstream index(dir / std::string(kIndexCsvName), std::ios::trunc);
        index << kIndexCsvHeader << '\n';
        for (int pass = 0; pass < 2; ++pass) {
            const cv::Mat frame(36, 64, CV_8UC3, cv::Scalar(20 + 100 * pass, 40, 60));
            REQUIRE(cv::imwrite((dir / frame_filename(pass)).string(), frame,
                                {cv::IMWRITE_JPEG_QUALITY, 90}));
            index << pass << ',' << 1000 + 33 * pass << ",64,36,1280,720\n";
        }
    }

    // Scoped so the stream is closed before remove_all below - Windows
    // refuses to delete a directory tree containing an open file.
    const auto rows = [&dir] {
        std::ifstream index_stream(dir / std::string(kIndexCsvName));
        REQUIRE(index_stream);
        return read_index(index_stream);
    }();
    REQUIRE(rows.size() == 2);
    for (const auto& row : rows) {
        const cv::Mat frame = cv::imread((dir / frame_filename(row.pass)).string(), cv::IMREAD_COLOR);
        REQUIRE_FALSE(frame.empty());
        // The size cross-check is the replay harness's secondary guard
        // against cross-run leftovers (frame_overwritten covers same-run
        // ring wraps), so the recorded metadata must agree with what
        // imread hands back.
        CHECK(frame.cols == row.w);
        CHECK(frame.rows == row.h);
    }
    CHECK(rows[1].t_ms - rows[0].t_ms == 33);

    fs::remove_all(dir);
}
