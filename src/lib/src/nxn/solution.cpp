#include <cube/nxn/solution.hpp>

#include <cstddef>
#include <utility>

namespace cube::nxn {

std::vector<LayerMove> Solution::moves() const {
    std::vector<LayerMove> all;
    all.reserve(move_count());
    for (const Step& step : steps) {
        all.insert(all.end(), step.moves.begin(), step.moves.end());
    }
    return all;
}

namespace {

struct Kept {
    std::size_t step;
    SliceTurn turn;
};

// Folds `t` into the run of same-axis turns at the end of `kept`, if one of
// them is on its layer: turns of other layers of one axis commute with it, so
// it can be merged with any of them. Returns whether it was.
[[nodiscard]] bool absorb(std::vector<Kept>& kept, SliceTurn t) {
    for (std::size_t back = kept.size(); back-- > 0;) {
        if (kept[back].turn.axis != t.axis) {
            return false;
        }
        if (kept[back].turn.layer != t.layer) {
            continue;
        }
        const int sum = (kept[back].turn.turns + t.turns) % 4;
        if (sum == 0) {
            kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(back));
        } else {
            kept[back].turn.turns = sum;
        }
        return true;
    }
    return false;
}

}  // namespace

void merge_steps(std::vector<Step>& steps, int size) {
    std::vector<Kept> kept;
    for (std::size_t s = 0; s < steps.size(); ++s) {
        for (const LayerMove& move : steps[s].moves) {
            SliceTurn t = to_slice_turn(move, size);
            t.turns = ((t.turns % 4) + 4) % 4;
            if (t.turns != 0 && !absorb(kept, t)) {
                kept.push_back(Kept{.step = s, .turn = t});
            }
        }
    }
    for (Step& step : steps) {
        step.moves.clear();
    }
    for (const Kept& k : kept) {
        steps[k.step].moves.push_back(to_layer_move(k.turn, size));
    }
}

std::vector<LayerMove> merged(std::span<const LayerMove> moves, int size) {
    std::vector<Step> one(1);
    one.front().moves.assign(moves.begin(), moves.end());
    merge_steps(one, size);
    return std::move(one.front().moves);
}

}  // namespace cube::nxn
