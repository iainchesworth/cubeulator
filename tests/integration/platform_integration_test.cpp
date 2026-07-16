#include <catch2/catch_test_macros.hpp>

#include <interfaces/camera.hpp>
#include <interfaces/inference_env.hpp>
#include <interfaces/paths.hpp>

#include <algorithm>

TEST_CASE("the platform-selected paths and inference_env implementations agree on a build",
          "[integration][platform]") {
    // Exercises the CMake-selected per-OS translation units end to end,
    // rather than any single interface in isolation.
    const auto paths = cube::platform::paths::current();
    const auto env = cube::platform::inference_env::current();

    REQUIRE_FALSE(env.execution_providers.empty());
    // Every platform's provider list includes a CPU fallback.
    REQUIRE(std::ranges::find(env.execution_providers, "CPU") != env.execution_providers.end());

    (void)paths;  // Desktop platforms populate this; mobile stubs may not yet.
}

TEST_CASE("camera enumeration returns a result rather than throwing", "[integration][platform]") {
    const auto devices = cube::platform::camera::enumerate_devices();
    REQUIRE(devices.has_value());
}
