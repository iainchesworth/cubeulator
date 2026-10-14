#pragma once

#include <cube/diagnostics/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace cube::diagnostics {

// One line of the diagnostics file (docs/diagnostics.md):
//
//   {"seq":12,"time":"2026-10-14T20:11:03.412Z","kind":"scan","data":{...}}
//
// `seq` counts records written since the process started, so a reader that
// joins two files, or misses a line, can tell. `time` is UTC. `kind` says what
// `data` holds; a reader ignores kinds it does not know.
struct RecordHeader {
    std::uint64_t seq = 0;
    std::string_view time;
    std::string_view kind;
};

[[nodiscard]] Json make_record(const RecordHeader& header, Json data);

// ISO 8601 UTC with milliseconds, e.g. "2026-10-14T20:11:03.412Z".
[[nodiscard]] std::string utc_timestamp(std::chrono::system_clock::time_point when);

}  // namespace cube::diagnostics
