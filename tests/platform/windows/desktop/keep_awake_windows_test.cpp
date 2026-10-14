// KeepAwake on Windows (SetThreadExecutionState): the request is always
// granted, with nothing to ask the person and no session to be missing, so a
// request is held at once and let go of at once.
//
// The behaviour that holds on every platform, Linux included, is in
// tests/unit/keep_awake_test.cpp.

#include "desktop/keep_awake.hpp"

#include <catch2/catch_test_macros.hpp>

using cube::app::KeepAwake;

TEST_CASE("The platform grants a keep-awake request at once", "[desktop][keepawake][platform]") {
    KeepAwake keepAwake;

    keepAwake.setActive(true);
    REQUIRE(keepAwake.held());

    keepAwake.setActive(false);
    REQUIRE_FALSE(keepAwake.held());
}
