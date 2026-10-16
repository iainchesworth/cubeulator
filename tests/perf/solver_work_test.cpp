// Work budgets for the solver: how many nodes its searches visit, over a fixed
// corpus of scrambles, per stage and per solve, for each of the three methods.
// Nothing here is timed. The counts are the same on every machine, compiler and
// build type, which is why they can be held close to what they are: a change
// that loses a signature reduction multiplies them by an order of magnitude
// long before a stopwatch notices.
//
// What a failure means, and how to move a budget: docs/testing.md,
// "Performance tests". The numbers are in budgets.hpp.

#include <catch2/catch_test_macros.hpp>

#include "budgets.hpp"
#include "check.hpp"
#include "measure.hpp"
#include "metrics.hpp"

#include <cube/solver/search_work.hpp>
#include <cube/solver/solver.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using cube::perf::check_within_budget;
using cube::perf::corpus_scramble;
using cube::perf::skip_unless_optimised;
using cube::solver::SolveMethod;

// A search's cost and the solution's length as one comparable value.
std::array<std::uint64_t, 4> what_was_spent(const cube::solver::SearchWork& work,
                                            std::size_t moves) {
    return {work.searches, work.nodes, work.largest, static_cast<std::uint64_t>(moves)};
}

}  // namespace

TEST_CASE("the beginner solve visits no more nodes than its budget, stage by stage",
          "[perf][solver]") {
    skip_unless_optimised();
    check_within_budget(cube::perf::beginner_metrics());
}

TEST_CASE("the intermediate solve visits no more nodes than its budget", "[perf][solver]") {
    skip_unless_optimised();
    check_within_budget(cube::perf::intermediate_metrics());
}

TEST_CASE("the advanced solve visits no more nodes than its budget", "[perf][solver]") {
    skip_unless_optimised();
    check_within_budget(cube::perf::advanced_metrics());
}

TEST_CASE("an N x N solve makes no more moves than its budget, and its library no more cycles",
          "[perf][nxn]") {
    skip_unless_optimised();
    check_within_budget(cube::perf::nxn_metrics());
}

TEST_CASE("the stages measured one at a time are the solve that solve() runs", "[perf][solver]") {
    skip_unless_optimised();
    // The beginner figures above are taken by calling the stage functions in
    // turn, the way solve() does. If solve() grows a stage, or does work
    // between them, those figures would go on being exact about something
    // that is no longer the solve: this is what notices.
    for (const unsigned seed : {4U, 23U}) {
        INFO("corpus seed " << seed << "; searches, nodes, largest search, moves");
        const cube::solver::Cube scrambled = corpus_scramble(seed);

        const cube::perf::BeginnerWork staged = cube::perf::measure_beginner(scrambled);
        const cube::perf::MethodWork whole =
            cube::perf::measure_method(scrambled, SolveMethod::Beginner);

        REQUIRE(staged.solved);
        REQUIRE(whole.solved);
        CHECK(what_was_spent(staged.total, staged.moves) ==
              what_was_spent(whole.work, whole.moves));
    }
}
