// What every performance metric measures right now, beside its budget - the
// tool for setting or moving one (docs/testing.md, "Performance tests"). These
// cases assert nothing and are hidden, so no test run selects them:
//
//   cube_perf_tests "[.perf-report]"          everything, with the corpus per seed
//   cube_perf_tests "[.perf-report-timing]"   only the wall-time metrics
//
// The timing half is only worth reading from an optimised build.

#include <catch2/catch_test_macros.hpp>

#include "budgets.hpp"
#include "measure.hpp"
#include "metrics.hpp"
#include "perf_build.hpp"

#include <cube/solver/solver.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

using cube::perf::BeginnerWork;
using cube::perf::MethodWork;
using cube::solver::SolveMethod;

constexpr std::string_view method_name(SolveMethod method) {
    switch (method) {
        case SolveMethod::Beginner:
            return "beginner";
        case SolveMethod::Intermediate:
            return "intermediate";
        case SolveMethod::Advanced:
            return "advanced";
    }
    return "?";
}

void print_build() {
    std::println("build: {}  optimised {}  timing meaningful {}", cube::perf::kBuildType,
                 cube::perf::kOptimised ? 1 : 0, cube::perf::kTimingMeaningful ? 1 : 0);
}

// One line per metric: what it measures, its budget, the room between them
// (how far above the measurement the budget sits), and where it came from.
void print_metrics(const char* title, const std::vector<cube::perf::Metric>& metrics) {
    std::println("\n{}", title);
    for (const cube::perf::Metric& m : metrics) {
        const double headroom =
            m.measured > 0.0 ? 100.0 * (m.budget - m.measured) / m.measured : 0.0;
        std::string where;
        if (m.seed != cube::perf::kNoSeed) {
            where = "seed " + std::to_string(m.seed);
        }
        if (!m.note.empty()) {
            where += (where.empty() ? "" : "  ") + m.note;
        }
        std::println("  {:<46} {:>14.2f} / {:<14.2f} {:<18} {:>+7.0f}%  {}{}", m.name, m.measured,
                     m.budget, m.unit, headroom, m.measured > m.budget ? "OVER  " : "", where);
    }
}

void print_timing() {
    print_metrics("timing: solves (fastest run against the calibration workload)",
                  cube::perf::solve_timing_metrics());
    print_metrics("timing: reads", cube::perf::read_timing_metrics());
    print_metrics("timing: scan", cube::perf::scan_timing_metrics());
}

// The beginner solve of each seed, stage by stage.
void print_beginner_per_seed() {
    std::println("\nbeginner, per seed (nodes per stage; largest search of the solve; moves)");
    std::println("{:>4} {:>9} {:>9} {:>9} {:>9} {:>9} | {:>9} {:>9} | {:>5} {:>7}", "seed", "cross",
                 "corners", "mid-edge", "oll", "pll", "solve", "largest", "moves", "ms");
    std::uint64_t worst_total = 0;
    for (unsigned seed = 0; seed < cube::perf::budget::kSolverSeeds; ++seed) {
        const auto start = cube::perf::Clock::now();
        const BeginnerWork work = cube::perf::measure_beginner(cube::perf::corpus_scramble(seed));
        const double ms = cube::perf::milliseconds_since(start);
        std::string stages;
        for (const cube::perf::StageWork& stage : work.stages) {
            stages += std::format(" {:>9}", stage.work.nodes);
        }
        std::println("{:>4}{} | {:>9} {:>9} | {:>5} {:>7.0f}{}", seed, stages, work.total.nodes,
                     work.total.largest, work.moves, ms, work.solved ? "" : "  UNSOLVED");
        worst_total = std::max(worst_total, work.total.nodes);
    }
    std::println("worst solve: {} nodes", worst_total);
}

// The other two methods, a line per seed.
void print_methods_per_seed() {
    for (const SolveMethod method : {SolveMethod::Intermediate, SolveMethod::Advanced}) {
        cube::solver::prepare(method);
        std::println("\n{}, per seed (nodes; searches; largest; moves)", method_name(method));
        for (unsigned seed = 0; seed < cube::perf::budget::kSolverSeeds; ++seed) {
            const auto start = cube::perf::Clock::now();
            const MethodWork work =
                cube::perf::measure_method(cube::perf::corpus_scramble(seed), method);
            std::println("{:>4} {:>11} {:>5} {:>11} {:>4} {:>7.1f} ms{}", seed, work.work.nodes,
                         work.work.searches, work.work.largest, work.moves,
                         cube::perf::milliseconds_since(start), work.solved ? "" : "  UNSOLVED");
        }
    }
}

}  // namespace

TEST_CASE("report: what the solver's work and the reader's buffers measure now",
          "[.perf-report][perf-report]") {
    print_build();
    print_beginner_per_seed();
    print_methods_per_seed();

    // --- every metric, beside its budget ----------------------------------
    print_metrics("beginner", cube::perf::beginner_metrics());
    print_metrics("intermediate", cube::perf::intermediate_metrics());
    print_metrics("advanced", cube::perf::advanced_metrics());
    print_metrics("scan", cube::perf::scan_metrics());
    print_metrics("judgement", cube::perf::judgement_metrics());
    print_timing();
}

TEST_CASE("report: what the wall-time metrics measure now", "[.perf-report-timing]") {
    print_build();
    print_timing();
}
