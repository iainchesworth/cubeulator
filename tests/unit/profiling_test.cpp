#include <catch2/catch_test_macros.hpp>

#include <cube/profiling.hpp>

#include <cstdint>
#include <string_view>

// CUBE_EXPECT_TRACY and CUBE_EXPECT_RECORDER are set by tests/CMakeLists.txt
// from CUBEULATOR_ENABLE_TRACY and CUBEULATOR_ENABLE_PROFILE_RECORDER (1 or 0),
// so this file can ask "which variant did CMake select?" without an #ifdef of
// its own.

namespace {

// Every macro, in the shapes the sources use. In the disabled variant they all
// expand to nothing, so what this proves there is that a call site still
// compiles warning-free under -Werror; in the enabled variant it also runs
// the real Tracy client with nobody connected (on-demand mode), which must be
// harmless.
int instrumented_sum(int count) {
    CUBE_ZONE_SCOPED();
    int total = 0;
    for (int i = 0; i < count; ++i) {
        CUBE_ZONE_SCOPED_N("test.iteration");
        total += i;
    }
    return total;
}

int manual_zone(int count) {
    CUBE_ZONE_BEGIN(span, "test.manual");
    const int total = instrumented_sum(count);
    CUBE_ZONE_VALUE(span, count);
    CUBE_ZONE_END(span);
    return total;
}

}  // namespace

TEST_CASE("the profiling variant matches the CMake options", "[profiling]") {
    REQUIRE(cube::profiling::enabled == (CUBE_EXPECT_TRACY != 0 || CUBE_EXPECT_RECORDER != 0));
    REQUIRE(cube::profiling::to_recorder == (CUBE_EXPECT_RECORDER != 0));
}

TEST_CASE("instrumented code computes the same result", "[profiling]") {
    REQUIRE(instrumented_sum(5) == 10);
    REQUIRE(manual_zone(5) == 10);
}

TEST_CASE("the process-level macros compile and run", "[profiling]") {
    CUBE_THREAD_NAME("cube-unit-tests");
    CUBE_APP_INFO(std::string_view{"cube_unit_tests"});
    CUBE_MESSAGE("test.message");

    // One of each argument type the sources plot: a count, a float, a flag.
    // [[maybe_unused]] because the disabled variant's macros never read them.
    [[maybe_unused]] const std::int64_t count = 7;
    [[maybe_unused]] const float ratio = 0.5F;
    [[maybe_unused]] const bool flag = true;
    CUBE_PLOT("test.count", count);
    CUBE_PLOT("test.ratio", ratio);
    CUBE_PLOT("test.flag", flag);

    CUBE_FRAME_MARK();
    CUBE_FRAME_MARK_NAMED("test.frames");

    // Nothing to assert about the instrumentation itself - it has no
    // observable effect on the program - beyond having got this far.
    SUCCEED();
}
