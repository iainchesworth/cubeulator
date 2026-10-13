#pragma once

#include <cube/solver/cube.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/direct_scan.hpp>

namespace cube::vision {

// How well a candidate cube explains a corner read.
struct CornerFit {
    // Every side matches a side of the cube under some quarter-turn, and every
    // shared edge is where the cube puts the two sides' common edge, with the
    // stickers along it paired as they are on the cube.
    bool fits = false;
    // Mean colour distance over the cells compared, with each side in the
    // turn that suited it best; large when nothing was compared.
    float distance = 1.0e9F;
};

// A side matches when its nine colours sit this close (mean colour_distance)
// to the candidate's side in the session's own colours. Sides seen at an angle
// are lit differently from those seen square-on, so this is looser than the
// held-cube fit.
inline constexpr float kCornerFitDistance = 8.0F;
// A turn of a side counts as possible when it is within this of the best.
// Sides with a symmetric pattern fit several turns equally and constrain
// nothing, which is correct.
inline constexpr float kCornerTurnTolerance = 1.5F;

// Whether `cube` could have produced `read`, the colours in `colours` being
// what each of its stickers measured in this session.
[[nodiscard]] CornerFit fit_corner(const CornerRead& read, const solver::Cube& cube,
                                   const SessionColours& colours);

}  // namespace cube::vision
