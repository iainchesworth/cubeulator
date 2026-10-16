#pragma once

// Camera frames of a cube side, drawn rather than recorded, for tests of the
// direct-scan reader and the controller built on it. Real recordings carry
// the person holding the cube and stay out of the repository; the scoreboard
// tool (tools/scan) is what runs the reader over those.
//
// Every drawing is of an N x N side, N taken from the number of stickers it is
// given (nine is a 3x3's, twenty-five a 5x5's), and a 3x3 is what the
// std::array<Color, 9> overloads and the defaults give, so the tests written
// for the 3x3 are unchanged. The sticker spacing (Pose::pitch) is per sticker,
// so a bigger cube is a bigger drawing unless the pose says otherwise.

#include <cube/model/facelet.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <functional>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
//
// Every value is drawn on a statement of its own. The arguments of a call are
// evaluated in an order the standard leaves open, MSVC and GCC take them right
// to left and clang left to right, and `cv::Point(rng.uniform(..),
// rng.uniform(..))` swapped its two coordinates (and the same for the box and
// the colour) from one compiler to the next - a different room, so a side the
// reader found on one was lost on another. The order here is the right-to-left
// one the cases were written against, so that is the room every compiler draws.
[[nodiscard]] inline cv::Mat room(cv::Size size) {
    cv::Mat frame(size, CV_8UC3, cv::Scalar(96, 104, 110));
    cv::RNG rng(1234);
    for (int i = 0; i < 40; ++i) {
        const int top = rng.uniform(0, size.height);
        const int left = rng.uniform(0, size.width);
        const int height = rng.uniform(8, size.height / 5);
        const int width = rng.uniform(8, size.width / 6);
        const int red = rng.uniform(30, 220);
        const int green = rng.uniform(30, 220);
        const int blue = rng.uniform(30, 220);
        cv::rectangle(frame, cv::Rect(cv::Point(left, top), cv::Size(width, height)),
                      cv::Scalar(blue, green, red), cv::FILLED);
    }
    cv::Mat noise(size, CV_8UC3);
    rng.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(6));
    cv::add(frame, noise, frame);
    return frame;
}

// The side length of a square of `count` stickers: 3 for nine, 5 for twenty-five.
[[nodiscard]] inline int side_length(std::size_t count) {
    std::size_t size = 0;
    while ((size + 1) * (size + 1) <= count) {
        ++size;
    }
    if (size < 2 || size * size != count) {
        throw std::invalid_argument("a cube side has N x N stickers, not " + std::to_string(count));
    }
    return static_cast<int>(size);
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
    // How many rows of stickers are in view (a side half out of frame), from the
    // top; all of them unless it says otherwise.
    std::optional<int> rows = std::nullopt;
    // The colour each sticker is drawn in; replace to light the cube differently.
    std::function<cv::Scalar(solver::Color)> paint = &cube::test::paint;
    // A wordmark in four colours across the middle of the centre sticker of a
    // side (of an odd size; an even one has none) whose centre is this colour, the way a Rubik's
    // cube's white centre carries one. It runs corner to corner and covers about a quarter of the
    // sticker, and it is painted with `paint`, so it is lit as the rest is.
    std::optional<solver::Color> logo_on_centre = std::nullopt;
};

// The six colours of a real cube under a warm lamp, measured in float Lab on
// 2026-10-14 from the scan in which two sides were fused into one. Red and
// orange are 16 Lab apart and almost all of that is lightness - the one axis
// colour_distance counts for a quarter - and the blue and green are nearly
// black.
[[nodiscard]] inline cv::Vec3f warm_lamp_lab(solver::Color colour) {
    switch (colour) {
        case solver::Color::White:
            return {65.5F, 22.5F, 16.5F};
        case solver::Color::Yellow:
            return {54.3F, 25.9F, 53.0F};
        case solver::Color::Red:
            return {28.4F, 50.4F, 36.5F};
        case solver::Color::Orange:
            return {41.0F, 56.1F, 46.7F};
        case solver::Color::Green:
            return {16.8F, -12.7F, 14.8F};
        case solver::Color::Blue:
            return {11.0F, 5.5F, -15.5F};
    }
    return {};
}

// A cube drawn in those colours.
[[nodiscard]] inline Drawing warm_lamp_drawing() {
    Drawing drawing;
    drawing.paint = [](solver::Color colour) {
        const cv::Mat3f lab(1, 1, warm_lamp_lab(colour));
        cv::Mat3f bgr;
        cv::cvtColor(lab, bgr, cv::COLOR_Lab2BGR);
        const cv::Vec3f value = bgr(0, 0) * 255.0F;
        return cv::Scalar(cv::saturate_cast<uchar>(value(0)), cv::saturate_cast<uchar>(value(1)),
                          cv::saturate_cast<uchar>(value(2)));
    };
    return drawing;
}

// The wordmark of Drawing::logo_on_centre: four squares stepping along the
// sticker's diagonal.
inline void draw_logo(cv::Mat& frame, const Pose& pose, float column, float row,
                      const Drawing& drawing) {
    constexpr std::array<solver::Color, 4> kLetters = {solver::Color::Green, solver::Color::Blue,
                                                       solver::Color::Yellow, solver::Color::Red};
    constexpr float kStep = 0.13F;
    constexpr float kHalf = 0.11F;
    for (std::size_t i = 0; i < kLetters.size(); ++i) {
        const float along = (static_cast<float>(i) - 1.5F) * kStep;
        fill_square(frame, pose, column + along, row + along, kHalf, drawing.paint(kLetters[i]));
    }
}

// One cube side as the camera sees it: dark plastic and N x N stickers,
// row-major from the top-left.
[[nodiscard]] inline cv::Mat draw_side(std::span<const solver::Color> stickers, const Pose& pose,
                                       const Drawing& drawing = {}) {
    const int size = side_length(stickers.size());
    const auto count = static_cast<std::size_t>(size);
    const float middle = 0.5F * static_cast<float>(size - 1);
    const int rows = std::min(drawing.rows.value_or(size), size);
    cv::Mat frame = room(drawing.size);
    fill_square(frame, pose, 0.0F, 0.0F, (0.5F * static_cast<float>(size)) + 0.12F,
                cv::Scalar(16, 16, 16));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < size; ++column) {
            fill_square(frame, pose, static_cast<float>(column) - middle,
                        static_cast<float>(row) - middle, 0.4F,
                        drawing.paint(stickers[(static_cast<std::size_t>(row) * count) +
                                               static_cast<std::size_t>(column)]));
        }
    }
    if (drawing.logo_on_centre.has_value() && rows == size && size % 2 == 1 &&
        stickers[((count / 2) * count) + (count / 2)] == *drawing.logo_on_centre) {
        draw_logo(frame, pose, 0.0F, 0.0F, drawing);
    }
    return frame;
}

// A finger across one sticker of a drawn side, reaching in from the edge of
// the picture: skin-coloured, narrower than the sticker so some of it still
// shows either side, and covering the middle of it, which is where a sticker
// is sampled. `cell` is row-major, as the camera sees the side.
inline void lay_finger(cv::Mat& frame, const Pose& pose, int cell, int size = 3) {
    const float middle = 0.5F * static_cast<float>(size - 1);
    const auto column = static_cast<float>(cell % size) - middle;
    const auto row = static_cast<float>(cell / size) - middle;
    const cv::Point2f tip = place(pose, column - 0.25F, row);
    // In from the nearer side edge of the picture.
    const float edge = column >= 0.0F ? static_cast<float>(frame.cols) : 0.0F;
    const float half = 0.3F * pose.pitch;
    cv::rectangle(frame, cv::Point2f(std::min(tip.x, edge), tip.y - half),
                  cv::Point2f(std::max(tip.x + 0.5F * pose.pitch, edge), tip.y + half),
                  cv::Scalar(140, 170, 222), cv::FILLED, cv::LINE_AA);
}

// A side turned a quarter turn clockwise in its own plane.
[[nodiscard]] inline std::vector<solver::Color> turned_clockwise(
    std::span<const solver::Color> side) {
    const auto size = static_cast<std::size_t>(side_length(side.size()));
    std::vector<solver::Color> turned(side.size());
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            turned.at((size * row) + column) = side[(size * (size - 1 - column)) + row];
        }
    }
    return turned;
}

[[nodiscard]] inline std::array<solver::Color, 9> turned_clockwise(
    const std::array<solver::Color, 9>& side) {
    const std::vector<solver::Color> turned =
        turned_clockwise(std::span<const solver::Color>(side));
    std::array<solver::Color, 9> out{};
    std::ranges::copy(turned, out.begin());
    return out;
}

// A side of `cube` as a camera pointed at it sees it. The solver's layout is
// camera-handed on U, F, L and mirror-handed on D, R, B (see
// scan_assembly.hpp), so the outside view of those three is the
// column-flipped solver array.
//
// For D that is a half-turn away from cube::solver::outside_view, whose D is
// the top-bottom mirror. A read of one side cannot tell (its roll is not
// known), so square-on drawings do not care; where a side's neighbours
// matter, as in a corner view, draw from cube::solver::outside_view.
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

// The same for a cube of any size: a side of `cube` as a camera pointed at it
// sees it, N x N stickers row-major from the top-left. For a 3x3 it is
// outside_view; the layout of a cube (nxn::NCube is the solver's, for N = 3) is
// camera-handed on U, F, L and column-mirrored on D, R, B at every size.
[[nodiscard]] inline std::vector<solver::Color> camera_view(const nxn::NCube& cube,
                                                            solver::Face face) {
    const int size = cube.size();
    const bool mirrored =
        face == solver::Face::D || face == solver::Face::R || face == solver::Face::B;
    std::vector<solver::Color> stickers;
    stickers.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            stickers.push_back(cube.at(face, row, mirrored ? size - 1 - column : column));
        }
    }
    return stickers;
}

// Whether six sides, each seen square-on and in any roll, make exactly one
// cube. Reading cannot change the answer: it is about the sides themselves.
[[nodiscard]] inline bool sides_make_one_cube(const solver::Cube& cube) {
    const auto facet = [](solver::Color colour) {
        switch (colour) {
            case solver::Color::White:
                return model::FacetColor::white;
            case solver::Color::Yellow:
                return model::FacetColor::yellow;
            case solver::Color::Green:
                return model::FacetColor::green;
            case solver::Color::Blue:
                return model::FacetColor::blue;
            case solver::Color::Orange:
                return model::FacetColor::orange;
            case solver::Color::Red:
                return model::FacetColor::red;
        }
        return model::FacetColor::unknown;
    };
    std::array<std::array<model::Facelet, 9>, 6> faces{};
    std::size_t index = 0;
    for (const solver::Face face : solver::kAllFaces) {
        const std::array<solver::Color, 9> side = cube::test::outside_view(cube, face);
        for (std::size_t i = 0; i < 9; ++i) {
            faces[index][i] = model::Facelet{.color = facet(side[i]), .confidence = 1.0F};
        }
        ++index;
    }
    return solver::assemble_scanned_faces(faces).has_value();
}

// A scrambled cube whose six sides, shown square-on, make exactly one cube.
//
// About one scramble in eighty does not: the sides can be put together in
// more than one valid way, and nothing in six flat views says which, however
// well they are read (measured: 12 of 1000 25-move scrambles). The scan
// reports those as sides that do not make a cube. Which seeds are such cubes
// differs by platform, because random_scramble draws through
// std::uniform_int_distribution, whose algorithm is the standard library's
// own - seed 14 is an ordinary cube on libstdc++ and MSVC and an ambiguous one
// on libc++. A test of the scan wants a cube the scan can read, so it takes
// the first one from `seed` upwards.
[[nodiscard]] inline solver::Cube scramble(unsigned seed) {
    for (unsigned s = seed;; ++s) {
        const solver::Cube cube = solver::random_scramble(s);
        if (sides_make_one_cube(cube)) {
            return cube;
        }
    }
}

// Two sides of a cube turned corner-on: `left` and `right` meet at a vertical
// edge `edge_x` pixels from the left, `edge_y` high, each foreshortened by its
// own column step and tilted towards the horizon by `tilt` per column, so the
// pair reads as the near corner of a cube seen a little from above. Rows are
// vertical, `pitch` apart. Each side is row-major from its top-left as the
// camera sees it, so `left`'s right column is against `right`'s left column.
struct CornerPose {
    float edge_x = 320.0F;
    float edge_y = 180.0F;
    float pitch = 40.0F;
    float left_width = 30.0F;   // column step of the left side, pixels
    float right_width = 24.0F;  // and of the right
    float tilt = 4.0F;          // rise per column away from the shared edge
};

[[nodiscard]] inline cv::Mat draw_corner(std::span<const solver::Color> left,
                                         std::span<const solver::Color> right,
                                         const CornerPose& pose = {}, const Drawing& drawing = {}) {
    const int size = side_length(left.size());
    if (side_length(right.size()) != size) {
        throw std::invalid_argument("the two sides of a corner view are of one cube");
    }
    const auto count = static_cast<std::size_t>(size);
    const auto cells = static_cast<float>(size);
    cv::Mat frame = room(drawing.size);
    const float top = pose.edge_y - (0.5F * cells * pose.pitch);

    // A cell (column, row) of a side whose column step is (dx, dy) and whose
    // cell (0,0) has its top-left corner at `origin`.
    const auto corner = [&](const cv::Point2f& origin, float dx, float dy, float column,
                            float row) {
        return cv::Point(
            cv::Point2f(origin.x + column * dx, origin.y + column * dy + row * pose.pitch));
    };
    const auto fill_side = [&](const cv::Point2f& origin, float dx, float dy,
                               std::span<const solver::Color> stickers) {
        const std::array<cv::Point, 4> backing = {
            corner(origin, dx, dy, -0.08F, -0.08F),
            corner(origin, dx, dy, cells + 0.08F, -0.08F),
            corner(origin, dx, dy, cells + 0.08F, cells + 0.08F),
            corner(origin, dx, dy, -0.08F, cells + 0.08F),
        };
        cv::fillConvexPoly(frame, backing.data(), 4, cv::Scalar(16, 16, 16), cv::LINE_AA);
        for (int row = 0; row < size; ++row) {
            for (int column = 0; column < size; ++column) {
                const float c = static_cast<float>(column);
                const float r = static_cast<float>(row);
                const std::array<cv::Point, 4> quad = {
                    corner(origin, dx, dy, c + 0.1F, r + 0.1F),
                    corner(origin, dx, dy, c + 0.9F, r + 0.1F),
                    corner(origin, dx, dy, c + 0.9F, r + 0.9F),
                    corner(origin, dx, dy, c + 0.1F, r + 0.9F),
                };
                cv::fillConvexPoly(frame, quad.data(), 4,
                                   drawing.paint(stickers[(static_cast<std::size_t>(row) * count) +
                                                          static_cast<std::size_t>(column)]),
                                   cv::LINE_AA);
            }
        }
    };

    // Left side: its right edge is the shared one; columns step right and down
    // towards it, so its far (left) edge is higher.
    fill_side({pose.edge_x - (cells * pose.left_width), top - (cells * pose.tilt)}, pose.left_width,
              pose.tilt, left);
    // Right side: its left edge is the shared one; columns step right and up.
    fill_side({pose.edge_x, top}, pose.right_width, -pose.tilt, right);
    return frame;
}

[[nodiscard]] inline cv::Mat draw_corner(const std::array<solver::Color, 9>& left,
                                         const std::array<solver::Color, 9>& right,
                                         const CornerPose& pose = {}, const Drawing& drawing = {}) {
    return draw_corner(std::span<const solver::Color>(left), std::span<const solver::Color>(right),
                       pose, drawing);
}

// Three sides of a cube turned so its near corner points at the camera: the
// hexagon a cube makes when seen from a corner, three rhombi meeting at
// `vertex`, each cube edge `edge` pixels long and the picture turned
// `degrees` in its plane. The edges leave the vertex up, down-left and
// down-right; the sides, in order, lie between (down-left, up), (down-right,
// down-left) and (up, down-right), each with its steps turning clockwise so
// that a side's array is as it looks from outside, row-major from the corner
// the reader would call its first. Side `i`'s cells are `sides[i]` as given.
struct Corner3Pose {
    cv::Point2f vertex{320.0F, 190.0F};
    float edge = 120.0F;
    float degrees = 0.0F;
};

// For three sides of any one size. A template only so that a braced list of
// three 3x3 sides, which this signature could take as well as the next one,
// keeps meaning the next one.
template <typename Sides>
    requires std::same_as<Sides, std::array<std::span<const solver::Color>, 3>>
[[nodiscard]] inline cv::Mat draw_corner3(const Sides& sides, const Corner3Pose& pose = {},
                                          const Drawing& drawing = {}) {
    const int size = side_length(sides[0].size());
    if (side_length(sides[1].size()) != size || side_length(sides[2].size()) != size) {
        throw std::invalid_argument("the three sides of a corner view are of one cube");
    }
    const auto count = static_cast<std::size_t>(size);
    const auto cells = static_cast<float>(size);
    cv::Mat frame = room(drawing.size);
    const float radians = pose.degrees * std::numbers::pi_v<float> / 180.0F;
    const auto turned = [&](float x, float y) {
        return cv::Point2f(x * std::cos(radians) - y * std::sin(radians),
                           x * std::sin(radians) + y * std::cos(radians));
    };
    const cv::Point2f up = turned(0.0F, -pose.edge);
    const cv::Point2f down_left = turned(-0.866F * pose.edge, 0.5F * pose.edge);
    const cv::Point2f down_right = turned(0.866F * pose.edge, 0.5F * pose.edge);

    // A side as the parallelogram spanned from the vertex by `columns` and
    // `rows` (each N cells long), with gaps of plastic between stickers.
    const auto fill_side = [&](const cv::Point2f& columns, const cv::Point2f& rows,
                               std::span<const solver::Color> stickers) {
        const auto at = [&](float column, float row) {
            return cv::Point(pose.vertex + (column / cells) * columns + (row / cells) * rows);
        };
        const std::array<cv::Point, 4> backing = {
            at(-0.04F, -0.04F),
            at(cells + 0.04F, -0.04F),
            at(cells + 0.04F, cells + 0.04F),
            at(-0.04F, cells + 0.04F),
        };
        cv::fillConvexPoly(frame, backing.data(), 4, cv::Scalar(16, 16, 16), cv::LINE_AA);
        for (int row = 0; row < size; ++row) {
            for (int column = 0; column < size; ++column) {
                const float c = static_cast<float>(column);
                const float r = static_cast<float>(row);
                const std::array<cv::Point, 4> quad = {
                    at(c + 0.1F, r + 0.1F),
                    at(c + 0.9F, r + 0.1F),
                    at(c + 0.9F, r + 0.9F),
                    at(c + 0.1F, r + 0.9F),
                };
                cv::fillConvexPoly(frame, quad.data(), 4,
                                   drawing.paint(stickers[(static_cast<std::size_t>(row) * count) +
                                                          static_cast<std::size_t>(column)]),
                                   cv::LINE_AA);
            }
        }
    };
    fill_side(down_left, up, sides[0]);
    fill_side(down_right, down_left, sides[1]);
    fill_side(up, down_right, sides[2]);
    return frame;
}

[[nodiscard]] inline cv::Mat draw_corner3(const std::array<std::array<solver::Color, 9>, 3>& sides,
                                          const Corner3Pose& pose = {},
                                          const Drawing& drawing = {}) {
    const std::array<std::span<const solver::Color>, 3> spans = {
        std::span<const solver::Color>(sides[0]),
        std::span<const solver::Color>(sides[1]),
        std::span<const solver::Color>(sides[2]),
    };
    return draw_corner3(spans, pose, drawing);
}

// The cube of the first scan recorded with a cube held on corners by hand: a
// pattern with the orange and red sides solid and the green, blue, white and
// yellow sides six and three, the three being two opposite corners and an edge.
// Its six sides make twenty cubes, which is what makes it ambiguous; this is
// one of them, as `cube.face(face)` lays each side out.
[[nodiscard]] inline std::optional<solver::Cube> patterned_cube() {
    const auto side = [](std::string_view letters) {
        std::array<solver::Color, 9> colours{};
        for (std::size_t i = 0; i < 9; ++i) {
            switch (letters[i]) {
                case 'W':
                    colours[i] = solver::Color::White;
                    break;
                case 'Y':
                    colours[i] = solver::Color::Yellow;
                    break;
                case 'R':
                    colours[i] = solver::Color::Red;
                    break;
                case 'O':
                    colours[i] = solver::Color::Orange;
                    break;
                case 'G':
                    colours[i] = solver::Color::Green;
                    break;
                default:
                    colours[i] = solver::Color::Blue;
                    break;
            }
        }
        return colours;
    };
    std::array<std::array<solver::Color, 9>, 6> faces{};
    const auto set = [&faces](solver::Face face, const std::array<solver::Color, 9>& colours) {
        faces[static_cast<std::size_t>(face)] = colours;
    };
    set(solver::Face::U, side("YYWYYYWWY"));
    set(solver::Face::D, side("YWWWWWWYY"));
    set(solver::Face::L, side("RRRRRRRRR"));
    set(solver::Face::R, side("OOOOOOOOO"));
    set(solver::Face::F, side("BBGGGGGGB"));
    set(solver::Face::B, side("BBGBBBGGB"));
    const auto cube = solver::Cube::from_facelets(faces);
    return cube.has_value() ? std::optional<solver::Cube>(*cube) : std::nullopt;
}

}  // namespace cube::test
