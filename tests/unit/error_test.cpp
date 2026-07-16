#include <catch2/catch_test_macros.hpp>

#include <cube/error.hpp>

#include <string_view>

using cube::CubeError;
using cube::Result;

TEST_CASE("to_string covers every CubeError value", "[error]") {
    REQUIRE(cube::to_string(CubeError::camera_not_found) == "no camera device was found");
    REQUIRE(cube::to_string(CubeError::no_solution) == "no solution could be found");
    REQUIRE_FALSE(cube::to_string(CubeError::model_load_failed).empty());
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
