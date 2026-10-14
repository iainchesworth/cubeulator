#include <cube/nxn/commutator.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace cube::nxn {

namespace {

// A cycle's identity: its cells starting from the smallest, packed. Facelets
// fit in ten bits up to size 9 (486), so three fit in a word.
[[nodiscard]] std::uint64_t cycle_key(std::array<std::uint16_t, 3> cells) noexcept {
    std::ranges::rotate(cells, std::ranges::min_element(cells));
    return (static_cast<std::uint64_t>(cells.at(0)) << 20U) |
           (static_cast<std::uint64_t>(cells.at(1)) << 10U) |
           static_cast<std::uint64_t>(cells.at(2));
}

// One group of interchangeable pieces' cycles under construction.
struct Group {
    std::vector<ThreeCycle> cycles;
    std::unordered_map<std::uint64_t, std::size_t> index;

    // Keeps the shortest word seen for each distinct cycle.
    void offer(std::array<std::uint16_t, 3> cells, const Word& word) {
        const std::uint64_t key = cycle_key(cells);
        const auto [at, fresh] = index.try_emplace(key, cycles.size());
        if (fresh) {
            cycles.push_back(ThreeCycle{.cells = cells, .word = word});
        } else if (word.size() < cycles.at(at->second).word.size()) {
            cycles.at(at->second).word = word;
        }
    }
};

// A commutator's shape: c, then z, then c', then z'. `c` is one turn or a
// conjugate x y x' of one; both it and its inverse are kept as permutations.
struct Prefix {
    Permutation forward;
    Permutation back;
    Word word;
};

// The most facelets a 3-cycle of wings moves; a commutator that moves more
// is not worth looking at any further.
constexpr std::size_t kMostMoved = 6;

// Folds the commutator c z c' z' into the facelets it moves, stopping as soon
// as more than kMostMoved are seen to. Returns false in that case.
//
// This is the one loop that is hot: it runs once per candidate commutator, a
// few hundred thousand times to build the library of a 7x7, and every value it
// indexes with is a facelet number read out of one of these four tables, all of
// them the same length. Checked indexing here made the whole library build
// about 1.8x slower (measured), so the check is made once, up front.
[[nodiscard]] bool fold_commutator(const Prefix& prefix, const Permutation& z,
                                   const Permutation& z_back, std::vector<std::uint16_t>& moved) {
    moved.clear();
    if (prefix.forward.size() != z.size() || prefix.back.size() != z.size() ||
        z_back.size() != z.size()) {
        return false;
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    // The cube after c, z, c', z' shows at i what it showed at
    // c[z[c'[z'[i]]]] before.
    for (std::size_t i = 0; i < z.size(); ++i) {
        const std::uint16_t from = prefix.forward[z[prefix.back[z_back[i]]]];
        if (from == i) {
            continue;
        }
        if (moved.size() == kMostMoved) {
            return false;
        }
        moved.push_back(static_cast<std::uint16_t>(i));
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return true;
}

// `word`, done after `h` and before `h` is undone.
[[nodiscard]] Word conjugated(SliceTurn h, const Word& word) {
    Word result;
    result.reserve(word.size() + 2);
    result.push_back(h);
    result.insert(result.end(), word.begin(), word.end());
    result.push_back(h.inverse());
    return result;
}

// The cycle a 3-element moved set makes, as cells in the order content
// travels: content at a goes to the i with gather(i) == a.
template <typename Gather>
[[nodiscard]] std::optional<std::array<std::uint16_t, 3>> cycle_of(
    const std::vector<std::uint16_t>& cells, const Gather& gather) {
    const auto next = [&](std::uint16_t a) {
        for (const std::uint16_t i : cells) {
            if (gather(i) == a) {
                return i;
            }
        }
        return a;
    };
    const std::uint16_t a = cells.at(0);
    const std::uint16_t b = next(a);
    const std::uint16_t c = next(b);
    if (a == b || b == c || a == c || gather(a) != c) {
        return std::nullopt;
    }
    return std::array<std::uint16_t, 3>{a, b, c};
}

}  // namespace

class CycleLibraryBuilder {
public:
    static CycleLibrary build(const Geometry& geometry) {
        CycleLibraryBuilder builder(geometry);
        builder.enumerate_commutators();
        builder.conjugate_once();
        builder.complete();
        CycleLibrary library;
        for (Group& g : builder.centre_groups_) {
            library.centres_.push_back(std::move(g.cycles));
        }
        for (Group& g : builder.wing_groups_) {
            library.wings_.push_back(std::move(g.cycles));
        }
        return library;
    }

private:
    explicit CycleLibraryBuilder(const Geometry& geometry)
        : geometry_(&geometry),
          centre_of_(geometry.facelet_count(), -1),
          wing_group_of_(geometry.facelet_count(), -1),
          wing_slot_of_(geometry.facelet_count(), -1) {
        index_centres();
        index_wings();
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            for (int layer = 0; layer < geometry.size(); ++layer) {
                for (int turns = 1; turns <= 3; ++turns) {
                    moves_.push_back(SliceTurn{.axis = axis, .layer = layer, .turns = turns});
                }
            }
        }
    }

    void index_centres() {
        const auto& orbits = geometry_->centre_orbits();
        centre_groups_.resize(orbits.size());
        for (std::size_t k = 0; k < orbits.size(); ++k) {
            for (const std::uint16_t f : geometry_->orbit_facelets(orbits.at(k))) {
                centre_of_.at(f) = static_cast<int>(k);
            }
        }
    }

    void index_wings() {
        const auto& orbits = geometry_->wing_orbits();
        wing_groups_.resize(orbits.size());
        for (std::size_t g = 0; g < orbits.size(); ++g) {
            for (std::size_t s = 0; s < orbits.at(g).size(); ++s) {
                const WingSlot& slot = orbits.at(g).at(s);
                for (const std::uint16_t f : {slot.primary, slot.secondary}) {
                    wing_group_of_.at(f) = static_cast<int>(g);
                    wing_slot_of_.at(f) = static_cast<int>(s);
                }
            }
        }
    }

    [[nodiscard]] std::vector<Prefix> make_prefixes() const {
        std::vector<Prefix> prefixes;
        prefixes.reserve(moves_.size() + (moves_.size() * moves_.size()));
        for (const SliceTurn x : moves_) {
            prefixes.push_back(
                {.forward = geometry_->turn(x), .back = geometry_->turn(x.inverse()), .word = {x}});
        }
        for (const SliceTurn x : moves_) {
            for (const SliceTurn y : moves_) {
                if (x.axis == y.axis) {
                    continue;  // the same axis commutes: x y x' is just y
                }
                Permutation forward = then(then(geometry_->turn(x), geometry_->turn(y)),
                                           geometry_->turn(x.inverse()));
                Permutation back = inverse(forward);
                prefixes.push_back({
                    .forward = std::move(forward),
                    .back = std::move(back),
                    .word = {x, y, x.inverse()},
                });
            }
        }
        return prefixes;
    }

    // All words  c z c' z'  with z one turn and c either one turn or a
    // conjugate  x y x'  of one, kept when they make a 3-cycle.
    void enumerate_commutators() {
        std::vector<std::uint16_t> moved;
        moved.reserve(kMostMoved + 1);
        for (const Prefix& prefix : make_prefixes()) {
            for (const SliceTurn z : moves_) {
                const Permutation& forward = geometry_->turn(z);
                const Permutation& back = geometry_->turn(z.inverse());
                if (!fold_commutator(prefix, forward, back, moved)) {
                    continue;
                }
                if (moved.size() != 3 && moved.size() != 6) {
                    continue;
                }
                // gather(i): where the content now at i came from.
                const auto gather = [&](std::uint16_t i) {
                    return prefix.forward.at(forward.at(prefix.back.at(back.at(i))));
                };
                Word word = prefix.word;
                word.push_back(z);
                for (const SliceTurn t : prefix.word | std::views::reverse) {
                    word.push_back(t.inverse());
                }
                word.push_back(z.inverse());
                if (moved.size() == 3) {
                    offer_centres(moved, gather, word);
                } else {
                    offer_wings(moved, gather, word);
                }
            }
        }
    }

    template <typename Gather>
    void offer_centres(const std::vector<std::uint16_t>& moved, const Gather& gather,
                       const Word& word) {
        const int orbit = centre_of_.at(moved.at(0));
        const auto in_orbit = [&](std::uint16_t f) { return centre_of_.at(f) == orbit; };
        if (orbit < 0 || !std::ranges::all_of(moved, in_orbit)) {
            return;
        }
        if (const auto cells = cycle_of(moved, gather)) {
            centre_groups_.at(static_cast<std::size_t>(orbit)).offer(*cells, word);
        }
    }

    // Six moved stickers are three wings if each wing has both its stickers
    // among them and all are in one group.
    template <typename Gather>
    void offer_wings(const std::vector<std::uint16_t>& moved, const Gather& gather,
                     const Word& word) {
        const int group = wing_group_of_.at(moved.at(0));
        if (group < 0) {
            return;
        }
        const auto& orbit = geometry_->wing_orbits().at(static_cast<std::size_t>(group));
        std::vector<std::uint16_t> primaries;
        for (const std::uint16_t f : moved) {
            if (wing_group_of_.at(f) != group) {
                return;
            }
            const WingSlot& slot = orbit.at(static_cast<std::size_t>(wing_slot_of_.at(f)));
            if (f != slot.primary) {
                continue;
            }
            if (std::ranges::find(moved, slot.secondary) == moved.end()) {
                return;
            }
            primaries.push_back(f);
        }
        if (primaries.size() != 3) {
            return;
        }
        const auto cells = cycle_of(primaries, gather);
        if (!cells) {
            return;
        }
        // The secondaries must go the same way round (they do, by orbit
        // invariance; this guards the assumption rather than relying on it).
        const auto secondary_of = [&](std::uint16_t primary) {
            return orbit.at(static_cast<std::size_t>(wing_slot_of_.at(primary))).secondary;
        };
        if (gather(secondary_of(cells->at(1))) != secondary_of(cells->at(0))) {
            return;
        }
        wing_groups_.at(static_cast<std::size_t>(group)).offer(*cells, word);
    }

    // Every known cycle, aimed elsewhere by one turn: do the turn, the cycle,
    // the turn back. The three pieces the cycle acts on are the three that
    // the turn brought to its cells, so the cells of the new cycle are
    // where the turn's gather says they came from.
    void conjugate_once() {
        widen(centre_groups_, false);
        widen(wing_groups_, true);
    }

    void widen(std::vector<Group>& groups, bool wings) const {
        for (Group& group : groups) {
            const std::vector<ThreeCycle> base = group.cycles;
            for (const ThreeCycle& cycle : base) {
                for (const SliceTurn h : moves_) {
                    const Permutation& gather = geometry_->turn(h);
                    std::array<std::uint16_t, 3> cells{};
                    std::ranges::transform(cycle.cells, cells.begin(),
                                           [&gather](std::uint16_t f) { return gather.at(f); });
                    if (wings && !same_wing_group(cells)) {
                        continue;
                    }
                    group.offer(cells, conjugated(h, cycle.word));
                }
            }
        }
    }

    // A few cycles are out of reach of one conjugation of a commutator. Each
    // of those is one more conjugation away from a cycle that was found, so
    // rather than conjugating everything again, ask for each missing cycle
    // whether some turn undoes it into one that is known: the answer is a
    // lookup per turn, and it leaves every group with all of its cycles.
    void complete() {
        const auto& centre_orbits = geometry_->centre_orbits();
        for (std::size_t k = 0; k < centre_groups_.size(); ++k) {
            complete_group(centre_groups_.at(k), geometry_->orbit_facelets(centre_orbits.at(k)));
        }
        const auto& wing_orbits = geometry_->wing_orbits();
        for (std::size_t g = 0; g < wing_groups_.size(); ++g) {
            std::vector<std::uint16_t> primaries;
            primaries.reserve(wing_orbits.at(g).size());
            for (const WingSlot& slot : wing_orbits.at(g)) {
                primaries.push_back(slot.primary);
            }
            complete_group(wing_groups_.at(g), primaries);
        }
    }

    void complete_group(Group& group, const std::vector<std::uint16_t>& cells) const {
        const std::size_t count = cells.size();
        if (group.cycles.size() >= count * (count - 1) * (count - 2) / 3) {
            return;
        }
        for (const std::uint16_t a : cells) {
            for (const std::uint16_t b : cells) {
                for (const std::uint16_t c : cells) {
                    // Each cycle once, starting from its smallest cell.
                    if (a >= b || a >= c || b == c || group.index.contains(cycle_key({a, b, c}))) {
                        continue;
                    }
                    if (const auto found = conjugate_from_known(group, {a, b, c})) {
                        group.offer({a, b, c}, *found);
                    }
                }
            }
        }
    }

    // A word for the cycle `cells` made of a known cycle and one turn around
    // it, if there is one.
    [[nodiscard]] std::optional<Word> conjugate_from_known(
        const Group& group, std::array<std::uint16_t, 3> cells) const {
        for (const SliceTurn h : moves_) {
            const Permutation& undo = geometry_->turn(h.inverse());
            const auto found = group.index.find(
                cycle_key({undo.at(cells.at(0)), undo.at(cells.at(1)), undo.at(cells.at(2))}));
            if (found != group.index.end()) {
                return conjugated(h, group.cycles.at(found->second).word);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool same_wing_group(const std::array<std::uint16_t, 3>& cells) const {
        const int group = wing_group_of_.at(cells.at(0));
        return group >= 0 && std::ranges::all_of(cells, [this, group](std::uint16_t f) {
                   return wing_group_of_.at(f) == group;
               });
    }

    const Geometry* geometry_;
    std::vector<int> centre_of_;
    std::vector<int> wing_group_of_;
    std::vector<int> wing_slot_of_;
    std::vector<SliceTurn> moves_;
    std::vector<Group> centre_groups_;
    std::vector<Group> wing_groups_;
};

const CycleLibrary& CycleLibrary::of(int size) {
    static std::array<std::once_flag, static_cast<std::size_t>(kMaxSize) + 1> built;
    static std::array<std::unique_ptr<const CycleLibrary>, static_cast<std::size_t>(kMaxSize) + 1>
        cache;
    const auto slot = static_cast<std::size_t>(size);
    std::call_once(built.at(slot), [slot, size] {
        cache.at(slot) =
            std::make_unique<const CycleLibrary>(CycleLibraryBuilder::build(Geometry::of(size)));
    });
    return *cache.at(slot);
}

namespace {

constexpr std::size_t kNoCell = static_cast<std::size_t>(-1);

// A library cycle in terms of the cells of one arrangement problem.
struct Usable {
    std::array<std::size_t, 3> at;
    const Word* word;
};

// The cycles of `cycles` whose three cells are all among `cells`, with their
// cells numbered by position in `cells`.
[[nodiscard]] std::vector<Usable> usable_cycles(const std::vector<std::uint16_t>& cells,
                                                const std::vector<ThreeCycle>& cycles) {
    std::size_t largest = 0;
    for (const std::uint16_t f : cells) {
        largest = std::max<std::size_t>(largest, f);
    }
    std::vector<std::size_t> cell_of(largest + 1, kNoCell);
    for (std::size_t k = 0; k < cells.size(); ++k) {
        cell_of.at(cells.at(k)) = k;
    }

    std::vector<Usable> usable;
    usable.reserve(cycles.size());
    for (const ThreeCycle& cycle : cycles) {
        Usable u{.at = {}, .word = &cycle.word};
        bool all_known = true;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint16_t f = cycle.cells.at(k);
            const std::size_t at = f < cell_of.size() ? cell_of.at(f) : kNoCell;
            all_known = all_known && at != kNoCell;
            u.at.at(k) = at;
        }
        if (all_known) {
            usable.push_back(u);
        }
    }
    return usable;
}

// How many more cells hold what they should once `u` has been applied: the
// content at at[0] goes to at[1], at[1]'s to at[2], at[2]'s to at[0].
[[nodiscard]] int gain_of(const Usable& u, const Labels& current, const Labels& target) {
    const std::size_t a = u.at.at(0);
    const std::size_t b = u.at.at(1);
    const std::size_t c = u.at.at(2);
    const auto right = [&](std::size_t cell, int label) {
        return label == target.at(cell) ? 1 : 0;
    };
    return right(b, current.at(a)) + right(c, current.at(b)) + right(a, current.at(c)) -
           right(a, current.at(a)) - right(b, current.at(b)) - right(c, current.at(c));
}

// How many moves a word saves when it is merged onto the end of what has
// been emitted: a turn that meets a turn of its own layer (past turns of other
// layers of the same axis, which commute) joins it or cancels it. Only the
// last few emitted turns can be met, and only the first few of the word are
// worth asking about, so a small window is all that is looked at.
[[nodiscard]] std::size_t saved_by_merging(std::span<const SliceTurn> emitted, const Word& word) {
    constexpr std::size_t kTail = 8;
    constexpr std::size_t kHead = 4;
    std::array<SliceTurn, kTail + kHead> window{};
    std::size_t size = std::min(kTail, emitted.size());
    std::ranges::copy(emitted.last(size), window.begin());

    std::size_t saved = 0;
    for (const SliceTurn t : word | std::views::take(kHead)) {
        // The turn this one meets: the nearest of its layer in the run of
        // same-axis turns behind it, if there is one.
        std::optional<std::size_t> meets;
        for (std::size_t back = size; back > 0; --back) {
            const SliceTurn& behind = window.at(back - 1);
            if (behind.axis != t.axis) {
                break;
            }
            if (behind.layer == t.layer) {
                meets = back - 1;
                break;
            }
        }
        if (!meets) {
            window.at(size++) = t;
            continue;
        }
        const std::size_t at = *meets;
        const int sum = (window.at(at).turns + t.turns) % 4;
        if (sum != 0) {
            window.at(at).turns = sum;
            saved += 1;
            continue;
        }
        for (std::size_t k = at + 1; k < size; ++k) {
            window.at(k - 1) = window.at(k);
        }
        --size;
        saved += 2;
    }
    return saved;
}

// The cycle that fixes the most cells per move, or nullptr if none fixes any.
[[nodiscard]] const Usable* best_cycle(const std::vector<Usable>& usable, const Labels& current,
                                       const Labels& target, const Word& emitted) {
    const Usable* best = nullptr;
    std::size_t best_gain = 0;
    std::size_t best_length = 1;
    for (const Usable& u : usable) {
        const int gain = gain_of(u, current, target);
        if (gain <= 0) {
            continue;
        }
        const auto g = static_cast<std::size_t>(gain);
        // What it costs once merged with what came before it.
        const std::size_t saved = saved_by_merging(emitted, *u.word);
        const std::size_t length = std::max<std::size_t>(1, u.word->size() - saved);
        // Most cells fixed per move; ties go to the shorter word.
        const std::size_t lhs = g * best_length;
        const std::size_t rhs = best_gain * length;
        if (best == nullptr || lhs > rhs || (lhs == rhs && length < best_length)) {
            best = &u;
            best_gain = g;
            best_length = length;
        }
    }
    return best;
}

}  // namespace

Result<Word> arrange(const std::vector<std::uint16_t>& cells, const std::vector<ThreeCycle>& cycles,
                     Labels& current, const Labels& target) {
    const std::size_t count = cells.size();
    if (current.size() != count || target.size() != count) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    // Same multiset of labels, or no arrangement exists.
    Labels have = current;
    Labels want = target;
    std::ranges::sort(have);
    std::ranges::sort(want);
    if (have != want) {
        return std::unexpected(CubeError::invalid_cube_state);
    }

    const std::vector<Usable> usable = usable_cycles(cells, cycles);
    Word out;
    // Every round fixes at least one cell for good or the search has
    // stalled, so this bounds the work even if the library is thin.
    for (std::size_t round = 0; round < count * 4; ++round) {
        if (std::ranges::equal(current, target)) {
            return out;
        }
        const Usable* best = best_cycle(usable, current, target, out);
        if (best == nullptr) {
            return std::unexpected(CubeError::no_solution);
        }
        const std::size_t a = best->at.at(0);
        const std::size_t b = best->at.at(1);
        const std::size_t c = best->at.at(2);
        const int moved_a = current.at(a);
        current.at(a) = current.at(c);
        current.at(c) = current.at(b);
        current.at(b) = moved_a;
        out.insert(out.end(), best->word->begin(), best->word->end());
    }
    return std::unexpected(CubeError::no_solution);
}

}  // namespace cube::nxn
