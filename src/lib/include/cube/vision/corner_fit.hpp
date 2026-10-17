#pragma once

#include <cube/nxn/cube.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/direct_scan.hpp>

#include <vector>

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
// held-cube fit. It was 8, from six frames that sat within 7; the first cube
// held on a corner by hand by lamplight had its sides at a median of 8.2 and
// 90% within 11.0, and no read of it fitted any cube at 8 (30 of 35 did at
// 12). The same number as the same-side distance, which it must not be
// tighter than: a side counted for a side has to be able to fit it. On the
// older recordings 12 fits more of the true cube's reads, not fewer (4 of 5
// against 3 on the tungsten session) and still rules out 14 to 17.5 of the 18
// cubes one turn away (cube_scan corners <session> 12).
inline constexpr float kCornerFitDistance = 12.0F;
// A turn of a side counts as possible when it is within this of the best.
// Sides with a symmetric pattern fit several turns equally and constrain
// nothing, which is correct.
inline constexpr float kCornerTurnTolerance = 1.5F;

// Whether `cube` could have produced `read`, the colours in `colours` being
// what each of its stickers measured in this session. `max_distance` is how
// near each side's nine colours must sit to the cube's; it is a parameter so
// that what a recording needs can be measured (cube_scan ambiguity), and the
// app uses the default.
[[nodiscard]] CornerFit fit_corner(const CornerRead& read, const solver::Cube& cube,
                                   const SessionColours& colours,
                                   float max_distance = kCornerFitDistance);

// fit_corner of one read against each of `cubes`, in their order: the same
// answers, to the field, for much less work when the cubes are many. A read is
// fitted to a cube through the sides it shows - the colours of the three or two
// faces of the cube that its centres name - and nothing else of the cube, and
// the cubes six flat sides make differ from each other in a few stickers, so
// most of them show a read the same faces. Those are fitted once.
[[nodiscard]] std::vector<CornerFit> fit_corner_each(const CornerRead& read,
                                                     const std::vector<solver::Cube>& cubes,
                                                     const SessionColours& colours,
                                                     float max_distance = kCornerFitDistance);

// The same for a cube of any size. A side of the read is matched to a face of the
// cube by its N x N colours alone (an even size has no centre to name it by, and an
// odd one is as well named by what is on it); each shared edge must then be where the
// cube puts the two faces' common edge, with the stickers along it paired as the cube
// has them. `read.size` must be the cubes'; a cube of another size fits nothing.
[[nodiscard]] std::vector<CornerFit> fit_corner_each(const CornerRead& read,
                                                     const std::vector<nxn::NCube>& cubes,
                                                     const SessionColours& colours,
                                                     float max_distance = kCornerFitDistance);

}  // namespace cube::vision
