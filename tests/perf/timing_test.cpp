// Wall-time checks, the part of the performance tier that is opt-in.
//
// A clock is the one measurement that can be wrong for no reason of its own:
// another process, a cold cache, a runner that is not the machine the budget
// was set on. So each figure here is a ratio to a calibration workload run in
// the same process, alternately with the task and with the same load on both,
// and each budget is a few times what the ratio is; and the cases do not run
// unless CUBE_PERF_TIMING is set, never in an unoptimised or instrumented
// build, and alone (ctest RUN_SERIAL), not beside other tests.
//
// What a failure means: docs/testing.md, "Performance tests".

#include <catch2/catch_test_macros.hpp>

#include "check.hpp"
#include "metrics.hpp"

TEST_CASE("solving takes no more than a multiple of the calibration workload",
          "[perf][timing][solver]") {
    cube::perf::skip_unless_timing_wanted();
    cube::perf::check_within_budget(cube::perf::solve_timing_metrics());
}

TEST_CASE("solving a big cube takes no more than a multiple of the calibration workload",
          "[perf][timing][nxn]") {
    cube::perf::skip_unless_timing_wanted();
    cube::perf::check_within_budget(cube::perf::nxn_timing_metrics());
}

TEST_CASE("reading a frame takes no more than a multiple of the calibration workload",
          "[perf][timing][scan]") {
    cube::perf::skip_unless_timing_wanted();
    cube::perf::check_within_budget(cube::perf::read_timing_metrics());
}

TEST_CASE("feeding and finishing a scan takes no more than a multiple of the calibration workload",
          "[perf][timing][scan]") {
    cube::perf::skip_unless_timing_wanted();
    cube::perf::check_within_budget(cube::perf::scan_timing_metrics());
}
