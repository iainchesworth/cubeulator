#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/cube3.hpp>
#include <cube/nxn/notation.hpp>
#include <cube/nxn/stage.hpp>
#include <cube/nxn/stepped_plan.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/solver/stepped_solution.hpp>

// SteppedPlan is SteppedSolution for a cube of any size. At 3x3 it must be the
// same thing the app has always stepped through - the same moves, the same
// stages, the same cube at every position - and above 3x3 it must be a plan
// that ends solved, whose stages are the solution's.

namespace {

using cube::nxn::Display;
using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::nxn::SteppedPlan;
using cube::nxn::WideMove;
using cube::solver::Cube;
using cube::solver::MoveCount;
using cube::solver::SolveMethod;
using cube::solver::SteppedSolution;

constexpr std::array<SolveMethod, 3> kMethods = {
    SolveMethod::Beginner,
    SolveMethod::Intermediate,
    SolveMethod::Advanced,
};

// A 3x3 as the app holds it, read back out of a plan's cube.
Cube cube3_of(const NCube& cube) {
    const auto read = cube.to_cube3_as_held();
    REQUIRE(read.has_value());
    return *read;
}

// A plan of a cube of `size`, solved by `method`.
struct Planned {
    NCube start;
    cube::nxn::Solution solution;
};

Planned planned(int size, unsigned seed, SolveMethod method = SolveMethod::Advanced) {
    // A 3x3 as the app holds one: scrambled by face turns, so its centres stay
    // where the 3x3 solvers expect them (a middle-layer turn would move them).
    NCube start =
        size == 3
            ? NCube::from_cube3(cube::solver::random_scramble(seed, MoveCount{30}))
            : cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 40 + (8 * size)});
    auto solution = cube::nxn::solve_held(start, method);
    REQUIRE(solution.has_value());
    return Planned{.start = std::move(start), .solution = std::move(*solution)};
}

// What every plan keeps, whatever the cube or the display: the spans tile the
// moves in solving order, one per step of the solution; each move's stage is
// the stage of the span it falls in; the cubes are one more than the moves;
// and walking all the way through and all the way back arrives at the start.
void require_well_formed(SteppedPlan plan, const cube::nxn::Solution& solution) {
    const auto& spans = plan.stage_ranges();
    REQUIRE(spans.size() == solution.steps.size());
    int expected_start = 0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        CAPTURE(i);
        CHECK(spans.at(i).id == cube::nxn::stage_id(solution.steps.at(i)));
        CHECK(spans.at(i).start_index == expected_start);
        CHECK(spans.at(i).end_index >= spans.at(i).start_index);
        expected_start = spans.at(i).end_index;
    }
    CHECK(expected_start == plan.move_count());
    CHECK(plan.moves().size() == static_cast<std::size_t>(plan.move_count()));

    const NCube start = plan.current_cube();
    for (const auto& span : spans) {
        for (int index = span.start_index; index < span.end_index; ++index) {
            while (plan.current_index() < index) {
                plan.next();
            }
            CHECK(plan.current_stage() == span.id);
        }
    }
    while (!plan.at_end()) {
        plan.next();
    }
    CHECK(plan.current_cube() == plan.cube_at(plan.move_count()));
    while (!plan.at_start()) {
        plan.previous();
    }
    CHECK(plan.current_cube() == start);
}

}  // namespace

TEST_CASE("a plan with no moves is a solved 3x3 at both ends", "[nxn][plan]") {
    const SteppedPlan plan;
    CHECK(plan.size() == 3);
    CHECK(plan.move_count() == 0);
    CHECK(plan.at_start());
    CHECK(plan.at_end());
    CHECK(plan.current_cube().is_solved());
    CHECK(plan.stage_ranges().empty());

    const NCube cube = *NCube::solved(5);
    const SteppedPlan five = SteppedPlan::from(cube, cube::nxn::Solution{});
    CHECK(five.size() == 5);
    CHECK(five.current_cube() == cube);
    CHECK(five.at_end());
}

namespace {

// The moves and the stages of `plan` are `old`'s, one for one.
void require_same_moves_and_stages(const SteppedPlan& plan, const SteppedSolution& old) {
    REQUIRE(plan.move_count() == old.move_count());
    for (std::size_t i = 0; i < old.moves().size(); ++i) {
        const WideMove& move = plan.moves().at(i);
        CHECK(move.face == old.moves().at(i).face);
        CHECK(move.turns == old.moves().at(i).turns);
        CHECK(move.first == 0);
        CHECK(move.last == 0);
    }
    REQUIRE(plan.stage_ranges().size() == old.stage_ranges().size());
    for (std::size_t i = 0; i < old.stage_ranges().size(); ++i) {
        const auto& theirs = old.stage_ranges().at(i);
        const auto& ours = plan.stage_ranges().at(i);
        CHECK(static_cast<int>(ours.id) == static_cast<int>(theirs.stage));
        CHECK(ours.start_index == theirs.start_index);
        CHECK(ours.end_index == theirs.end_index);
    }
}

// Stepped forwards to the end and back to the start together, the two show the
// same cube, move and stage at every position.
void require_same_walk(SteppedPlan plan, SteppedSolution old) {
    for (int i = 0; i <= old.move_count(); ++i) {
        CHECK(cube3_of(plan.cube_at(i)) == old.cube_at(i));
    }
    while (!old.at_end()) {
        CHECK(static_cast<int>(plan.current_stage()) == static_cast<int>(old.current_stage()));
        CHECK(plan.current_move().face == old.current_move().face);
        CHECK(cube3_of(plan.current_cube()) == old.current_cube());
        old.next();
        plan.next();
    }
    CHECK(plan.at_end());
    CHECK(cube3_of(plan.current_cube()).is_solved());
    while (!old.at_start()) {
        old.previous();
        plan.previous();
        CHECK(cube3_of(plan.current_cube()) == old.current_cube());
    }
    CHECK(plan.at_start());
}

}  // namespace

TEST_CASE("at 3x3 a plan is exactly the solution the app has always stepped through",
          "[nxn][plan][slow]") {
    for (const SolveMethod method : kMethods) {
        for (unsigned seed = 1; seed <= 6; ++seed) {
            const Cube scrambled = cube::solver::random_scramble(seed, MoveCount{25});
            const auto theirs = cube::solver::solve(scrambled, method);
            REQUIRE(theirs.has_value());
            const SteppedSolution old = SteppedSolution::from(scrambled, *theirs);

            const auto ours = cube::nxn::solve_held(NCube::from_cube3(scrambled), method);
            REQUIRE(ours.has_value());
            for (const Display display : {Display::Layers, Display::Wide}) {
                const SteppedPlan plan =
                    SteppedPlan::from(NCube::from_cube3(scrambled), *ours, display);
                INFO("method " << static_cast<int>(method) << " seed " << seed << " display "
                               << static_cast<int>(display));
                require_same_moves_and_stages(plan, old);
                require_same_walk(plan, old);
            }
        }
    }
}

TEST_CASE("a plan stops at its ends", "[nxn][plan]") {
    const auto [start, solution] = planned(3, 4U, SolveMethod::Beginner);
    SteppedPlan plan = SteppedPlan::from(start, solution);
    plan.previous();
    CHECK(plan.current_index() == 0);
    for (int i = 0; i < plan.move_count() + 5; ++i) {
        plan.next();
    }
    CHECK(plan.current_index() == plan.move_count());
    CHECK(plan.at_end());
    // A position out of range is the nearest one that is not.
    CHECK(plan.cube_at(-3) == start);
    CHECK(plan.cube_at(plan.move_count() + 7) == plan.cube_at(plan.move_count()));
    CHECK(plan.cube_at(plan.move_count()).is_solved());
}

TEST_CASE("a plan of a bigger cube has the solution's stages and ends solved", "[nxn][plan]") {
    for (const int size : {2, 4, 5, 6, 7}) {
        const auto [start, solution] = planned(size, 7U);
        SteppedPlan plan = SteppedPlan::from(start, solution);
        INFO("size " << size);
        require_well_formed(plan, solution);
        CHECK(plan.size() == size);
        CHECK(plan.move_count() == static_cast<int>(solution.move_count()));
        CHECK(plan.cube_at(0) == start);
        CHECK(plan.cube_at(plan.move_count()).is_solved());
        // As turns of single layers, it is the solution.
        CHECK(plan.layer_turns() == solution.moves());
        CHECK(plan.layers_at(0) == std::vector<LayerMove>{solution.moves().front()});
    }
}

TEST_CASE("the stages of a reduction come in order, and the 3x3's are the core's", "[nxn][plan]") {
    using cube::nxn::StageId;
    const auto [start, solution] = planned(5, 3U, SolveMethod::Beginner);
    const SteppedPlan plan = SteppedPlan::from(start, solution);

    std::vector<StageId> ids;
    for (const auto& span : plan.stage_ranges()) {
        ids.push_back(span.id);
    }
    // Reduction first, then every stage of the 3x3 the cube has become.
    const std::vector<StageId> core_ids = {
        StageId::Cross, StageId::WhiteCorners, StageId::MiddleEdges, StageId::Oll, StageId::Pll,
    };
    REQUIRE(ids.size() >= core_ids.size());
    bool core_seen = false;
    for (const StageId id : ids) {
        if (cube::nxn::is_core(id)) {
            core_seen = true;
        } else {
            CHECK_FALSE(core_seen);  // nothing of the reduction comes after the core
        }
    }
    const std::vector<StageId> tail(ids.end() - static_cast<std::ptrdiff_t>(core_ids.size()),
                                    ids.end());
    CHECK(tail == core_ids);
    // A 5x5 reduction has centres and edges to do before the core.
    CHECK(std::ranges::find(ids, StageId::Centres) != ids.end());
    CHECK(std::ranges::find(ids, StageId::Edges) != ids.end());
}

TEST_CASE("wide moves are fewer moves of the same layer turns", "[nxn][plan]") {
    int fewer = 0;
    for (const int size : {4, 5, 6, 7}) {
        for (unsigned seed = 1; seed <= 4; ++seed) {
            const auto [start, solution] = planned(size, seed);
            const SteppedPlan layers = SteppedPlan::from(start, solution, Display::Layers);
            SteppedPlan wide = SteppedPlan::from(start, solution, Display::Wide);
            INFO("size " << size << " seed " << seed);
            require_well_formed(wide, solution);
            CHECK(wide.move_count() <= layers.move_count());
            fewer += wide.move_count() < layers.move_count() ? 1 : 0;
            // The same number of turns of single layers, and the same end.
            CHECK(wide.layer_turns().size() == solution.move_count());
            CHECK(wide.cube_at(wide.move_count()).is_solved());
            // A move is its layers: stepping a move applies exactly those turns.
            while (!wide.at_end()) {
                NCube stepped = wide.current_cube();
                stepped.apply(wide.layers_at(wide.current_index()));
                wide.next();
                CHECK(wide.current_cube() == stepped);
            }
        }
    }
    // Wide moves do turn up in real solutions, so this exercises something.
    CHECK(fewer > 0);
}

TEST_CASE("every stage has a key, and the 3x3 solver's stages keep their numbers", "[nxn][plan]") {
    using cube::nxn::StageId;
    std::set<std::string_view> keys;
    for (std::uint8_t value = 0; value <= static_cast<std::uint8_t>(StageId::Corners); ++value) {
        const auto id = static_cast<StageId>(value);
        const std::string_view key = cube::nxn::key(id);
        CHECK_FALSE(key.empty());
        keys.insert(key);
        CHECK(cube::nxn::is_core(id) == (value <= static_cast<std::uint8_t>(StageId::Finish)));
    }
    CHECK(keys.size() == 12);

    // A 3x3 solution's steps map by number: what the solve screen indexes by.
    const Cube scrambled = cube::solver::random_scramble(2U, MoveCount{25});
    const auto solution = cube::solver::solve(scrambled, SolveMethod::Intermediate);
    REQUIRE(solution.has_value());
    for (const auto& step : solution->steps) {
        CHECK(static_cast<int>(cube::nxn::stage_id(step.stage)) == static_cast<int>(step.stage));
    }
}

TEST_CASE("a plan can be put at any position, and says where the 3x3's stages begin",
          "[nxn][plan]") {
    for (const int size : {2, 3, 4, 5, 7}) {
        INFO("size " << size);
        const auto [start, solution] = planned(size, 5U, SolveMethod::Beginner);
        SteppedPlan plan = SteppedPlan::from(start, solution);

        // Anywhere in the plan is the cube cached there; out of range is the nearest end.
        for (const int index :
             {0, 1, plan.move_count() / 2, plan.move_count() - 1, plan.move_count()}) {
            plan.seek(index);
            CHECK(plan.current_index() == index);
            CHECK(plan.current_cube() == plan.cube_at(index));
        }
        plan.seek(-4);
        CHECK(plan.at_start());
        plan.seek(plan.move_count() + 9);
        CHECK(plan.at_end());
        plan.seek(plan.move_count() / 2);
        plan.next();
        CHECK(plan.current_index() == (plan.move_count() / 2) + 1);

        // The core begins where the first of its stages does: at the start of a
        // 3x3, which is the core alone, after the reduction of a bigger cube, and
        // nowhere for a 2x2, which has none.
        if (size == 3) {
            CHECK(plan.core_start() == 0);
        } else if (size == 2) {
            CHECK(plan.core_start() == plan.move_count());
        } else {
            const int begins = plan.core_start();
            CHECK(begins > 0);
            CHECK(begins < plan.move_count());
            // Everything before it is the reduction, and it is not yet a reduced cube
            // until then: the state at that move is one the 3x3 stages start from.
            for (const auto& span : plan.stage_ranges()) {
                if (span.start_index < begins) {
                    CHECK_FALSE(cube::nxn::is_core(span.id));
                }
            }
            CHECK(plan.cube_at(begins).is_reduced());
            CHECK_FALSE(plan.cube_at(0).is_reduced());
            plan.seek(begins);
            CHECK(cube::nxn::is_core(plan.current_stage()));
        }
    }
}

TEST_CASE("a plan gives back the solution it was made from, and every stage can be rebuilt",
          "[nxn][plan]") {
    using cube::nxn::StageId;
    // The step of a stage is the stage's, and nothing else.
    for (std::uint8_t value = 0; value <= static_cast<std::uint8_t>(StageId::Corners); ++value) {
        const auto id = static_cast<StageId>(value);
        const cube::nxn::Step step = cube::nxn::step_of(id);
        CHECK(cube::nxn::stage_id(step) == id);
        CHECK(step.moves.empty());
    }

    for (const int size : {2, 3, 4, 5, 6}) {
        for (const Display display : {Display::Layers, Display::Wide}) {
            INFO("size " << size << (display == Display::Wide ? " wide" : " layers"));
            const auto [start, solution] = planned(size, 9U, SolveMethod::Beginner);
            const SteppedPlan plan = SteppedPlan::from(start, solution, display);

            // One step per stage span, in order, with the turns of its moves.
            const cube::nxn::Solution again = plan.solution();
            REQUIRE(again.steps.size() == plan.stage_ranges().size());
            for (std::size_t i = 0; i < again.steps.size(); ++i) {
                CHECK(cube::nxn::stage_id(again.steps.at(i)) == plan.stage_ranges().at(i).id);
            }
            // The turns are the plan's own, in its order: a wide move gathers its layers
            // from the nearest, which is the solution's order only where it was that.
            CHECK(again.moves() == plan.layer_turns());
            if (display == Display::Layers) {
                CHECK(again.moves() == solution.moves());
            }

            // And made into a plan again, it is that plan.
            const SteppedPlan rebuilt = SteppedPlan::from(start, again, display);
            CHECK(rebuilt.moves() == plan.moves());
            CHECK(rebuilt.stage_ranges() == plan.stage_ranges());
            CHECK(rebuilt.cube_at(rebuilt.move_count()) == plan.cube_at(plan.move_count()));
        }
    }
}
