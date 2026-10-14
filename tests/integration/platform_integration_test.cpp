#include <catch2/catch_test_macros.hpp>

#include <interfaces/camera.hpp>
#include <interfaces/paths.hpp>
#include <interfaces/platform_info.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("camera enumeration returns a result rather than throwing", "[integration][platform]") {
    const auto devices = cube::platform::camera::enumerate_devices();
    REQUIRE(devices.has_value());
}

TEST_CASE("starting a camera without a real device reports an error, not a crash",
          "[integration][platform]") {
    const auto result = cube::platform::camera::start(
        cube::platform::camera::DeviceInfo{.identifier = "none", .display_name = "none"});
    REQUIRE_FALSE(result.has_value());
    cube::platform::camera::stop();
}

TEST_CASE("try_pop_latest_frame and is_capturing are safe before any start() and after stop()",
          "[integration][platform]") {
    REQUIRE_FALSE(cube::platform::camera::is_capturing());
    REQUIRE_FALSE(cube::platform::camera::try_pop_latest_frame().has_value());

    cube::platform::camera::stop();

    REQUIRE_FALSE(cube::platform::camera::is_capturing());
    REQUIRE_FALSE(cube::platform::camera::try_pop_latest_frame().has_value());
}

TEST_CASE("platform_info::current offers only graphics backends the app can apply",
          "[integration][platform]") {
    const auto info = cube::platform::platform_info::current();

    // main.cpp's applyPersistedGraphicsBackend() applies exactly these keys;
    // a platform that lists another would offer a choice that silently does
    // nothing on the next launch.
    constexpr std::array<std::string_view, 5> kApplicable{
        "d3d11", "d3d12", "vulkan", "opengl", "metal",
    };
    REQUIRE_FALSE(info.graphics_backends.empty());

    std::vector<std::string> keys;
    for (const auto& backend : info.graphics_backends) {
        INFO("backend key: " << backend.key);
        CHECK(std::ranges::find(kApplicable, backend.key) != kApplicable.end());
        CHECK_FALSE(backend.label.empty());
        keys.push_back(backend.key);
    }

    // Listed once: the Settings screen keys its chips by it.
    std::ranges::sort(keys);
    CHECK(std::ranges::adjacent_find(keys) == keys.end());
}
