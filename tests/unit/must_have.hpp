#pragma once

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>

namespace cube::test {

// The value of an optional that the test has just required to be there.
//
// REQUIRE(x.has_value()) already ends the test case when it is empty, but the
// flow analysis behind bugprone-unchecked-optional-access cannot see that
// Catch2's failure path does not return, so every `x->` after it reads as an
// unchecked access. Doing the check here, where an early `throw` is a path
// the analysis follows, states the same thing once: a test that gets past
// this call holds a value, and one that does not has said so by name.
template <typename T>
[[nodiscard]] const T& must_have(const std::optional<T>& value) {
    REQUIRE(value.has_value());
    if (!value.has_value()) {
        throw std::logic_error("must_have: REQUIRE did not end the test case");
    }
    return *value;
}

}  // namespace cube::test
