#include <catch2/catch_test_macros.hpp>

#include <cube/gpu_render.hpp>
#include <cube/i18n/i18n.hpp>
#include <cube/render/render.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/vision.hpp>

TEST_CASE("stub module entry points succeed", "[stub]") {
    REQUIRE(cube::solver::not_yet_implemented().has_value());
    REQUIRE(cube::vision::not_yet_implemented().has_value());
    REQUIRE(cube::render::not_yet_implemented().has_value());
    REQUIRE(cube::i18n::not_yet_implemented().has_value());
    REQUIRE(cube::gpu_render::not_yet_implemented().has_value());
}
