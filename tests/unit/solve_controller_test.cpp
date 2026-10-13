#include <catch2/catch_test_macros.hpp>

#include "controllers/scan_session.hpp"
#include "solve_controller.hpp"

#include <QMetaObject>
#include <QMetaProperty>

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

TEST_CASE("what changes when a solve lands is announced to the screen", "[qt][controllers]") {
    // The solve screen is created while the solve is still running. A
    // property declared constant is read then - as zero moves and no stages -
    // and never again, which left the screen showing "0 moves" and a dead
    // stage list beside a solution it could step through.
    const QMetaObject& meta = SolveController::staticMetaObject;
    for (const char* name : {"totalMoves", "stageRanges", "moveIndex", "currentMoveNotation", "isSolving"}) {
        const QMetaProperty property = meta.property(meta.indexOfProperty(name));
        INFO(name);
        REQUIRE(property.isValid());
        CHECK(property.hasNotifySignal());
        CHECK_FALSE(property.isConstant());
    }
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
