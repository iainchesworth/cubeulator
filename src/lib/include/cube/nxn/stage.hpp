#pragma once

#include <cstdint>
#include <string_view>

#include <cube/nxn/solution.hpp>
#include <cube/solver/solver.hpp>

// The stage of a solution as a person is told about it.
//
// The library has two layers of stage: what the reduction is doing (`Stage`:
// parity, centres, edges, or the core) and, inside the core, which of the 3x3
// solver's own stages (`solver::Stage`). A screen wants one list, "where am I
// in this solve", so a StageId flattens the two: a core step is its 3x3 stage,
// and the reduction's steps are their own.
//
// The 3x3 solver's stages keep the values `solver::Stage` gives them, because
// the solve screen words them by number and a 3x3 must read as it always did;
// a solution of a 3x3 is the core list alone, so nothing changes for it. The
// reduction's stages follow. A stage has no words here: the screens translate,
// from the key.

namespace cube::nxn {

enum class StageId : std::uint8_t {
    // The 3x3 solver's, valued as solver::Stage.
    Cross = 0,
    WhiteCorners = 1,
    MiddleEdges = 2,
    Oll = 3,
    Pll = 4,
    F2l = 5,
    Reduce = 6,
    Finish = 7,
    // The reduction's.
    Parity = 8,
    Centres = 9,
    Edges = 10,
    // A 2x2, solved as one search.
    Corners = 11,
};

static_assert(static_cast<std::uint8_t>(StageId::Cross) ==
              static_cast<std::uint8_t>(solver::Stage::Cross));
static_assert(static_cast<std::uint8_t>(StageId::WhiteCorners) ==
              static_cast<std::uint8_t>(solver::Stage::WhiteCorners));
static_assert(static_cast<std::uint8_t>(StageId::MiddleEdges) ==
              static_cast<std::uint8_t>(solver::Stage::MiddleEdges));
static_assert(static_cast<std::uint8_t>(StageId::Oll) ==
              static_cast<std::uint8_t>(solver::Stage::Oll));
static_assert(static_cast<std::uint8_t>(StageId::Pll) ==
              static_cast<std::uint8_t>(solver::Stage::Pll));
static_assert(static_cast<std::uint8_t>(StageId::F2l) ==
              static_cast<std::uint8_t>(solver::Stage::F2l));
static_assert(static_cast<std::uint8_t>(StageId::Reduce) ==
              static_cast<std::uint8_t>(solver::Stage::Reduce));
static_assert(static_cast<std::uint8_t>(StageId::Finish) ==
              static_cast<std::uint8_t>(solver::Stage::Finish));

[[nodiscard]] constexpr StageId stage_id(solver::Stage stage) noexcept {
    return static_cast<StageId>(static_cast<std::uint8_t>(stage));
}

// The stage a step of a solution belongs to.
[[nodiscard]] constexpr StageId stage_id(const Step& step) noexcept {
    switch (step.stage) {
        case Stage::Parity:
            return StageId::Parity;
        case Stage::Centres:
            return StageId::Centres;
        case Stage::Edges:
            return StageId::Edges;
        case Stage::Corners:
            return StageId::Corners;
        case Stage::Core:
            return stage_id(step.core_stage);
    }
    return StageId::Cross;
}

// A step with no moves, of the stage: the other way from stage_id(const Step&),
// for building a solution back from the stages and the moves that were kept.
[[nodiscard]] inline Step step_of(StageId id) {
    Step step;
    switch (id) {
        case StageId::Parity:
            step.stage = Stage::Parity;
            break;
        case StageId::Centres:
            step.stage = Stage::Centres;
            break;
        case StageId::Edges:
            step.stage = Stage::Edges;
            break;
        case StageId::Corners:
            step.stage = Stage::Corners;
            break;
        case StageId::Cross:
        case StageId::WhiteCorners:
        case StageId::MiddleEdges:
        case StageId::Oll:
        case StageId::Pll:
        case StageId::F2l:
        case StageId::Reduce:
        case StageId::Finish:
            step.stage = Stage::Core;
            step.core_stage = static_cast<solver::Stage>(static_cast<std::uint8_t>(id));
            break;
    }
    return step;
}

// Whether the stage is one of the 3x3 solver's: the part of a solve of a big
// cube that is, once reduced, an ordinary 3x3 solve.
[[nodiscard]] constexpr bool is_core(StageId id) noexcept {
    return static_cast<std::uint8_t>(id) <= static_cast<std::uint8_t>(StageId::Finish);
}

// A stable name for the stage, for the screens to translate by: lower case,
// hyphenated, never changed once released.
[[nodiscard]] constexpr std::string_view key(StageId id) noexcept {
    switch (id) {
        case StageId::Cross:
            return "cross";
        case StageId::WhiteCorners:
            return "white-corners";
        case StageId::MiddleEdges:
            return "middle-edges";
        case StageId::Oll:
            return "oll";
        case StageId::Pll:
            return "pll";
        case StageId::F2l:
            return "f2l";
        case StageId::Reduce:
            return "reduce";
        case StageId::Finish:
            return "finish";
        case StageId::Parity:
            return "parity";
        case StageId::Centres:
            return "centres";
        case StageId::Edges:
            return "edges";
        case StageId::Corners:
            return "corners";
    }
    return "";
}

}  // namespace cube::nxn
