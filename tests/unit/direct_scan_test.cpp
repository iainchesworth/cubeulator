#include <catch2/catch_test_macros.hpp>

#include "drawn_cube.hpp"

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <random>
#include <string_view>

using cube::CubeError;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::test::draw_side;
using cube::test::Drawing;
using cube::test::outside_view;
using cube::test::paint;
using cube::test::place;
using cube::test::Pose;
using cube::test::room;
using cube::test::turned_clockwise;
using cube::vision::read_side;
using cube::vision::SideRead;
using cube::vision::SideScan;

namespace {

constexpr std::array<Color, 6> kColours = {Color::White, Color::Yellow, Color::Green,
                                           Color::Blue,  Color::Orange, Color::Red};

constexpr std::array<Color, 9> kMixed = {Color::Green, Color::Orange, Color::Blue,
                                         Color::Blue,  Color::White,  Color::Yellow,
                                         Color::Green, Color::Red,    Color::White};

cv::Vec3f lab_of(const cv::Scalar& bgr) {
    const cv::Mat3f source(
        1, 1,
        cv::Vec3f(static_cast<float>(bgr[0]) / 255.0F, static_cast<float>(bgr[1]) / 255.0F,
                  static_cast<float>(bgr[2]) / 255.0F));
    cv::Mat3f lab;
    cv::cvtColor(source, lab, cv::COLOR_BGR2Lab);
    return lab(0, 0);
}

Color nearest_colour(const cv::Vec3f& lab) {
    Color best = Color::White;
    double best_distance = std::numeric_limits<double>::max();
    for (const Color colour : kColours) {
        const double distance = cv::norm(lab - lab_of(paint(colour)));
        if (distance < best_distance) {
            best_distance = distance;
            best = colour;
        }
    }
    return best;
}

Cube after(std::string_view algorithm) {
    Cube cube = Cube::solved();
    for (const Move move : *cube::solver::parse_algorithm(algorithm)) {
        cube = cube.apply(move);
    }
    return cube;
}

// Hold one side still in front of the camera for `frames` frames.
void show(SideScan& scan, const std::array<Color, 9>& stickers, const Pose& pose,
          const Drawing& drawing = {}, int frames = 12) {
    const cv::Mat frame = draw_side(stickers, pose, drawing);
    for (int i = 0; i < frames; ++i) {
        scan.add(read_side(frame));
    }
}

// Show all six sides in turn, each in an arbitrary orientation and position.
void show_cube(SideScan& scan, const Cube& cube, unsigned seed, const Drawing& drawing = {}) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> turns(0, 3);
    std::uniform_real_distribution<float> tilt(-9.0F, 9.0F);
    std::uniform_real_distribution<float> shift(-40.0F, 40.0F);
    for (const Face face : kAllFaces) {
        std::array<Color, 9> stickers = outside_view(cube, face);
        for (int i = turns(rng); i > 0; --i) {
            stickers = turned_clockwise(stickers);
        }
        show(
            scan, stickers,
            Pose{.centre = {320.0F + shift(rng), 180.0F + shift(rng) / 2.0F}, .degrees = tilt(rng)},
            drawing);
        scan.add(std::nullopt);  // the cube turns to its next side
    }
}

}  // namespace

TEST_CASE("a side held square to the camera is read row-major as the camera sees it",
          "[direct-scan]") {
    for (const float degrees : {0.0F, 11.0F, -17.0F}) {
        const Pose pose{.centre = {300.0F, 170.0F}, .pitch = 42.0F, .degrees = degrees};
        const std::optional<SideRead> read = read_side(draw_side(kMixed, pose));

        REQUIRE(read.has_value());
        CHECK(read->stickers_found == 9);
        CHECK(std::abs(read->pitch - pose.pitch) < 1.5F);
        for (int i = 0; i < 9; ++i) {
            const auto index = static_cast<std::size_t>(i);
            const cv::Point2f expected =
                place(pose, static_cast<float>(i % 3 - 1), static_cast<float>(i / 3 - 1));
            CHECK(cv::norm(read->centres[index] - expected) < 2.0);
            CHECK(cv::norm(read->lab[index] - lab_of(paint(kMixed[index]))) < 3.0);
        }
    }
}

TEST_CASE("a frame larger than the working size is read in its own pixels", "[direct-scan]") {
    const Pose pose{.centre = {960.0F, 520.0F}, .pitch = 120.0F, .degrees = 6.0F};
    const std::optional<SideRead> read =
        read_side(draw_side(kMixed, pose, Drawing{.size = {1920, 1080}}));

    REQUIRE(read.has_value());
    CHECK(std::abs(read->pitch - pose.pitch) < 4.0F);
    CHECK(cv::norm(read->centres[4] - pose.centre) < 5.0);
    CHECK(cv::norm(read->centres[0] - place(pose, -1.0F, -1.0F)) < 5.0);
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(nearest_colour(read->lab[i]) == kMixed[i]);
    }
}

TEST_CASE("frames without a whole square-on side give no read", "[direct-scan]") {
    SECTION("an empty room") {
        CHECK_FALSE(read_side(room({640, 360})).has_value());
    }
    SECTION("no frame at all") {
        CHECK_FALSE(read_side(cv::Mat()).has_value());
    }
    SECTION("only two rows of stickers in view") {
        // Six stickers fit a lattice, but where it sits on the side is a row out.
        CHECK_FALSE(read_side(draw_side(kMixed, Pose{}, Drawing{.rows = 2})).has_value());
    }
    SECTION("a side seen at an angle") {
        CHECK_FALSE(read_side(draw_side(kMixed, Pose{.squash = 0.6F})).has_value());
    }
}

TEST_CASE("a side counts once it has been held still", "[direct-scan]") {
    SideScan scan;
    const cv::Mat frame = draw_side(kMixed, Pose{});

    // The first frame has nothing before it to be steady against.
    scan.add(read_side(frame));
    CHECK_FALSE(scan.reading());
    for (std::size_t i = 1; i < SideScan::kMinReads; ++i) {
        scan.add(read_side(frame));
        CHECK(scan.reading());
    }
    CHECK(scan.sides().empty());

    scan.add(read_side(frame));
    REQUIRE(scan.sides().size() == 1);
    CHECK(scan.sides().front().reads == SideScan::kMinReads);
    CHECK(scan.sides().front().in_view);

    scan.add(std::nullopt);
    CHECK_FALSE(scan.reading());
    CHECK_FALSE(scan.sides().front().in_view);
    CHECK(scan.cube().error() == CubeError::face_capture_incomplete);
}

TEST_CASE("a side that moves between frames is not read", "[direct-scan]") {
    SideScan scan;
    for (int i = 0; i < 30; ++i) {
        scan.add(read_side(
            draw_side(kMixed, Pose{.centre = {200.0F + 9.0F * static_cast<float>(i), 180.0F}})));
        CHECK_FALSE(scan.reading());
    }
    CHECK(scan.sides().empty());
}

TEST_CASE("the same side shown again in another orientation is still one side", "[direct-scan]") {
    SideScan scan;
    show(scan, kMixed, Pose{});
    scan.add(std::nullopt);
    const std::array<Color, 9> turned = turned_clockwise(kMixed);
    show(scan, turned, Pose{.centre = {350.0F, 200.0F}, .degrees = 7.0F});

    REQUIRE(scan.sides().size() == 1);
    // Reported the way it was last shown, so the display matches the hand.
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(nearest_colour(scan.sides().front().lab[i]) == turned[i]);
    }
}

TEST_CASE("six sides shown in any order and orientation give the cube", "[direct-scan]") {
    for (const unsigned seed : {1U, 2U, 3U, 4U, 5U}) {
        const Cube cube = cube::solver::random_scramble(seed);
        SideScan scan;
        show_cube(scan, cube, seed);

        REQUIRE(scan.sides().size() == 6);
        const auto read = scan.cube();
        REQUIRE(read.has_value());
        CHECK(*read == cube);
    }
}

TEST_CASE("a side of one solid colour does not hold up the read", "[direct-scan]") {
    // Sune on the back layer: the front stays solid green, and a solid side
    // looks the same in all four orientations. Those are one cube, not four.
    const Cube cube = after("R B R' B R B2 R'");
    REQUIRE(cube.face(Face::F) == Cube::solved().face(Face::F));

    SideScan scan;
    show_cube(scan, cube, 7);

    const auto read = scan.cube();
    REQUIRE(read.has_value());
    CHECK(*read == cube);
}

TEST_CASE("two colours that trade names under a colour cast are told apart by the cube itself",
          "[direct-scan]") {
    // The six centre colours measured on a real scan by tungsten light
    // (live-demo-p6-retry, 2026-08-07). Red and orange sit three degrees of
    // hue apart there and differ mostly in lightness - not enough to say
    // which is which, and colour naming alone gets them backwards. Only one
    // of the two namings is a cube that can exist.
    const Drawing lamp_lit{.paint = [](Color colour) {
        const auto measured = [colour]() -> cv::Vec3f {
            switch (colour) {
                case Color::White:
                    return {46.2F, 16.6F, 5.9F};
                case Color::Yellow:
                    return {49.8F, 19.9F, 53.7F};
                case Color::Red:
                    return {25.6F, 48.1F, 36.3F};
                case Color::Orange:
                    return {34.3F, 53.5F, 45.4F};
                case Color::Green:
                    return {17.4F, -12.5F, 15.0F};
                case Color::Blue:
                    return {11.3F, 12.1F, -25.8F};
            }
            return {};
        }();
        const cv::Vec3b bgr = cube::vision::lab_to_bgr(measured);
        return cv::Scalar(bgr[0], bgr[1], bgr[2]);
    }};

    for (const unsigned seed : {21U, 22U, 23U}) {
        const Cube cube = cube::solver::random_scramble(seed);
        SideScan scan;
        show_cube(scan, cube, seed, lamp_lit);
        REQUIRE(scan.sides().size() == 6);

        // The premise: named by colour alone, these sides are not a cube.
        std::array<cube::vision::CapturedFace, 6> faces{};
        for (std::size_t i = 0; i < 6; ++i) {
            faces[i].lab = scan.sides()[i].lab;
        }
        const auto named = cube::vision::resolve_colors(faces);
        REQUIRE(named.has_value());
        REQUIRE_FALSE(cube::solver::assemble_scanned_faces(*named).has_value());

        const auto read = scan.cube();
        REQUIRE(read.has_value());
        CHECK(*read == cube);
    }
}

TEST_CASE("a stray capture gives way to a side that is shown for longer", "[direct-scan]") {
    const Cube cube = cube::solver::random_scramble(31);
    SideScan scan;
    // Something that looked like a side for just long enough to be captured.
    show(scan, kMixed, Pose{}, {}, static_cast<int>(SideScan::kMinReads) + 1);
    scan.add(std::nullopt);
    REQUIRE(scan.sides().size() == 1);

    show_cube(scan, cube, 31);

    // Seven groups are captured; the six shown are the six the cube is made
    // from, and the stray one is not among them.
    REQUIRE(scan.sides().size() == 6);
    const auto read = scan.cube();
    REQUIRE(read.has_value());
    CHECK(*read == cube);
    for (const auto& side : scan.sides()) {
        CHECK(side.reads > SideScan::kMinReads);
    }
}

TEST_CASE("a scan can be started over", "[direct-scan]") {
    SideScan scan;
    show_cube(scan, cube::solver::random_scramble(11), 11);
    REQUIRE(scan.cube().has_value());

    scan.reset();
    CHECK(scan.sides().empty());
    CHECK(scan.cube().error() == CubeError::face_capture_incomplete);
}
