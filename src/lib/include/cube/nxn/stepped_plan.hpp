#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/notation.hpp>
#include <cube/nxn/solution.hpp>
#include <cube/nxn/stage.hpp>

namespace cube::nxn {

// One stage's contiguous slice of a plan's moves: moves at index
// [start_index, end_index) belong to the stage. A stage can be empty.
struct StageSpan {
    StageId id = StageId::Cross;
    int start_index = 0;
    int end_index = 0;

    friend constexpr bool operator==(const StageSpan&, const StageSpan&) = default;
};

// What a move of a plan is.
enum class Display : std::uint8_t {
    // One move per turn of one layer: the library's own solution, as it is.
    // At 3x3 the only choice there is, since no two layers of a 3x3 that a
    // face turn names are next to each other.
    Layers,
    // Adjacent layers turned through the same angle are one move (Rw), as a
    // person would say it: see group() in notation.hpp. Fewer moves, each a
    // block of layer turns.
    Wide,
};

// A solution to follow one move at a time, with the cube cached at every
// position: what a screen needs to show "move N of the solve" - the cube as it
// stands, the move about to happen, how far through each stage - without
// working it out again on every step. This is SteppedSolution for a cube of
// any size: the state is an NCube, the moves are wide moves (one layer, or
// several turned together), and the stages are StageIds.
//
// Deliberately free of Qt and of the screens, so that the controller that
// hands it to QML can stay a thin wrapper and this is tested alone.
class SteppedPlan {
public:
    // A plan of the solved 3x3 with no moves: something consistent to show
    // before the first solve lands.
    SteppedPlan();

    // `start` stepped through `solution`. Each step of the solution is one
    // stage span (an empty step is an empty span), and a move belongs to the
    // span of the step it came from: a block never reaches across two steps.
    // `solution` must be a solution of `start`, or the cubes the plan caches
    // are wherever its moves take them.
    [[nodiscard]] static SteppedPlan from(const NCube& start, const Solution& solution,
                                          Display display = Display::Layers);

    // The cube's size.
    [[nodiscard]] int size() const noexcept { return cubes_.front().size(); }

    [[nodiscard]] int move_count() const noexcept { return static_cast<int>(moves_.size()); }

    // Every move, in order.
    [[nodiscard]] const std::vector<WideMove>& moves() const noexcept { return moves_; }

    // The turns of single layers every move is, in order: what is stored and
    // replayed. Their number is at least move_count() (a wide move is several).
    [[nodiscard]] std::vector<LayerMove> layer_turns() const;

    // The turns of single layers move `index` is.
    [[nodiscard]] std::vector<LayerMove> layers_at(int index) const;

    // The solution this plan was made from: one step per stage span, in order,
    // each with the turns of single layers of its moves. `from` of it, with the
    // plan's display, gives this plan again. This is what is kept of a solve to
    // follow it later without solving it again.
    [[nodiscard]] Solution solution() const;

    // How many moves have been applied to reach the state on show; in
    // [0, move_count()].
    [[nodiscard]] int current_index() const noexcept { return index_; }

    [[nodiscard]] bool at_start() const noexcept { return index_ == 0; }
    [[nodiscard]] bool at_end() const noexcept { return index_ == move_count(); }

    // The move about to be made from the state on show. Only valid when
    // !at_end().
    [[nodiscard]] const WideMove& current_move() const noexcept {
        return moves_.at(static_cast<std::size_t>(index_));
    }

    // Which stage current_move() belongs to. Only valid when !at_end().
    [[nodiscard]] StageId current_stage() const noexcept {
        return stage_per_move_.at(static_cast<std::size_t>(index_));
    }

    // The cube after current_index() moves: what should be drawn.
    [[nodiscard]] const NCube& current_cube() const noexcept {
        return cubes_.at(static_cast<std::size_t>(index_));
    }

    // The cube after `index` moves, wherever current_index() is: for a second
    // view of the same plan (the screen that reviews the solution) that must
    // not move the first. Clamped to [0, move_count()].
    [[nodiscard]] const NCube& cube_at(int index) const noexcept {
        return cubes_.at(static_cast<std::size_t>(std::clamp(index, 0, move_count())));
    }

    // Applies current_move() and advances; a no-op at_end().
    void next() noexcept;

    // Un-applies the previous move; a no-op at_start().
    void previous() noexcept;

    // Puts the state `index` moves in on show, wherever it is now. An index out
    // of range is the nearest one that is not.
    void seek(int index) noexcept;

    // The move at which the stages of the 3x3 the cube has become begin: after
    // the parity, centres and edges of a reduction. 0 for a 3x3, whose plan is
    // the core alone; move_count() for a 2x2, which has no core.
    [[nodiscard]] int core_start() const noexcept;

    // Each stage's [start, end) range of moves, in solving order, so that a
    // screen can show done / current / to come by comparing current_index()
    // with a range.
    [[nodiscard]] const std::vector<StageSpan>& stage_ranges() const noexcept {
        return stage_ranges_;
    }

private:
    std::vector<WideMove> moves_;
    std::vector<StageId> stage_per_move_;
    // cubes_[i] is the state after i moves; always one more than moves_.
    std::vector<NCube> cubes_;
    std::vector<StageSpan> stage_ranges_;
    int index_ = 0;
};

}  // namespace cube::nxn
