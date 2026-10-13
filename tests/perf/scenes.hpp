#pragma once

// The pictures the scan measurements run on: drawn, never recorded (a
// recording shows whoever was holding the cube), by the helpers the scan's own
// tests use (tests/unit/drawn_cube.hpp).

#include "drawn_cube.hpp"

#include <cube/solver/cube.hpp>
#include <cube/vision/direct_scan.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace cube::perf {

// A side with all six colours on it, so a read has something to tell apart.
inline constexpr std::array<solver::Color, 9> kMixedSide = {
    solver::Color::Green, solver::Color::Orange, solver::Color::Blue,
    solver::Color::Blue,  solver::Color::White,  solver::Color::Yellow,
    solver::Color::Green, solver::Color::Red,    solver::Color::White};

// The frame sizes a camera gives: the working width itself, and the two a
// phone or a webcam is more likely to deliver. The side fills about the same
// share of the picture in each, as it does when the person steps back.
struct SceneSize {
    int width;
    int height;
};
inline constexpr std::array<SceneSize, 3> kSceneSizes = {{{640, 360}, {1280, 720}, {1920, 1080}}};

[[nodiscard]] inline cv::Mat side_frame(SceneSize size) {
    const float scale = static_cast<float>(size.width) / 640.0F;
    const test::Pose pose{
        .centre = {size.width / 2.0F, size.height / 2.0F}, .pitch = 40.0F * scale, .degrees = 6.0F};
    return test::draw_side(kMixedSide, pose, test::Drawing{.size = {size.width, size.height}});
}

// A frame with no cube in it: the read that finds nothing, which is most of
// the frames of a scan that is waiting for a person to pick the cube up.
[[nodiscard]] inline cv::Mat empty_frame(SceneSize size) {
    return test::room({size.width, size.height});
}

// The reads of one scan of `cube`: each of its six sides held square to the
// camera for `frames` frames, in a roll of its own, with the nullopt the turn
// of the cube makes between them. What SideScan::add is fed, in order.
[[nodiscard]] inline std::vector<std::optional<vision::SideRead>> scan_reads(
    const solver::Cube& cube, int frames) {
    std::vector<std::optional<vision::SideRead>> reads;
    int turns = 0;
    for (const solver::Face face : solver::kAllFaces) {
        std::array<solver::Color, 9> stickers = test::outside_view(cube, face);
        for (int i = turns++ % 4; i > 0; --i) {
            stickers = test::turned_clockwise(stickers);
        }
        const cv::Mat frame = test::draw_side(stickers, test::Pose{});
        const std::optional<vision::SideRead> read = vision::read_side(frame);
        for (int i = 0; i < frames; ++i) {
            reads.push_back(read);
        }
        reads.emplace_back(std::nullopt);
    }
    return reads;
}

}  // namespace cube::perf
