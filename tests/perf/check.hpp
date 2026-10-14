#pragma once

// What a performance case does with its metrics, and how it stands aside in a
// build where it has nothing to say.

#include <catch2/catch_test_macros.hpp>

#include "measure.hpp"
#include "metrics.hpp"
#include "perf_build.hpp"

#include <string>
#include <vector>

namespace cube::perf {

// Ends the case as skipped (not failed) in a build that is not optimised: the
// corpus there is minutes, and the counts it would check are the same ones an
// optimised build checks.
inline void skip_unless_optimised() {
    if constexpr (!kOptimised) {
        SKIP("the performance tier runs in an optimised build, and this is "
             << kBuildType << "; its work counts are the same in every build, so the "
             << "optimised legs hold them (docs/testing.md, \"Performance tests\")");
    }
}

// Ends the case as skipped where a stopwatch cannot be read as a statement
// about the program, and where nobody asked for one.
inline void skip_unless_timing_wanted() {
    // An if/else and not two ifs: SKIP ends the function, and a statement
    // after the one the build always takes is unreachable code, which is a
    // warning and so an error.
    if constexpr (kTimingMeaningful) {
        if (!timing_requested()) {
            SKIP(
                "wall-time checks are opt-in: set CUBE_PERF_TIMING=1, or run the *-perf test "
                "preset (docs/testing.md, \"Performance tests\")");
        }
    } else {
        SKIP("a clock reading is not about the program in this build ("
             << kBuildType << (kInstrumented ? ", with coverage or a sanitizer compiled in" : "")
             << "); wall-time checks need an optimised, uninstrumented build");
    }
}

// Every metric within its budget. A failure names the metric, the seed that
// gave a maximum, and both numbers.
inline void check_within_budget(const std::vector<Metric>& metrics) {
    for (const Metric& m : metrics) {
        INFO("metric " << m.name << " (" << m.unit << ")"
                       << (m.seed == kNoSeed ? std::string()
                                             : ", worst corpus seed " + std::to_string(m.seed))
                       << (m.note.empty() ? std::string() : "; " + m.note));
        CHECK(m.measured <= m.budget);
    }
}

}  // namespace cube::perf
