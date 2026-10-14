#pragma once

#include <array>

#include <cube/error.hpp>
#include <cube/nxn/commutator.hpp>
#include <cube/nxn/cube.hpp>

namespace cube::nxn::detail {

// Turns `cube` until every centre sticker shows its face's colour in
// `scheme`, and returns the turns made (each already applied to `cube`).
//
// The 3-cycles of the commutator engine fix one to three stickers per nine or
// so moves and leave every other piece alone, which is more care than centres
// need: the wings and corners are not solved yet and the stage after this one
// puts them right whatever state they are in. So this searches a wider set
// of short words - the outer turns, and the commutators of any two turns -
// each possibly done after one turn and undone after, and takes whichever
// puts the most centre stickers right per move, whatever it does to the
// wings and corners. What it may not do is move a face's middle cubie (which
// is what an odd size's colour scheme is read from); and every word is even
// on every wing orbit, being a commutator or an outer turn, so the parity
// decision made before this stage still holds after it.
//
// The pure 3-cycles are in the set too, which is what guarantees every step
// gains: for any wrong sticker and any sticker that should be there, some
// cycle puts it there and breaks nothing else.
//
// Fails with no_solution only if the centre stickers cannot be arranged to
// `scheme` at all (their colours are not the multiset the scheme needs).
[[nodiscard]] Result<Word> solve_centre_stickers(NCube& cube, const std::array<Color, 6>& scheme);

// Builds the action set for `size` now instead of on the first solve.
void prepare_centre_search(int size);

}  // namespace cube::nxn::detail
