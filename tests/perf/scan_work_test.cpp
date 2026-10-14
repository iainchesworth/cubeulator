// Work budgets for the reader: how many pixel buffers a read of a drawn frame
// allocates, and that a read of a bigger frame does not allocate more. Counted,
// not timed, so a regression that adds a full-frame conversion, or a buffer per
// sticker per frame, fails here on any machine.
//
// What a failure means, and how to move a budget: docs/testing.md,
// "Performance tests". The numbers are in budgets.hpp.

#include <catch2/catch_test_macros.hpp>

#include "check.hpp"
#include "metrics.hpp"

TEST_CASE("reading a side allocates a bounded set of pixel buffers at every frame size",
          "[perf][scan]") {
    cube::perf::skip_unless_optimised();
    cube::perf::check_within_budget(cube::perf::scan_metrics());
}

TEST_CASE("judging a scan runs its searches once for a naming, and none after", "[perf][scan]") {
    cube::perf::skip_unless_optimised();
    cube::perf::check_within_budget(cube::perf::judgement_metrics());
}
