#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <cube/error.hpp>
#include <cube/nxn/geometry.hpp>

// The commutator engine.
//
// A commutator A B A' B' of two layer turns that overlap in a single piece
// cycles three pieces and puts everything else back, which is exactly the
// operation a reduction needs: it can put one piece in its place without
// undoing the rest. Rather than carry a list of algorithms per size and per
// orbit, this finds them: it enumerates short commutators, folds each into
// the one rearrangement it makes, and keeps those that move exactly three
// pieces of one kind and nothing else. A cycle of pieces that is not in the
// list is reached by conjugating one that is (do a turn, the cycle, the turn
// back), which moves the three pieces it acts on to wherever the turn took
// them and costs two more moves - so the search can aim any cycle at any
// three places it can reach.

namespace cube::nxn {

using Word = std::vector<SliceTurn>;

// A rearrangement of three pieces that leaves the cube otherwise as it was.
// `cells` names the pieces by one facelet each (the sticker itself for a
// centre; the primary sticker for a wing, whose secondary comes with it), in
// the order the cycle moves them: what is at cells[0] ends at cells[1], what
// was at cells[1] ends at cells[2], and what was at cells[2] ends at
// cells[0].
struct ThreeCycle {
    std::array<std::uint16_t, 3> cells{};
    Word word;
};

// The pure 3-cycles known for one size, per orbit.
class CycleLibrary {
public:
    // Built on first use, for the life of the program. Safe from any thread.
    [[nodiscard]] static const CycleLibrary& of(int size);

    // The cycles of the stickers of one centre orbit: `index` counts along
    // Geometry::centre_orbits().
    [[nodiscard]] const std::vector<ThreeCycle>& centres(std::size_t index) const noexcept {
        return centres_.at(index);
    }
    // The cycles of the wings of one wing orbit: `index` counts along
    // Geometry::wing_orbits().
    [[nodiscard]] const std::vector<ThreeCycle>& wings(std::size_t index) const noexcept {
        return wings_.at(index);
    }

private:
    friend class CycleLibraryBuilder;

    std::vector<std::vector<ThreeCycle>> centres_;
    std::vector<std::vector<ThreeCycle>> wings_;
};

// What is where: one label per cell, compared for equality. Colours stand in
// for centre pieces (which are told apart only by colour) and a colour pair
// for wings.
using Labels = std::vector<int>;

// Rearranges the cells by 3-cycles from `cycles` until `current` equals
// `target`, and returns the turns that do it. `cells[k]` is the facelet that
// names cell k; a cycle in `cycles` acts on three of those cells (a cycle
// that names a facelet that is not one of them is not used). `current` is
// left as the arrangement the returned turns produce.
//
// Always done in the fewest moves per cell fixed that the library allows, but
// not the fewest moves overall. Fails with no_solution when the cells cannot
// be arranged by these cycles (for example when `target` is an odd
// permutation of `current`, which no 3-cycle can reach).
[[nodiscard]] Result<Word> arrange(const std::vector<std::uint16_t>& cells,
                                   const std::vector<ThreeCycle>& cycles, Labels& current,
                                   const Labels& target);

}  // namespace cube::nxn
