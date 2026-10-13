#include <catch2/catch_test_macros.hpp>

#include "solve_controller.hpp"

#include <cube/solver/solver.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QMetaProperty>
#include <QVariantMap>

#include <vector>

using cube::solver::Cube;
using cube::solver::random_scramble;

namespace {

// The solve runs on a worker and its result lands through the event loop.
bool wait_until_solved(SolveController& controller) {
    QElapsedTimer waited;
    waited.start();
    while (controller.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return !controller.isSolving();
}

// The stages of the solution in place, as cube::solver::Stage values, in order.
std::vector<int> stage_values(const SolveController& controller) {
    std::vector<int> values;
    for (const QVariant& entry : controller.stageRanges()) {
        values.push_back(entry.toMap().value(QStringLiteral("stageIndex")).toInt());
    }
    return values;
}

// Steps the solution to its end and returns the cube as it stands there.
Cube step_to_end(SolveController& controller) {
    while (!controller.atEnd()) {
        controller.nextMove();
    }
    return controller.currentCube();
}

constexpr int kCross = 0, kWhiteCorners = 1, kMiddleEdges = 2, kOll = 3, kPll = 4;
constexpr int kF2l = 5, kReduce = 6, kFinish = 7;

}  // namespace

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

TEST_CASE("SolveController solves the way it is asked to, and keeps to that method's stages",
          "[qt][controllers][solve-method]") {
    SolveController controller;
    CHECK(controller.method() == SolveController::Beginner);

    const Cube scrambled = random_scramble(41, 100);
    struct Case {
        SolveController::Method method;
        std::vector<int> stages;
    };
    const std::vector<Case> cases = {
        {SolveController::Beginner, {kCross, kWhiteCorners, kMiddleEdges, kOll, kPll}},
        {SolveController::Intermediate, {kCross, kF2l, kOll, kPll}},
        {SolveController::Advanced, {kReduce, kFinish}},
    };

    std::vector<int> totals;
    for (const Case& c : cases) {
        INFO("method " << static_cast<int>(c.method));
        controller.setMethod(c.method);
        controller.solveCube(scrambled);
        REQUIRE(wait_until_solved(controller));
        CHECK(stage_values(controller) == c.stages);
        CHECK(controller.currentCube() == scrambled);
        CHECK_FALSE(controller.solutionContinuesCube());
        totals.push_back(controller.totalMoves());
        CHECK(step_to_end(controller).is_solved());
    }
    // Three methods, three different lengths of solve: each shorter than the
    // one before it.
    REQUIRE(totals.size() == 3);
    CHECK(totals[1] < totals[0]);
    CHECK(totals[2] < totals[1]);
}

TEST_CASE("changing the method before anything is scanned only records it",
          "[qt][controllers][solve-method]") {
    SolveController controller;
    int changes = 0;
    QObject::connect(&controller, &SolveController::methodChanged, [&changes]() { ++changes; });

    controller.setMethod(SolveController::Advanced);
    CHECK(controller.method() == SolveController::Advanced);
    CHECK(changes == 1);
    CHECK_FALSE(controller.isSolving());
    CHECK(controller.totalMoves() == 0);

    // Setting what it already is announces nothing and solves nothing.
    controller.setMethod(SolveController::Advanced);
    CHECK(changes == 1);
    CHECK_FALSE(controller.isSolving());
}

TEST_CASE("changing the method part-way solves on from the cube as it stands",
          "[qt][controllers][solve-method]") {
    SolveController controller;
    int solutions = 0;
    QObject::connect(&controller, &SolveController::solutionChanged,
                     [&solutions]() { ++solutions; });

    const Cube scrambled = random_scramble(17, 100);
    controller.solveCube(scrambled);
    REQUIRE(wait_until_solved(controller));
    REQUIRE(stage_values(controller).size() == 5);
    const int first_solution = solutions;

    for (int i = 0; i < 9; ++i) {
        controller.nextMove();
    }
    const Cube here = controller.currentCube();
    REQUIRE_FALSE(here.is_solved());
    const int beginner_left = controller.totalMoves() - controller.moveIndex();

    controller.setMethod(SolveController::Advanced);
    CHECK(controller.isSolving());  // being worked out, not instantly swapped
    REQUIRE(wait_until_solved(controller));

    // A new solution, of the same cube, starting where the cube stands.
    CHECK(solutions == first_solution + 1);
    CHECK(controller.solutionContinuesCube());
    CHECK(controller.moveIndex() == 0);
    CHECK(controller.currentCube() == here);
    CHECK(stage_values(controller) == std::vector<int>{kReduce, kFinish});
    CHECK(controller.totalMoves() < beginner_left);
    CHECK(controller.totalMoves() <= 26);
    CHECK(step_to_end(controller).is_solved());

    // And on again, from wherever that left it.
    controller.previousMove();
    controller.previousMove();
    const Cube two_back = controller.currentCube();
    controller.setMethod(SolveController::Intermediate);
    REQUIRE(wait_until_solved(controller));
    CHECK(controller.currentCube() == two_back);
    CHECK(stage_values(controller) == std::vector<int>{kCross, kF2l, kOll, kPll});
    CHECK(step_to_end(controller).is_solved());
}

TEST_CASE("changing the method while a solve is still being worked out replaces that solve",
          "[qt][controllers][solve-method]") {
    SolveController controller;

    const Cube scrambled = random_scramble(23, 100);
    controller.solveCube(scrambled);  // the beginner method: the slow one
    REQUIRE(controller.isSolving());
    controller.setMethod(SolveController::Intermediate);
    REQUIRE(wait_until_solved(controller));
    CHECK(stage_values(controller) == std::vector<int>{kCross, kF2l, kOll, kPll});
    CHECK(controller.currentCube() == scrambled);
    CHECK(controller.moveIndex() == 0);
    // A scan whose solve had not landed has no earlier hold to carry on from:
    // this is still "a new cube to solve", not a continuation.
    CHECK_FALSE(controller.solutionContinuesCube());

    // The replaced beginner solve must not land afterwards and undo the swap.
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 400) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    CHECK(stage_values(controller) == std::vector<int>{kCross, kF2l, kOll, kPll});
    CHECK(step_to_end(controller).is_solved());
}

TEST_CASE("a cube scanned after a swap is a new cube, not a continuation",
          "[qt][controllers][solve-method]") {
    SolveController controller;
    controller.solveCube(random_scramble(5, 100));
    REQUIRE(wait_until_solved(controller));
    controller.setMethod(SolveController::Advanced);
    REQUIRE(wait_until_solved(controller));
    REQUIRE(controller.solutionContinuesCube());

    const Cube next = random_scramble(6, 100);
    controller.solveCube(next);
    REQUIRE(wait_until_solved(controller));
    CHECK_FALSE(controller.solutionContinuesCube());
    CHECK(controller.currentCube() == next);
    CHECK(stage_values(controller) == std::vector<int>{kReduce, kFinish});
}
