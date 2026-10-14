#include <catch2/catch_test_macros.hpp>

#include <cube/version.hpp>

#include <string>
#include <string_view>

TEST_CASE("semantic version is populated", "[version]") {
    // Not a literal: the version moves with every release, and a test that
    // names it would have to be edited by each one. The string is the three
    // numbers, then a "-label.N" for a pre-release (docs/releasing.md).
    const std::string numbers = std::to_string(cube::version_major) + "." +
                                std::to_string(cube::version_minor) + "." +
                                std::to_string(cube::version_patch);
    const std::string_view full = cube::version_string;
    REQUIRE(full.substr(0, numbers.size()) == numbers);
    REQUIRE((full.size() == numbers.size() || full.at(numbers.size()) == '-'));
}

TEST_CASE("git provenance is stamped", "[version]") {
    // The exact values depend on the checkout, but the fields must be filled in.
    REQUIRE_FALSE(std::string{cube::git_describe}.empty());
    REQUIRE_FALSE(std::string{cube::git_commit}.empty());
}

TEST_CASE("version_details renders a non-empty block", "[version]") {
    const std::string details = cube::version_details();
    REQUIRE(details.contains("Cubeulator"));
    REQUIRE(details.contains(cube::version_string));
}
