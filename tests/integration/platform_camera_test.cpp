// The camera interface, called the way the app calls it from a thread of its
// own with no device behind it: a result, not an exception; an error, not a
// crash; nothing to pop before a start.
//
// Built only where the camera is a native device opened from the calling thread
// (CUBEULATOR_PLATFORM_TEST_NATIVE_CAMERA). The browser's is getUserMedia(),
// which has to be asked for from the page and answered by it, and a test
// program under Node has none; its camera is tests/web's, through a browser.
#include <catch2/catch_test_macros.hpp>

#include <interfaces/camera.hpp>

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
