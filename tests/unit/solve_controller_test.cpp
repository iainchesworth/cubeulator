#include <catch2/catch_test_macros.hpp>

#include "solve_controller.hpp"

#include <QMetaObject>
#include <QMetaProperty>

// A scanned cube arriving and coming back solved for stepping is covered in
// direct_scan_controller_test.cpp, which can drive a whole scan without a
// camera.

TEST_CASE("SolveController starts with a solved placeholder and no solve in flight",
          "[qt][controllers]") {
    SolveController controller;

    // Nothing is solved until a scan hands over a cube - see solveCube().
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
    for (const char* name :
         {"totalMoves", "stageRanges", "moveIndex", "currentMoveNotation", "isSolving"}) {
        const QMetaProperty property = meta.property(meta.indexOfProperty(name));
        INFO(name);
        REQUIRE(property.isValid());
        CHECK(property.hasNotifySignal());
        CHECK_FALSE(property.isConstant());
    }
}

TEST_CASE("SolveController::nextMove/previousMove are no-ops with nothing to step",
          "[qt][controllers]") {
    SolveController controller;

    controller.nextMove();
    controller.previousMove();

    REQUIRE(controller.moveIndex() == 0);
    REQUIRE(controller.totalMoves() == 0);
}

TEST_CASE("SolveController's placeholder cube is solved on all three visible faces",
          "[qt][controllers]") {
    SolveController controller;

    for (const auto& colors :
         {controller.topColors(), controller.rightColors(), controller.leftColors()}) {
        REQUIRE(colors.size() == 9);
        const auto center = colors.at(4);
        for (const auto& color : colors) {
            REQUIRE(color == center);
        }
    }
}
