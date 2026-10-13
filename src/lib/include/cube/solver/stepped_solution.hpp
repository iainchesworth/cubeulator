#pragma once

#include <vector>

#include <cube/solver/solver.hpp>

namespace cube::solver {

// One stage's contiguous slice of the flattened move list: moves at index
// [start_index, end_index) belong to `stage`.
struct StageRange {
    Stage stage;
    int start_index = 0;
    int end_index = 0;
};

// A Solution flattened into individual moves and steppable one at a time,
// with the Cube state cached at every position - what a UI needs to show
// "move N of the solve" (current stickers, the move about to happen)
// without recomputing from scratch on every step. Deliberately kept
// GUI-free and Catch2-testable so the Qt bridge (SolveController) can stay
// a thin forwarding wrapper - see the solve-visualization plan.
class SteppedSolution {
public:
    [[nodiscard]] static SteppedSolution from(const Cube& start, const Solution& solution);

    [[nodiscard]] int move_count() const noexcept { return static_cast<int>(moves_.size()); }

    // How many moves have been applied to reach the currently-displayed
    // state; in [0, move_count()].
    [[nodiscard]] int current_index() const noexcept { return index_; }

    [[nodiscard]] bool at_start() const noexcept { return index_ == 0; }
    [[nodiscard]] bool at_end() const noexcept { return index_ == move_count(); }

    // The move about to be performed from the current state. Only valid
    // when !at_end().
    [[nodiscard]] Move current_move() const noexcept {
        return moves_[static_cast<std::size_t>(index_)];
    }

    // Which stage current_move() belongs to. Only valid when !at_end().
    [[nodiscard]] Stage current_stage() const noexcept {
        return stage_per_move_[static_cast<std::size_t>(index_)];
    }

    // The cube state after current_index() moves have been applied - what
    // should currently be drawn.
    [[nodiscard]] const Cube& current_cube() const noexcept {
        return cubes_[static_cast<std::size_t>(index_)];
    }

    // Applies current_move() and advances; a no-op at_end().
    void next() noexcept;

    // Un-applies the previous move; a no-op at_start().
    void previous() noexcept;

    // Each stage's contiguous [start, end) range within the flat move
    // list, in solving order - lets a UI highlight "done"/"current" stages
    // by comparing current_index() against a range instead of needing one
    // entry per stage-sized macro-step (see AppState.qml's old, per-stage
    // stepping model, which this per-move model replaces).
    [[nodiscard]] const std::vector<StageRange>& stage_ranges() const noexcept {
        return stage_ranges_;
    }

private:
    std::vector<Move> moves_;
    std::vector<Stage> stage_per_move_;
    std::vector<Cube>
        cubes_;  // cubes_[i] = state after i moves; cubes_.size() == moves_.size() + 1
    std::vector<StageRange> stage_ranges_;
    int index_ = 0;
};

}  // namespace cube::solver
