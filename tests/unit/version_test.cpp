#include <catch2/catch_test_macros.hpp>

#include <cube/version.hpp>

#include <string>

TEST_CASE("semantic version is populated", "[version]") {
    REQUIRE(cube::version_major == 0);
    REQUIRE(cube::version_minor == 1);
    REQUIRE(cube::version_patch == 0);
    REQUIRE(cube::version_string == "0.1.0");
}

TEST_CASE("git provenance is stamped", "[version]") {
    // The exact values depend on the checkout, but the fields must be filled in.
    REQUIRE_FALSE(std::string{cube::git_describe}.empty());
    REQUIRE_FALSE(std::string{cube::git_commit}.empty());
}

TEST_CASE("version_details renders a non-empty block", "[version]") {
    const std::string details = cube::version_details();
    REQUIRE(details.find("Cubeulator") != std::string::npos);
    REQUIRE(details.find(cube::version_string) != std::string::npos);
}
