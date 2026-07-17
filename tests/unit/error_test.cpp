#include <catch2/catch_test_macros.hpp>

#include <cube/error.hpp>

#include <string_view>

using cube::CubeError;
using cube::Result;

TEST_CASE("to_string covers every CubeError value", "[error]") {
    REQUIRE(cube::to_string(CubeError::invalid_cube_state) ==
            "the supplied cube state is not physically valid");
    REQUIRE(cube::to_string(CubeError::unsolvable_state) ==
            "no solution exists for this cube state");
    REQUIRE(cube::to_string(CubeError::invalid_move_sequence) ==
            "the move sequence contains an invalid move");
    REQUIRE(cube::to_string(CubeError::no_solution) == "no solution could be found");
    REQUIRE(cube::to_string(CubeError::camera_not_found) == "no camera device was found");
    REQUIRE(cube::to_string(CubeError::camera_start_failed) == "the camera could not be started");
    REQUIRE(cube::to_string(CubeError::no_valid_cube_detected) ==
            "no valid cube was detected in the frame");
    REQUIRE(cube::to_string(CubeError::face_capture_incomplete) ==
            "not all faces have been captured yet");
    REQUIRE(cube::to_string(CubeError::model_load_failed) ==
            "the inference model could not be loaded");
    REQUIRE(cube::to_string(CubeError::inference_provider_unavailable) ==
            "no suitable inference execution provider is available");
    REQUIRE(cube::to_string(CubeError::inference_failed) == "inference failed to produce a result");
    REQUIRE(cube::to_string(CubeError::gpu_surface_unavailable) ==
            "no GPU-compatible render surface is available");
    REQUIRE(cube::to_string(CubeError::shader_compile_failed) == "a shader failed to compile");
    REQUIRE(cube::to_string(CubeError::locale_resource_not_found) ==
            "a locale resource file could not be found");
}

TEST_CASE("Result<T> composes with std::expected's monadic operations", "[error]") {
    const Result<int> ok = 42;
    const Result<int> err = std::unexpected(CubeError::no_solution);

    REQUIRE(ok.has_value());
    REQUIRE(ok.value() == 42);

    REQUIRE_FALSE(err.has_value());
    REQUIRE(err.error() == CubeError::no_solution);

    const auto doubled = ok.transform([](int v) { return v * 2; });
    REQUIRE(doubled.value() == 84);
}
