#pragma once

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/vision/direct_scan.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <limits>
#include <vector>

namespace cube::vision {

// Working out how a cube is being held from one side of it.
//
// A camera facing the person sees the side of the cube that faces away from
// them. When that side is square to the camera, read_side gives its nine
// colours; the cube itself is known (it was scanned, and every move since is
// known), so those nine colours can be compared with what each of the 24
// ways of holding it would show. The hold that fits says which side faces
// the camera and which way up it is - enough to draw the cube the way it is
// held.
//
// This is not pose tracking. Nothing is estimated between reads and nothing
// is read at an angle: a side square to the camera, or no answer.
//
// The two distances below were measured with `cube_scan holds` on the five
// recorded scans that read (docs/recovery-plan.md), each replayed against
// its own cube: a steady read sat a median of 1.3 to 1.7 from the hold it
// showed and nine in ten sat within 3.5, while the nearest hold that would
// have looked any different was never less than 4.9 further off.

// How well a read fits one way of holding the cube.
struct HoldFit {
    solver::Hold hold;
    // Mean colour_distance between the nine stickers read and the nine that
    // hold would show the camera, each compared in the session's own colours.
    float distance = std::numeric_limits<float>::max();
};

// Every hold, best fit first. `lab` is a SideRead's nine colours, row-major
// as the camera saw them.
[[nodiscard]] std::array<HoldFit, 24> fit_holds(const std::array<cv::Vec3f, 9>& lab,
                                                const solver::Cube& cube,
                                                const SessionColours& colours);
// The same for a read, counting only the stickers the reader actually found.
// A cube in the hand has fingers on it; a sticker under a finger is sampled
// where it must be and comes out the colour of the finger, which says
// nothing about the cube.
[[nodiscard]] std::array<HoldFit, 24> fit_holds(const SideRead& read, const solver::Cube& cube,
                                                const SessionColours& colours);

// A read shows a hold when it sits this close to it.
inline constexpr float kHoldFitDistance = 3.5F;
// ...and no hold that would look different comes within this of fitting as
// well. One sticker of the wrong colour is worth a ninth of the gap between
// the two colours: 0.9 for red against orange by tungsten light, the
// narrowest pair measured, and 1.6 to 2.0 by daylight. So this is under one
// sticker in the worst light, and a read that cannot rule a one-sticker
// difference out decides nothing.
inline constexpr float kHoldClearDistance = 0.5F;

// What one read says about the hold.
struct HoldReading {
    // The holds the read shows; empty when it shows none clearly - not this
    // cube, or not a clean read. More than one when several holds show the
    // camera the very same nine colours (a side whose pattern looks the same
    // when turned), which nothing in one read can choose between.
    std::vector<solver::Hold> holds;
    // The best fit's distance, whether or not it was good enough to count:
    // how well this cube explains the read at all.
    float distance = std::numeric_limits<float>::max();

    [[nodiscard]] bool shows(const solver::Hold& hold) const noexcept;
};

[[nodiscard]] HoldReading read_hold(const std::array<cv::Vec3f, 9>& lab, const solver::Cube& cube,
                                    const SessionColours& colours);
// The same for a read, counting only the stickers the reader actually found.
[[nodiscard]] HoldReading read_hold(const SideRead& read, const solver::Cube& cube,
                                    const SessionColours& colours);

// How many of the stickers a read found are some other colour than the one
// `expected` has there - nearer, that is, to another of the session's six
// colours than to their own. One such is as likely a glint on a sticker as a
// sticker in the wrong place; two or more are a different side.
[[nodiscard]] int stickers_off(const SideRead& read, const std::array<solver::Color, 9>& expected,
                               const SessionColours& colours);

}  // namespace cube::vision
