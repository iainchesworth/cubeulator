#pragma once

// The performance tier's metrics: each one a name, what it measures now and
// the budget it is held to. The tests assert `measured <= budget` for every
// metric of a group and the hidden report prints the same lists, so what a
// failure says and what the report shows cannot differ.

#include <string>
#include <vector>

namespace cube::perf {

// A seed that is not one corpus scramble: a mean, or something that has no
// scramble behind it.
inline constexpr unsigned kNoSeed = ~0U;

struct Metric {
    // "<group>.<thing>.<statistic>", e.g. "beginner.middle_edges.nodes.max".
    std::string name;
    double measured = 0.0;
    double budget = 0.0;
    // The corpus seed that gave `measured` where it is a maximum, so a failure
    // can be reproduced with the report.
    unsigned seed = kNoSeed;
    // What the numbers are counted in, for the line that prints them.
    std::string unit{};
    // Anything else worth reading beside a failure: the raw times behind a
    // ratio, say.
    std::string note{};
};

// --- work: counted, exact on every machine -----------------------------------

// The three solve methods over the solver corpus: nodes per stage and per
// solve, the largest single search, and solution length.
[[nodiscard]] std::vector<Metric> beginner_metrics();
[[nodiscard]] std::vector<Metric> intermediate_metrics();
[[nodiscard]] std::vector<Metric> advanced_metrics();

// The N x N solver over its corpus, for each size: moves per solve, and the
// 3-cycles the size's library holds.
[[nodiscard]] std::vector<Metric> nxn_metrics();

// The reader's pixel buffers over synthetic frames.
[[nodiscard]] std::vector<Metric> scan_metrics();

// What judging a scan costs, counted in the exhaustive searches over the 4^6
// turns of its six sides (AssemblyTrace::searches): a cube the sides make one
// way, several ways, and no way; judged after every read, as the app judges it,
// and then again as a held cube is.
[[nodiscard]] std::vector<Metric> judgement_metrics();

// --- time: a ratio to the calibration workload, opt-in -----------------------

// Solving by each method, reading frames, and feeding and finishing a scan.
[[nodiscard]] std::vector<Metric> solve_timing_metrics();
// An N x N solve at the larger sizes.
[[nodiscard]] std::vector<Metric> nxn_timing_metrics();
[[nodiscard]] std::vector<Metric> read_timing_metrics();
[[nodiscard]] std::vector<Metric> scan_timing_metrics();

}  // namespace cube::perf
