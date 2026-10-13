#pragma once

// On-disk format of a scan recording (docs/recording-format.md): the exact
// (already downscaled) frames the reader ran on, one JPEG per pass, plus
// an append-only index.csv of per-pass metadata. The recorder lives in the
// app (DirectScan, under CUBE_SCAN_RECORD_DIR) and the replay consumer is
// `cube_scan` - they compile into different binaries, so the naming and
// CSV schema live here, in one place, where neither side can drift from
// the other.
//
// Header-only and Qt/OpenCV-free on purpose: it is pure naming/parsing, so
// the Qt-free cube::lib test binary and the Qt app can both use it without
// dragging new dependencies or CMake source-list entries along.
//
// Ring behavior: frame files wrap at kRecordRingFrames (filename is
// pass % kRecordRingFrames) so an accidentally-left-on recorder bounds its
// disk use, while index.csv keeps appending - a replay of a wrapped
// recording finds rows whose file has been overwritten by a later pass.
// The index itself is the authoritative wrap detector (frame_overwritten
// below): within one app run the analysis resolution never changes, so the
// obvious (w, h)-vs-disk cross-check would silently pair a stale row's
// timestamp with a later lap's same-sized image; the size check survives
// only as a secondary guard against cross-run leftovers. At the ~30 Hz
// analysis ceiling the ring holds the most recent ~2 minutes, comfortably
// longer than any single scripted corpus clip.

#include <charconv>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cube::vision::recording {

inline constexpr int kRecordRingFrames = 3600;
inline constexpr std::string_view kIndexCsvName = "index.csv";
inline constexpr std::string_view kIndexCsvHeader = "pass,t_ms,w,h,full_w,full_h";

// One index.csv row. w/h are the recorded (analysis-scale) frame size;
// full_w/full_h are the original camera frame size, kept so a replay can
// recover the scale factor the live path used. t_ms is the
// TimedFrame mailbox timestamp (steady_clock ms - only differences are
// meaningful, see interfaces/camera.hpp).
struct RecordedFrameInfo {
    int pass = 0;
    std::int64_t t_ms = 0;
    int w = 0;
    int h = 0;
    int full_w = 0;
    int full_h = 0;
};

// "rec_000042.jpg" for the pass's slot in the ring. Zero-padded to a fixed
// width so directory listings sort chronologically (within one ring lap).
[[nodiscard]] inline std::string frame_filename(int pass) {
    std::string name = "rec_000000.jpg";
    int slot = pass % kRecordRingFrames;
    if (slot < 0) {
        slot += kRecordRingFrames;
    }
    for (int digit = 9; slot > 0; --digit, slot /= 10) {
        name[static_cast<std::size_t>(digit)] = static_cast<char>('0' + slot % 10);
    }
    return name;
}

// True when a row's frame file has provably been replaced on disk: rows a
// full ring lap (or more) older than the newest row in the same index share
// a filename slot with a later pass that has already overwritten it (the
// recorder writes the JPEG before appending the row, so the newest row's
// whole lap is on disk). Callers should compute max_pass over the rows they
// read and drop these - the file that loads at that name holds a LATER
// frame's pixels, and pairing them with this row's timestamp corrupts every
// downstream metric without any size mismatch to betray it.
[[nodiscard]] inline bool frame_overwritten(int pass, int max_pass) {
    return max_pass - pass >= kRecordRingFrames;
}

// Parses one CSV row; nullopt for the header row, blank lines, or anything
// malformed (a recording truncated mid-write ends with a partial line -
// replay must shrug that off, not die on it).
[[nodiscard]] inline std::optional<RecordedFrameInfo> parse_index_line(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    if (line.empty() || line == kIndexCsvHeader) {
        return std::nullopt;
    }
    if (line.back() == ',') {
        // A trailing delimiter would otherwise slip through: the sixth
        // field parse swallows it as a separator, leaving an empty view
        // that the leftover-text check below can't tell from a clean
        // end-of-line. No recorder output ever ends with one, so it's
        // malformed.
        return std::nullopt;
    }

    RecordedFrameInfo info;
    const auto parse_field = [&line](auto& out) {
        const std::size_t comma = line.find(',');
        const std::string_view field = line.substr(0, comma);
        line.remove_prefix(comma == std::string_view::npos ? line.size() : comma + 1);
        const auto [ptr, ec] = std::from_chars(field.data(), field.data() + field.size(), out);
        return ec == std::errc{} && ptr == field.data() + field.size();
    };
    if (!parse_field(info.pass) || !parse_field(info.t_ms) || !parse_field(info.w) ||
        !parse_field(info.h) || !parse_field(info.full_w) || !parse_field(info.full_h) ||
        !line.empty()) {
        return std::nullopt;
    }
    return info;
}

// Every parseable row of an index.csv stream, in file order (which is pass
// order - the recorder only ever appends).
[[nodiscard]] inline std::vector<RecordedFrameInfo> read_index(std::istream& stream) {
    std::vector<RecordedFrameInfo> rows;
    std::string line;
    while (std::getline(stream, line)) {
        if (const auto info = parse_index_line(line)) {
            rows.push_back(*info);
        }
    }
    return rows;
}

}  // namespace cube::vision::recording
