#include <catch2/catch_test_macros.hpp>

#include <cube/scan_session_config.hpp>

TEST_CASE("ScanSessionConfig fluent builder chains and defaults sensibly", "[config]") {
    const auto config = cube::ScanSessionConfig{}
                             .with_device("front-camera")
                             .with_auto_advance(true);

    REQUIRE(config.device_identifier() == "front-camera");
    REQUIRE(config.auto_advance());
    REQUIRE(config.frame_timeout_ms() == 5000);
}

TEST_CASE("ScanSessionConfig with_frame_timeout_ms overrides the default", "[config]") {
    const auto config = cube::ScanSessionConfig{}.with_frame_timeout_ms(1000);
    REQUIRE(config.frame_timeout_ms() == 1000);
}
