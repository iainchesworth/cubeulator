#include <catch2/catch_test_macros.hpp>

#include "controllers/scan_session.hpp"
#include "solve_controller.hpp"

using cube::app::ScanSession;

// SolveController's reaction to a completed scan (onScanCompleted's
// solveReady/scanRejected branches) isn't exercised here: it only fires
// from ScanSession::scanCompleted, which only fires once confirmFace() has
// a real detected reading - set solely by analyze(), a private method only
// reachable via poll()'s camera-driven pipeline. There's no public seam to
// inject a completed scan without adding test-only scaffolding to
// ScanSession, which scan_session_test.cpp deliberately avoids for the same
// reason. That path is covered by manual verification instead (see the
// implementation plan).

TEST_CASE("SolveController starts with a solved placeholder and no solve in flight", "[qt][controllers]") {
    ScanSession session;
    SolveController controller(session);

    // Unlike the old random_scramble()-seeded constructor, nothing is
    // solved until a real scan completes - see onScanCompleted().
    REQUIRE_FALSE(controller.isSolving());
    REQUIRE(controller.totalMoves() == 0);
    REQUIRE(controller.atStart());
    REQUIRE(controller.atEnd());
}

TEST_CASE("SolveController::nextMove/previousMove are no-ops with nothing to step", "[qt][controllers]") {
    ScanSession session;
    SolveController controller(session);

    controller.nextMove();
    controller.previousMove();

    REQUIRE(controller.moveIndex() == 0);
    REQUIRE(controller.totalMoves() == 0);
}

TEST_CASE("SolveController's placeholder cube is solved on all three visible faces", "[qt][controllers]") {
    ScanSession session;
    SolveController controller(session);

    for (const auto& colors : {controller.topColors(), controller.rightColors(), controller.leftColors()}) {
        REQUIRE(colors.size() == 9);
        const auto center = colors.at(4);
        for (const auto& color : colors) {
            REQUIRE(color == center);
        }
    }
}
