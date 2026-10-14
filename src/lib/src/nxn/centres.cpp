#include "centres.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cube::nxn::detail {

namespace {

using Move = std::pair<std::uint16_t, std::uint16_t>;

// A short word and what it does to the stickers that matter to this stage:
// (to, from) says that after the word the sticker at `to` is the one that was
// at `from`. Only centre and face-centre stickers are listed; what a word does
// to anything else is the next stage's business.
struct Action {
    Word word;
    std::vector<Move> moves;
};

// A way to find, among all the actions, those that change a given sticker.
struct Handle {
    std::uint32_t action;
    std::uint16_t from;
};

struct ActionSet {
    std::vector<Action> actions;
    // actions[0, conjugable) are short enough to be done after a setup turn;
    // the pure cycles after them are not.
    std::size_t conjugable = 0;
    std::vector<std::vector<Handle>> changing;  // by the sticker that changes
    std::vector<SliceTurn> turns;
};

[[nodiscard]] bool is_outer(SliceTurn t, int size) noexcept {
    return t.layer == 0 || t.layer == size - 1;
}

// The relevant part of a permutation: for every centre or face-centre sticker
// it moves, where its new content came from.
[[nodiscard]] std::vector<Move> relevant_moves(const Permutation& gather,
                                               const std::vector<bool>& relevant) {
    std::vector<Move> moves;
    for (std::size_t i = 0; i < gather.size(); ++i) {
        if (relevant.at(i) && gather.at(i) != i) {
            moves.emplace_back(static_cast<std::uint16_t>(i), gather.at(i));
        }
    }
    return moves;
}

class ActionSetBuilder {
public:
    static ActionSet build(int size) {
        const Geometry& geometry = Geometry::of(size);
        ActionSetBuilder builder(geometry);
        builder.add_turns_and_commutators();
        builder.add_pure_cycles(CycleLibrary::of(size));
        return builder.finish();
    }

private:
    explicit ActionSetBuilder(const Geometry& geometry)
        : geometry_(&geometry), relevant_(geometry.facelet_count(), false) {
        for (std::size_t f = 0; f < geometry.facelet_count(); ++f) {
            const Role role = geometry.role(f);
            relevant_.at(f) = role == Role::Centre || role == Role::FaceCentre;
        }
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            for (int layer = 0; layer < geometry.size(); ++layer) {
                for (int turns = 1; turns <= 3; ++turns) {
                    set_.turns.push_back(SliceTurn{.axis = axis, .layer = layer, .turns = turns});
                }
            }
        }
    }

    // Offers an action; of two with the same effect the shorter is kept.
    void offer(Word word, std::vector<Move> moves) {
        if (moves.empty()) {
            return;
        }
        std::uint64_t key = 1469598103934665603ULL;
        for (const auto& [to, from] : moves) {
            key = (key ^ ((static_cast<std::uint64_t>(to) << 16U) | from)) * 1099511628211ULL;
        }
        const auto [at, fresh] = seen_.try_emplace(key, set_.actions.size());
        if (fresh) {
            set_.actions.push_back(Action{.word = std::move(word), .moves = std::move(moves)});
        } else if (word.size() < set_.actions.at(at->second).word.size()) {
            set_.actions.at(at->second) =
                Action{.word = std::move(word), .moves = std::move(moves)};
        }
    }

    void add_turns_and_commutators() {
        const int size = geometry_->size();
        for (const SliceTurn x : set_.turns) {
            if (is_outer(x, size)) {
                offer({x}, relevant_moves(geometry_->turn(x), relevant_));
            }
        }
        for (const SliceTurn x : set_.turns) {
            for (const SliceTurn z : set_.turns) {
                if (x.axis == z.axis) {
                    continue;  // turns of one axis commute
                }
                const Permutation p = then(then(then(geometry_->turn(x), geometry_->turn(z)),
                                                geometry_->turn(x.inverse())),
                                           geometry_->turn(z.inverse()));
                offer({x, z, x.inverse(), z.inverse()}, relevant_moves(p, relevant_));
            }
        }
        set_.conjugable = set_.actions.size();
    }

    void add_pure_cycles(const CycleLibrary& library) {
        const std::size_t orbits = geometry_->centre_orbits().size();
        for (std::size_t k = 0; k < orbits; ++k) {
            for (const ThreeCycle& cycle : library.centres(k)) {
                // content at cells[j] ends at cells[j + 1].
                const auto& c = cycle.cells;
                std::vector<Move> moves{{c.at(1), c.at(0)}, {c.at(2), c.at(1)}, {c.at(0), c.at(2)}};
                offer(cycle.word, std::move(moves));
            }
        }
    }

    ActionSet finish() {
        set_.changing.assign(geometry_->facelet_count(), {});
        for (std::size_t a = 0; a < set_.actions.size(); ++a) {
            for (const auto& [to, from] : set_.actions.at(a).moves) {
                set_.changing.at(to).push_back(
                    Handle{.action = static_cast<std::uint32_t>(a), .from = from});
            }
        }
        return std::move(set_);
    }

    const Geometry* geometry_;
    std::vector<bool> relevant_;
    ActionSet set_;
    std::unordered_map<std::uint64_t, std::size_t> seen_;
};

const ActionSet& action_set(int size) {
    static std::array<std::once_flag, static_cast<std::size_t>(kMaxSize) + 1> built;
    static std::array<std::unique_ptr<const ActionSet>, static_cast<std::size_t>(kMaxSize) + 1>
        cache;
    const auto slot = static_cast<std::size_t>(size);
    std::call_once(built.at(slot), [slot, size] {
        cache.at(slot) = std::make_unique<const ActionSet>(ActionSetBuilder::build(size));
    });
    return *cache.at(slot);
}

// How many moves merging would take off a candidate that begins with `first`
// right after `last`: a turn of the same layer joins or cancels the one before.
[[nodiscard]] std::size_t merge_saving(const Word& emitted, SliceTurn first) noexcept {
    if (emitted.empty()) {
        return 0;
    }
    const SliceTurn last = emitted.back();
    if (last.axis != first.axis || last.layer != first.layer) {
        return 0;
    }
    return (last.turns + first.turns) % 4 == 0 ? 2 : 1;
}

// A candidate: an action, done after one setup turn (or none), and what it
// would gain.
struct Choice {
    std::uint32_t action = 0;
    std::optional<std::size_t> setup;  // index into ActionSet::turns
    double score = 0.0;                // centre stickers put right, per move
};

// The permutations that carry a setup turn: how a candidate's stickers map
// when it is done after the turn and undone after.
struct Setup {
    std::optional<std::size_t> index;
    const Permutation* forward = nullptr;
    const Permutation* back = nullptr;
};

// The state of the search: which sticker has what colour, and what each
// centre sticker should be.
class Search {
public:
    Search(NCube& cube, const std::array<Color, 6>& scheme)
        : cube_(&cube),
          geometry_(&cube.geometry()),
          set_(&action_set(cube.size())),
          colour_(cube.geometry().facelet_count()),
          want_(cube.geometry().facelet_count(), kNone),
          protected_(cube.geometry().facelet_count(), false) {
        for (std::size_t f = 0; f < colour_.size(); ++f) {
            colour_.at(f) = static_cast<std::uint8_t>(cube.facelet(f));
            const Role role = geometry_->role(f);
            if (role == Role::Centre) {
                want_.at(f) = static_cast<std::uint8_t>(
                    scheme.at(static_cast<std::size_t>(geometry_->face_of(f))));
                scored_.push_back(static_cast<std::uint16_t>(f));
            } else if (role == Role::FaceCentre) {
                protected_.at(f) = true;
            }
        }
    }

    Result<Word> run() {
        if (!feasible()) {
            return std::unexpected(CubeError::invalid_cube_state);
        }
        Word emitted;
        // Each step puts at least one more sticker right, so this cannot run
        // longer than there are stickers; the cap is only a backstop.
        for (std::size_t step = 0; step <= scored_.size() + 1; ++step) {
            const std::vector<std::uint16_t> wrong = wrong_cells();
            if (wrong.empty()) {
                return emitted;
            }
            const std::optional<Choice> choice = best_choice(wrong, emitted);
            if (!choice) {
                return std::unexpected(CubeError::no_solution);
            }
            apply(*choice, emitted);
        }
        return std::unexpected(CubeError::no_solution);
    }

private:
    static constexpr std::uint8_t kNone = 0xFF;
    // How many wrong stickers are tried as the one to fix each step. More
    // finds slightly better words and costs time; past a dozen it is no better.
    static constexpr std::size_t kTargets = 8;

    // Each colour appears on the centre stickers exactly as often as the
    // scheme puts it there.
    [[nodiscard]] bool feasible() const {
        std::array<int, 8> have{};
        std::array<int, 8> need{};
        for (const std::uint16_t f : scored_) {
            ++have.at(colour_.at(f));
            ++need.at(want_.at(f));
        }
        return have == need;
    }

    [[nodiscard]] std::vector<std::uint16_t> wrong_cells() const {
        std::vector<std::uint16_t> wrong;
        for (const std::uint16_t f : scored_) {
            if (colour_.at(f) != want_.at(f)) {
                wrong.push_back(f);
            }
        }
        return wrong;
    }

    // The net number of centre stickers a candidate would put right, or
    // nullopt if it would move a protected one.
    [[nodiscard]] std::optional<int> gain_of(const Action& action, const Permutation* h) const {
        int gain = 0;
        for (const auto& [to, from] : action.moves) {
            const std::uint16_t j = h != nullptr ? h->at(to) : to;
            if (protected_.at(j)) {
                return std::nullopt;
            }
            const std::uint16_t s = h != nullptr ? h->at(from) : from;
            if (want_.at(j) != kNone) {
                gain +=
                    (colour_.at(s) == want_.at(j) ? 1 : 0) - (colour_.at(j) == want_.at(j) ? 1 : 0);
            }
        }
        return gain;
    }

    // `handle`'s action, after `setup`, scored - if it puts the right colour
    // on `t`, moves nothing protected and gains overall.
    [[nodiscard]] std::optional<Choice> candidate(const Handle& handle, const Setup& setup,
                                                  std::uint16_t t, const Word& emitted) const {
        if (setup.index && handle.action >= set_->conjugable) {
            return std::nullopt;
        }
        const std::uint16_t s =
            setup.forward != nullptr ? setup.forward->at(handle.from) : handle.from;
        if (colour_.at(s) != want_.at(t)) {
            return std::nullopt;
        }
        const Action& action = set_->actions.at(handle.action);
        const std::optional<int> gain = gain_of(action, setup.forward);
        if (!gain || *gain <= 0) {
            return std::nullopt;
        }
        const SliceTurn first = setup.index ? set_->turns.at(*setup.index) : action.word.front();
        std::size_t length = action.word.size() + (setup.index ? 2 : 0);
        length -= std::min(length - 1, merge_saving(emitted, first));
        return Choice{
            .action = handle.action,
            .setup = setup.index,
            .score = static_cast<double>(*gain) / static_cast<double>(length),
        };
    }

    // The best candidate, over every setup, that fixes sticker `t`.
    [[nodiscard]] std::optional<Choice> best_fixing(std::uint16_t t, const Word& emitted) const {
        std::optional<Choice> best;
        for (std::size_t k = 0; k <= set_->turns.size(); ++k) {
            Setup setup;
            std::uint16_t i = t;
            if (k > 0) {
                const SliceTurn turn = set_->turns.at(k - 1);
                setup = Setup{
                    .index = k - 1,
                    .forward = &geometry_->turn(turn),
                    .back = &geometry_->turn(turn.inverse()),
                };
                i = setup.back->at(t);
            }
            for (const Handle& handle : set_->changing.at(i)) {
                const std::optional<Choice> c = candidate(handle, setup, t, emitted);
                if (c && (!best || c->score > best->score)) {
                    best = c;
                }
            }
        }
        return best;
    }

    [[nodiscard]] std::optional<Choice> best_choice(const std::vector<std::uint16_t>& wrong,
                                                    const Word& emitted) const {
        std::optional<Choice> best;
        const std::size_t stride = std::max<std::size_t>(1, wrong.size() / kTargets);
        std::size_t tried = 0;
        for (std::size_t w = 0; w < wrong.size() && tried < kTargets; w += stride, ++tried) {
            const std::optional<Choice> c = best_fixing(wrong.at(w), emitted);
            if (c && (!best || c->score > best->score)) {
                best = c;
            }
        }
        return best;
    }

    void apply(const Choice& choice, Word& emitted) {
        const Action& action = set_->actions.at(choice.action);
        const Permutation* h = nullptr;
        if (choice.setup) {
            const SliceTurn turn = set_->turns.at(*choice.setup);
            h = &geometry_->turn(turn);
            cube_->apply(turn);
            emitted.push_back(turn);
        }
        for (const SliceTurn t : action.word) {
            cube_->apply(t);
            emitted.push_back(t);
        }
        if (choice.setup) {
            const SliceTurn back = set_->turns.at(*choice.setup).inverse();
            cube_->apply(back);
            emitted.push_back(back);
        }
        // The same change to the colours: all sources read before any is written.
        std::vector<std::uint8_t> taken;
        taken.reserve(action.moves.size());
        for (const auto& move : action.moves) {
            taken.push_back(colour_.at(h != nullptr ? h->at(move.second) : move.second));
        }
        for (std::size_t k = 0; k < action.moves.size(); ++k) {
            const std::uint16_t to = action.moves.at(k).first;
            colour_.at(h != nullptr ? h->at(to) : to) = taken.at(k);
        }
    }

    NCube* cube_;
    const Geometry* geometry_;
    const ActionSet* set_;
    std::vector<std::uint8_t> colour_;
    std::vector<std::uint8_t> want_;
    std::vector<bool> protected_;
    std::vector<std::uint16_t> scored_;
};

}  // namespace

Result<Word> solve_centre_stickers(NCube& cube, const std::array<Color, 6>& scheme) {
    return Search(cube, scheme).run();
}

}  // namespace cube::nxn::detail
