#pragma once

// Scripts for the fake camera: what the person at the desk would show it.

#include "fake_camera.hpp"

#include "drawn_cube.hpp"

#include <cube/solver/cube.hpp>
#include <cube/vision/direct_scan.hpp>

#include <array>

namespace cube::systest {

// Polls it takes for a side held still to count: the first has nothing to be
// steady against (the same figure tests/unit/direct_scan_controller_test.cpp
// uses). A few spare, because the scan screen's own poll is 30 Hz and a frame
// is not lost for being late.
inline constexpr int kHoldFrames = static_cast<int>(vision::SideScan::kMinReads) + 1 + 3;

// One cube side, held square to the camera.
inline void show_side(const std::array<solver::Color, 9>& stickers, int frames = kHoldFrames,
                      const cube::test::Pose& pose = {}) {
    FakeCamera::instance().play(cube::test::draw_side(stickers, pose), frames);
}

// Nothing in view: the cube is turning to its next side.
inline void show_empty_room(int frames = 1) {
    FakeCamera::instance().play(cube::test::room({640, 360}), frames);
}

// Every side of `cube`, each held long enough to count, with a turn between.
// Scripted, not waited on: the app consumes it one frame per poll.
inline void show_cube(const solver::Cube& cube) {
    for (const solver::Face face : solver::kAllFaces) {
        show_side(cube::test::outside_view(cube, face));
        show_empty_room();
    }
}

// The first `sides` sides of `cube` only: a scan part way through.
inline void show_sides(const solver::Cube& cube, int sides) {
    int shown = 0;
    for (const solver::Face face : solver::kAllFaces) {
        if (shown++ == sides) {
            break;
        }
        show_side(cube::test::outside_view(cube, face));
        show_empty_room();
    }
}

}  // namespace cube::systest
