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
#include <cstdio>
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
    std::printf("build: %.*s  optimised %d  timing meaningful %d\n",
                static_cast<int>(cube::perf::kBuildType.size()), cube::perf::kBuildType.data(),
                cube::perf::kOptimised ? 1 : 0, cube::perf::kTimingMeaningful ? 1 : 0);
}

// One line per metric: what it measures, its budget, the room between them
// (how far above the measurement the budget sits), and where it came from.
void print_metrics(const char* title, const std::vector<cube::perf::Metric>& metrics) {
    std::printf("\n%s\n", title);
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
        std::printf("  %-46s %14.2f / %-14.2f %-18s %+7.0f%%  %s%s\n", m.name.c_str(), m.measured,
                    m.budget, m.unit.c_str(), headroom, m.measured > m.budget ? "OVER  " : "",
                    where.c_str());
    }
}

void print_timing() {
    print_metrics("timing: solves (fastest run against the calibration workload)",
                  cube::perf::solve_timing_metrics());
    print_metrics("timing: reads", cube::perf::read_timing_metrics());
    print_metrics("timing: scan", cube::perf::scan_timing_metrics());
}

}  // namespace

TEST_CASE("report: what the solver's work and the reader's buffers measure now",
          "[.perf-report][perf-report]") {
    print_build();

    // --- the beginner solve, seed by seed ---------------------------------
    std::printf("\nbeginner, per seed (nodes per stage; largest search of the solve; moves)\n");
    std::printf("%4s %9s %9s %9s %9s %9s | %9s %9s | %5s %7s\n", "seed", "cross", "corners",
                "mid-edge", "oll", "pll", "solve", "largest", "moves", "ms");
    std::uint64_t worst_total = 0;
    for (unsigned seed = 0; seed < cube::perf::budget::kReportSeeds; ++seed) {
        const auto scrambled = cube::perf::corpus_scramble(seed);
        const auto start = cube::perf::Clock::now();
        const BeginnerWork work = cube::perf::measure_beginner(scrambled);
        const double ms = cube::perf::milliseconds_since(start);
        std::printf("%4u %9llu %9llu %9llu %9llu %9llu | %9llu %9llu | %5zu %7.0f%s\n", seed,
                    static_cast<unsigned long long>(work.stages[0].work.nodes),
                    static_cast<unsigned long long>(work.stages[1].work.nodes),
                    static_cast<unsigned long long>(work.stages[2].work.nodes),
                    static_cast<unsigned long long>(work.stages[3].work.nodes),
                    static_cast<unsigned long long>(work.stages[4].work.nodes),
                    static_cast<unsigned long long>(work.total.nodes),
                    static_cast<unsigned long long>(work.total.largest), work.moves, ms,
                    work.solved ? "" : "  UNSOLVED");
        worst_total = std::max(worst_total, work.total.nodes);
    }
    std::printf("worst solve: %llu nodes\n", static_cast<unsigned long long>(worst_total));

    // --- the other two methods --------------------------------------------
    for (const SolveMethod method : {SolveMethod::Intermediate, SolveMethod::Advanced}) {
        cube::solver::prepare(method);
        std::printf("\n%.*s, per seed (nodes; searches; largest; moves)\n",
                    static_cast<int>(method_name(method).size()), method_name(method).data());
        for (unsigned seed = 0; seed < cube::perf::budget::kReportSeeds; ++seed) {
            const auto start = cube::perf::Clock::now();
            const MethodWork work =
                cube::perf::measure_method(cube::perf::corpus_scramble(seed), method);
            std::printf("%4u %11llu %5llu %11llu %4zu %7.1f ms%s\n", seed,
                        static_cast<unsigned long long>(work.work.nodes),
                        static_cast<unsigned long long>(work.work.searches),
                        static_cast<unsigned long long>(work.work.largest), work.moves,
                        cube::perf::milliseconds_since(start), work.solved ? "" : "  UNSOLVED");
        }
    }

    // --- every metric, beside its budget ----------------------------------
    print_metrics("beginner", cube::perf::beginner_metrics());
    print_metrics("intermediate", cube::perf::intermediate_metrics());
    print_metrics("advanced", cube::perf::advanced_metrics());
    print_metrics("scan", cube::perf::scan_metrics());
    print_timing();
}

TEST_CASE("report: what the wall-time metrics measure now", "[.perf-report-timing]") {
    print_build();
    print_timing();
}
