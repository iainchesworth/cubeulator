#include <catch2/catch_test_macros.hpp>

#include "solve_controller.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/notation.hpp>
#include <cube/solver/solver.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QMetaProperty>
#include <QStringList>
#include <QVariantMap>

#include <cstddef>
#include <vector>

using cube::solver::Cube;
using cube::solver::MoveCount;
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

    const Cube scrambled = random_scramble(41, MoveCount{100});
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

TEST_CASE("stickersAt reads any position of the solution without moving the solve's",
          "[qt][controllers][review]") {
    SolveController controller;
    controller.solveCube(random_scramble(23, MoveCount{25}));
    REQUIRE(wait_until_solved(controller));
    const int total = controller.totalMoves();
    REQUIRE(total > 2);

    // The solve screen is two turns in; a review of the solution reads
    // positions of its own.
    controller.nextMove();
    controller.nextMove();
    const QVariantMap here = controller.stickers();
    const QVariantMap start = controller.stickersAt(0);
    const QVariantMap end = controller.stickersAt(total);
    CHECK(controller.moveIndex() == 2);
    CHECK(controller.stickers() == here);
    CHECK(controller.stickersAt(2) == here);
    CHECK(start != here);

    // Each position is what stepping there gives.
    while (controller.moveIndex() > 0) {
        controller.previousMove();
    }
    CHECK(controller.stickers() == start);
    for (int index = 0; index <= total; ++index) {
        CAPTURE(index);
        CHECK(controller.stickersAt(index) == controller.stickers());
        controller.nextMove();
    }

    // The solved cube has nine of one colour on every side; outside the
    // solution is its nearest end.
    CHECK(controller.stickersAt(total) == end);
    CHECK(controller.stickersAt(total + 7) == end);
    CHECK(controller.stickersAt(-3) == start);
    for (const QVariant& side : end) {
        const QVariantList colours = side.toList();
        REQUIRE(colours.size() == 9);
        for (const QVariant& colour : colours) {
            CHECK(colour == colours.at(4));
        }
    }
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

    const Cube scrambled = random_scramble(17, MoveCount{100});
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

    const Cube scrambled = random_scramble(23, MoveCount{100});
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
    controller.solveCube(random_scramble(5, MoveCount{100}));
    REQUIRE(wait_until_solved(controller));
    controller.setMethod(SolveController::Advanced);
    REQUIRE(wait_until_solved(controller));
    REQUIRE(controller.solutionContinuesCube());

    const Cube next = random_scramble(6, MoveCount{100});
    controller.solveCube(next);
    REQUIRE(wait_until_solved(controller));
    CHECK_FALSE(controller.solutionContinuesCube());
    CHECK(controller.currentCube() == next);
    CHECK(stage_values(controller) == std::vector<int>{kReduce, kFinish});
}

TEST_CASE("a 3x3 solve says its size, turns only outer layers and has no reduction to skip",
          "[qt][controllers]") {
    SolveController controller;
    CHECK(controller.size() == 3);
    controller.solveCube(random_scramble(8, MoveCount{60}));
    REQUIRE(wait_until_solved(controller));
    CHECK(controller.size() == 3);
    for (const QVariant& entry : controller.moves()) {
        CHECK(entry.toMap().value(QStringLiteral("first")).toInt() == 0);
        CHECK(entry.toMap().value(QStringLiteral("last")).toInt() == 0);
    }
    CHECK(controller.currentMoveFirst() == 0);
    CHECK(controller.currentMoveLast() == 0);
    CHECK(controller.coreStartIndex() == 0);
    CHECK_FALSE(controller.canSkipToCore());
    controller.skipToCore();
    CHECK(controller.moveIndex() == 0);
}

TEST_CASE("a 5x5 solve can skip its reduction", "[qt][controllers]") {
    SolveController controller;
    const cube::nxn::NCube scrambled =
        cube::nxn::random_scramble({.size = 5, .seed = 12, .moves = 90});
    controller.solveCube(scrambled);
    REQUIRE(wait_until_solved(controller));
    CHECK(controller.size() == 5);
    REQUIRE(controller.totalMoves() > 0);

    // A move says which layers it turns, and the first move is the one about to happen.
    for (const QVariant& entry : controller.moves()) {
        const QVariantMap move = entry.toMap();
        const int first = move.value(QStringLiteral("first")).toInt();
        const int last = move.value(QStringLiteral("last")).toInt();
        CHECK(first >= 0);
        CHECK(last >= first);
        CHECK(last < 5);
    }
    const QVariantMap front = controller.moves().front().toMap();
    CHECK(controller.currentMoveFirst() == front.value(QStringLiteral("first")).toInt());
    CHECK(controller.currentMoveLast() == front.value(QStringLiteral("last")).toInt());

    // The 3x3 stages begin after the reduction, and a person can jump there.
    const int core = controller.coreStartIndex();
    CHECK(core > 0);
    CHECK(core < controller.totalMoves());
    CHECK(controller.canSkipToCore());
    controller.skipToCore();
    CHECK(controller.moveIndex() == core);
    CHECK_FALSE(controller.canSkipToCore());
    CHECK(controller.plan().current_cube().is_reduced());
    // Where they are the stickers are those of the cube there, and the stage
    // is one of the 3x3's.
    CHECK(controller.stickers() == controller.stickersAt(core));
    CHECK(controller.currentStageIndex() <= kFinish);

    // Back to the start, and on to the end, where it is solved.
    controller.goToMove(0);
    CHECK(controller.atStart());
    CHECK(controller.canSkipToCore());
    controller.goToMove(controller.totalMoves() + 20);
    CHECK(controller.atEnd());
    CHECK(controller.plan().current_cube().is_solved());
    controller.skipToCore();
    CHECK(controller.atEnd());
}

TEST_CASE("a bigger cube's adjacent layers turned together are one move", "[qt][controllers]") {
    // Seed 1 of a 4x4 solved the fewest-moves way has one: 136 layer turns,
    // 134 moves. Wide moves are rare in real solutions, a fraction of a percent.
    SolveController controller;
    controller.setMethod(SolveController::Advanced);
    const cube::nxn::NCube scrambled =
        cube::nxn::random_scramble({.size = 4, .seed = 1, .moves = 72});
    controller.solveCube(scrambled);
    REQUIRE(wait_until_solved(controller));
    int wide = 0;
    int layer_turns = 0;
    for (const QVariant& entry : controller.moves()) {
        const QVariantMap move = entry.toMap();
        const int layers = move.value(QStringLiteral("last")).toInt() -
                           move.value(QStringLiteral("first")).toInt() + 1;
        layer_turns += layers;
        wide += layers > 1 ? 1 : 0;
    }
    CHECK(wide > 0);
    CHECK(controller.totalMoves() < layer_turns);
    CHECK(static_cast<std::size_t>(layer_turns) == controller.plan().layer_turns().size());
    // Following the moves ends solved all the same.
    controller.goToMove(controller.totalMoves());
    CHECK(controller.plan().current_cube().is_solved());
}

TEST_CASE("a 2x2 and a 4x4 say their size, and only the 4x4 has a reduction to skip",
          "[qt][controllers]") {
    for (const int size : {2, 4}) {
        SolveController controller;
        controller.solveCube(cube::nxn::random_scramble({.size = size, .seed = 3, .moves = 60}));
        REQUIRE(wait_until_solved(controller));
        INFO("size " << size);
        CHECK(controller.size() == size);
        CHECK(controller.canSkipToCore() == (size == 4));
        if (size == 2) {
            CHECK(controller.coreStartIndex() == controller.totalMoves());
        }
        controller.goToMove(controller.totalMoves());
        CHECK(controller.plan().current_cube().is_solved());
    }
}

TEST_CASE("a side is named by the colour the cube is meant to have there, at any size",
          "[qt][controllers]") {
    // A solved cube of any size has each side its own colour; the names are the
    // colours the screens already use (colour_name.hpp).
    for (const int size : {2, 3, 4, 5, 7}) {
        SolveController controller;
        controller.solveCube(*cube::nxn::NCube::solved(size));
        REQUIRE(wait_until_solved(controller));
        INFO("size " << size);
        const QVariantMap colours = controller.sideColours();
        CHECK(colours.size() == 6);
        QStringList seen;
        for (const char* side : {"U", "D", "L", "R", "F", "B"}) {
            REQUIRE(colours.contains(QString::fromLatin1(side)));
            seen.append(colours.value(QString::fromLatin1(side)).toString());
        }
        seen.removeDuplicates();
        CHECK(seen.size() == 6);  // six sides, six colours
        if (size == 3) {
            // The 3x3's own middle stickers say the same.
            const QVariantMap stickers = controller.stickers();
            for (const QString& side : colours.keys()) {
                CHECK(colours.value(side) == stickers.value(side).toList().at(4));
            }
        }
    }

    // A cube scrambled with middle layers has moved its middles; a side is still named
    // by the colour that is on it.
    SolveController controller;
    const cube::nxn::NCube scrambled =
        cube::nxn::random_scramble({.size = 5, .seed = 9, .moves = 60});
    controller.solveCube(scrambled);
    REQUIRE(wait_until_solved(controller));
    const QVariantMap colours = controller.sideColours();
    const QVariantMap stickers = controller.stickers();
    for (const QString& side : colours.keys()) {
        CHECK(colours.value(side) == stickers.value(side).toList().at(12));  // the 5x5's middle
    }
}

TEST_CASE("a scramble in notation is solved at any size", "[qt][controllers]") {
    SolveController controller;
    struct Scramble {
        int size;
        const char* text;
    };
    for (const Scramble& scramble :
         {Scramble{.size = 2, .text = "R U R' U' F2 R2"},
          Scramble{.size = 3, .text = "R U R' U' L2 D F' B2"},
          Scramble{.size = 4, .text = "R U 2R' Fw 3Fw' Uw2 L' D2 2-3B"},
          Scramble{.size = 5, .text = "R U R' U' 2R F2 3Rw' 2-3L D2 Bw x"}}) {
        INFO("size " << scramble.size << ": " << scramble.text);
        const QVariantMap started =
            controller.solveScramble(scramble.size, QString::fromLatin1(scramble.text));
        REQUIRE(started.value("ok").toBool());
        CHECK(started.value("moves").toInt() > 0);
        CHECK(started.value("layerTurns").toInt() >= started.value("moves").toInt());
        CHECK(controller.isSolving());
        REQUIRE(wait_until_solved(controller));
        CHECK(controller.size() == scramble.size);

        // The cube it solves is the notation made of a solved cube.
        auto expected = *cube::nxn::NCube::solved(scramble.size);
        expected.apply(cube::nxn::layers_of(*cube::nxn::parse(scramble.text, scramble.size)));
        CHECK(controller.plan().cube_at(0) == expected);
        controller.goToMove(controller.totalMoves());
        CHECK(controller.plan().current_cube().is_solved());
    }
}

TEST_CASE("a scramble that is not one says why, where, and starts nothing", "[qt][controllers]") {
    SolveController controller;
    struct Refusal {
        int size;
        const char* text;
        const char* error;
        int offset;
        const char* token;
    };
    for (const Refusal& refusal : {
             Refusal{
                 .size = 3, .text = "R U Q F", .error = "unknownMove", .offset = 4, .token = "Q"},
             Refusal{
                 .size = 3, .text = "R U F3", .error = "unknownMove", .offset = 4, .token = "F3"},
             Refusal{.size = 4,
                     .text = "R 5L U",
                     .error = "layerOutOfRange",
                     .offset = 2,
                     .token = "5L"},
             Refusal{.size = 3,
                     .text = "R 4Rw",
                     .error = "layerOutOfRange",
                     .offset = 2,
                     .token = "4Rw"},
             Refusal{.size = 3, .text = "", .error = "empty", .offset = 0, .token = ""},
             Refusal{.size = 3, .text = "   ", .error = "empty", .offset = 0, .token = ""},
             Refusal{.size = 3, .text = "R R'", .error = "alreadySolved", .offset = 0, .token = ""},
             Refusal{
                 .size = 3, .text = "x y2 z'", .error = "alreadySolved", .offset = 0, .token = ""},
             Refusal{.size = 1, .text = "R", .error = "unsupportedSize", .offset = 0, .token = ""},
             Refusal{.size = 10, .text = "R", .error = "unsupportedSize", .offset = 0, .token = ""},
         }) {
        INFO("size " << refusal.size << ": '" << refusal.text << "'");
        const QVariantMap result =
            controller.solveScramble(refusal.size, QString::fromLatin1(refusal.text));
        CHECK_FALSE(result.value("ok").toBool());
        CHECK(result.value("error").toString() == QString::fromLatin1(refusal.error));
        CHECK(result.value("offset").toInt() == refusal.offset);
        CHECK(result.value("token").toString() == QString::fromLatin1(refusal.token));
        CHECK_FALSE(controller.isSolving());
    }
    // Text that is not Latin-1 is no move, at its own place in the text.
    const QVariantMap strange = controller.solveScramble(3, QString::fromUtf8("R U \u4e2d F"));
    CHECK(strange.value("error").toString() == "unknownMove");
    CHECK(strange.value("offset").toInt() == 4);
    CHECK_FALSE(controller.isSolving());
}
