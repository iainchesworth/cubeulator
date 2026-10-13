#include <catch2/catch_test_macros.hpp>

#include <cube/i18n/i18n.hpp>

TEST_CASE("stub module entry points succeed", "[stub]") {
    REQUIRE(cube::i18n::not_yet_implemented().has_value());
}
