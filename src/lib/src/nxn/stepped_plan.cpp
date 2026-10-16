#include <cube/nxn/stepped_plan.hpp>

#include <span>
#include <utility>

namespace cube::nxn {

namespace {

// The moves of one step, as a person is shown them.
[[nodiscard]] std::vector<WideMove> moves_of(const Step& step, int size, Display display) {
    if (display == Display::Wide) {
        return group(step.moves, size);
    }
    std::vector<WideMove> moves;
    moves.reserve(step.moves.size());
    for (const LayerMove& move : step.moves) {
        moves.push_back(WideMove{
            .face = move.face,
            .first = move.depth,
            .last = move.depth,
            .turns = move.turns,
        });
    }
    return moves;
}

}  // namespace

SteppedPlan::SteppedPlan() : cubes_{*NCube::solved(3)} {}

SteppedPlan SteppedPlan::from(const NCube& start, const Solution& solution, Display display) {
    SteppedPlan plan;
    plan.cubes_.clear();
    plan.cubes_.push_back(start);

    NCube cube = start;
    for (const Step& step : solution.steps) {
        const StageId id = stage_id(step);
        const int range_start = plan.move_count();
        for (const WideMove& move : moves_of(step, start.size(), display)) {
            cube.apply(layers_of(move));
            plan.moves_.push_back(move);
            plan.stage_per_move_.push_back(id);
            plan.cubes_.push_back(cube);
        }
        plan.stage_ranges_.push_back(
            StageSpan{.id = id, .start_index = range_start, .end_index = plan.move_count()});
    }
    return plan;
}

std::vector<LayerMove> SteppedPlan::layer_turns() const {
    return layers_of(moves_);
}

std::vector<LayerMove> SteppedPlan::layers_at(int index) const {
    return layers_of(moves_.at(static_cast<std::size_t>(index)));
}

Solution SteppedPlan::solution() const {
    Solution out;
    out.steps.reserve(stage_ranges_.size());
    for (const StageSpan& span : stage_ranges_) {
        Step step = step_of(span.id);
        for (int i = span.start_index; i < span.end_index; ++i) {
            const std::vector<LayerMove> turns = layers_at(i);
            step.moves.insert(step.moves.end(), turns.begin(), turns.end());
        }
        out.steps.push_back(std::move(step));
    }
    return out;
}

void SteppedPlan::next() noexcept {
    if (!at_end()) {
        ++index_;
    }
}

void SteppedPlan::previous() noexcept {
    if (!at_start()) {
        --index_;
    }
}

void SteppedPlan::seek(int index) noexcept {
    index_ = std::clamp(index, 0, move_count());
}

int SteppedPlan::core_start() const noexcept {
    for (const StageSpan& span : stage_ranges_) {
        if (is_core(span.id)) {
            return span.start_index;
        }
    }
    return move_count();
}

}  // namespace cube::nxn
