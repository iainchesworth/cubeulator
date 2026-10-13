#pragma once

// Camera frames of a cube side, drawn rather than recorded, for tests of the
// direct-scan reader and the controller built on it. Real recordings carry
// the person holding the cube and stay out of the repository; the scoreboard
// tool (tools/scan) is what runs the reader over those.

#include <cube/solver/cube.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <utility>

namespace cube::test {

// Sticker colours roughly as a webcam renders them in even light (BGR).
[[nodiscard]] inline cv::Scalar paint(solver::Color colour) {
    switch (colour) {
        case solver::Color::White:
            return {235, 235, 235};
        case solver::Color::Yellow:
            return {40, 210, 235};
        case solver::Color::Red:
            return {40, 40, 200};
        case solver::Color::Orange:
            return {30, 120, 240};
        case solver::Color::Green:
            return {70, 160, 40};
        case solver::Color::Blue:
            return {170, 80, 20};
    }
    return {};
}

// A cluttered room: mid-grey with noise and a scatter of coloured boxes, so
// the reader has to pick the lattice out of something.
[[nodiscard]] inline cv::Mat room(cv::Size size) {
    cv::Mat frame(size, CV_8UC3, cv::Scalar(96, 104, 110));
    cv::RNG rng(1234);
    for (int i = 0; i < 40; ++i) {
        const cv::Point corner(rng.uniform(0, size.width), rng.uniform(0, size.height));
        const cv::Size box(rng.uniform(8, size.width / 6), rng.uniform(8, size.height / 5));
        cv::rectangle(frame, cv::Rect(corner, box),
                      cv::Scalar(rng.uniform(30, 220), rng.uniform(30, 220), rng.uniform(30, 220)),
                      cv::FILLED);
    }
    cv::Mat noise(size, CV_8UC3);
    rng.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(6));
    cv::add(frame, noise, frame);
    return frame;
}

// Where a side sits in the frame.
struct Pose {
    cv::Point2f centre{320.0F, 180.0F};
    float pitch = 40.0F;   // sticker spacing along the side's rows, pixels
    float degrees = 0.0F;  // in-plane turn
    float squash = 1.0F;   // column spacing over row spacing: under 1 is a side seen at an angle
};

// The pixel at (column, row) sticker spacings from the side's centre.
[[nodiscard]] inline cv::Point2f place(const Pose& pose, float column, float row) {
    const float radians = pose.degrees * std::numbers::pi_v<float> / 180.0F;
    const float x = column * pose.pitch * pose.squash;
    const float y = row * pose.pitch;
    return pose.centre + cv::Point2f(x * std::cos(radians) - y * std::sin(radians),
                                     x * std::sin(radians) + y * std::cos(radians));
}

inline void fill_square(cv::Mat& frame, const Pose& pose, float column, float row, float half,
                        const cv::Scalar& colour) {
    const std::array<cv::Point, 4> corners = {cv::Point(place(pose, column - half, row - half)),
                                              cv::Point(place(pose, column + half, row - half)),
                                              cv::Point(place(pose, column + half, row + half)),
                                              cv::Point(place(pose, column - half, row + half))};
    cv::fillConvexPoly(frame, corners.data(), 4, colour, cv::LINE_AA);
}

struct Drawing {
    cv::Size size{640, 360};
    // How many rows of stickers are in view (a side half out of frame).
    int rows = 3;
    // The colour each sticker is drawn in; replace to light the cube differently.
    std::function<cv::Scalar(solver::Color)> paint = &cube::test::paint;
};

// One cube side as the camera sees it: dark plastic and nine stickers,
// row-major from the top-left.
[[nodiscard]] inline cv::Mat draw_side(const std::array<solver::Color, 9>& stickers,
                                       const Pose& pose, const Drawing& drawing = {}) {
    cv::Mat frame = room(drawing.size);
    fill_square(frame, pose, 0.0F, 0.0F, 1.62F, cv::Scalar(16, 16, 16));
    for (int row = 0; row < drawing.rows; ++row) {
        for (int column = 0; column < 3; ++column) {
            fill_square(frame, pose, static_cast<float>(column - 1), static_cast<float>(row - 1),
                        0.4F, drawing.paint(stickers[static_cast<std::size_t>(row * 3 + column)]));
        }
    }
    return frame;
}

[[nodiscard]] inline std::array<solver::Color, 9> turned_clockwise(
    const std::array<solver::Color, 9>& side) {
    std::array<solver::Color, 9> turned{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            turned[static_cast<std::size_t>(3 * row + column)] =
                side[static_cast<std::size_t>(3 * (2 - column) + row)];
        }
    }
    return turned;
}

// A side of `cube` as a camera pointed at it sees it. The solver's layout is
// camera-handed on U, F, L and mirror-handed on D, R, B (see
// scan_assembly.hpp), so the outside view of those three is the
// column-flipped solver array.
[[nodiscard]] inline std::array<solver::Color, 9> outside_view(const solver::Cube& cube,
                                                               solver::Face face) {
    std::array<solver::Color, 9> stickers = cube.face(face);
    if (face == solver::Face::D || face == solver::Face::R || face == solver::Face::B) {
        for (std::size_t row = 0; row < 3; ++row) {
            std::swap(stickers[row * 3], stickers[row * 3 + 2]);
        }
    }
    return stickers;
}

}  // namespace cube::test
