#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "deterministic_rng.hpp"
#include "drawn_cube.hpp"

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/prepared_frame.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <random>
#include <string_view>
#include <vector>
#include "deterministic_rng.hpp"
#include "must_have.hpp"

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
using cube::vision::prepare_frame;
using cube::vision::read_side;
using cube::vision::SideRead;
using cube::vision::SideScan;

namespace {

constexpr std::array<Color, 6> kColours = {
    Color::White, Color::Yellow, Color::Green, Color::Blue, Color::Orange, Color::Red,
};

constexpr std::array<Color, 9> kMixed = {
    Color::Green,  Color::Orange, Color::Blue, Color::Blue,  Color::White,
    Color::Yellow, Color::Green,  Color::Red,  Color::White,
};

cv::Vec3f lab_of(const cv::Scalar& bgr) {
    const cv::Mat3f source(
        1, 1,
        cv::Vec3f(static_cast<float>(bgr(0)) / 255.0F, static_cast<float>(bgr(1)) / 255.0F,
                  static_cast<float>(bgr(2)) / 255.0F));
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

constexpr int kHeldFrames = 12;

// Hold one side still in front of the camera for `frames` frames.
void show(SideScan& scan, const std::array<Color, 9>& stickers, const Pose& pose,
          int frames = kHeldFrames, const Drawing& drawing = {}) {
    const cv::Mat frame = draw_side(stickers, pose, drawing);
    for (int i = 0; i < frames; ++i) {
        scan.add(read_side(frame));
    }
}

// How a side is held up to the camera by show_cube: turned a number of quarter
// turns, and placed and tilted a little.
struct Placement {
    std::uint32_t turns = 0;
    Pose pose;
};

// The next side's placement, drawn from `rng`.
//
// The quarter-turns are the top two bits of the engine's word. std::mt19937's
// output is specified exactly; what std::uniform_int_distribution(0, 3) does
// with it is not - MSVC and libstdc++ take the two high bits and libc++ the
// two low ones - so the same seed turned every side differently on
// WebAssembly. A side drawn at one turn is a frame the reader can find and at
// another it is not (a dark green column against the lamp's white), and the
// case that was written against the high bits was given a turn on which the
// white side was not read, and found five sides. The tilt and shift are
// uniform_real_distribution: one draw of the engine and a formula the standard
// gives, and the libraries agree on them to the last pixel.
Placement next_placement(std::mt19937& rng) {
    std::uniform_real_distribution<float> tilt(-9.0F, 9.0F);
    std::uniform_real_distribution<float> shift(-40.0F, 40.0F);
    Placement placement;
    // mt19937's result_type is a 64-bit unsigned long on LP64, and only the top
    // two bits of a 32-bit draw are wanted: the cast says so.
    placement.turns = static_cast<std::uint32_t>(rng() >> 30U);
    placement.pose = Pose{
        .centre = {320.0F + shift(rng), 180.0F + (shift(rng) / 2.0F)},
        .degrees = tilt(rng),
    };
    return placement;
}

// Show all six sides in turn, each in an arbitrary orientation and position.
void show_cube(SideScan& scan, const Cube& cube, unsigned seed, const Drawing& drawing = {}) {
    std::mt19937 rng(seed);
    for (const Face face : kAllFaces) {
        const Placement placement = next_placement(rng);
        std::array<Color, 9> stickers = outside_view(cube, face);
        for (std::uint32_t turns = placement.turns; turns > 0; --turns) {
            stickers = turned_clockwise(stickers);
        }
        show(scan, stickers, placement.pose, kHeldFrames, drawing);
        scan.add(std::nullopt);  // the cube turns to its next side
    }
}

// The six centre colours measured on a real scan by tungsten light
// (live-demo-p6-retry, 2026-08-07). Red and orange sit three degrees of hue
// apart there and differ mostly in lightness - not enough to say which is
// which, and colour naming alone gets them backwards. Only one of the two
// namings is a cube that can exist.
// `pale_green` lifts the green from L 17 to 30: as pale as the lamp's white
// is pink, which leaves find_white_reference no margin to tell the white by
// and so no light to take out.
Drawing tungsten_lamp(bool pale_green = false) {
    return Drawing{
        .paint =
            [pale_green](Color colour) {
                const auto measured = [colour, pale_green] -> cv::Vec3f {
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
                            return {pale_green ? 30.0F : 17.4F, -12.5F, 15.0F};
                        case Color::Blue:
                            return {11.3F, 12.1F, -25.8F};
                    }
                    return {};
                }();
                const cv::Vec3b bgr = cube::vision::lab_to_bgr(measured);
                return cv::Scalar(bgr(0), bgr(1), bgr(2));
            },
    };
}

}  // namespace

TEST_CASE("a side held square to the camera is read row-major as the camera sees it",
          "[direct-scan]") {
    for (const float degrees : {0.0F, 11.0F, -17.0F}) {
        const Pose pose{.centre = {300.0F, 170.0F}, .pitch = 42.0F, .degrees = degrees};
        const std::optional<SideRead> read = read_side(draw_side(kMixed, pose));

        const auto& read_value = ::cube::test::must_have(read);
        CHECK(read_value.stickers_found == 9);
        CHECK(std::abs(read_value.pitch - pose.pitch) < 1.5F);
        for (int i = 0; i < 9; ++i) {
            const auto index = static_cast<std::size_t>(i);
            const int column = (i % 3) - 1;
            const int row = (i / 3) - 1;
            const cv::Point2f expected =
                place(pose, static_cast<float>(column), static_cast<float>(row));
            CHECK(cv::norm(read_value.centres.at(index) - expected) < 2.0);
            CHECK(cv::norm(read_value.lab.at(index) - lab_of(paint(kMixed.at(index)))) < 3.0);
        }
    }
}

TEST_CASE("a logo across a centre sticker does not change the colour it reads", "[direct-scan]") {
    // A Rubik's cube's white centre carries a wordmark in four colours across
    // its middle, which is where a sticker is sampled. Over 84 reads of such a
    // side in the recorded sessions the centre sat 13 Lab units from the white
    // stickers beside it, nearly all of it lightness. Cell 4 of kMixed is the
    // white centre and cell 8 a white corner.
    const Pose pose{.centre = {300.0F, 170.0F}, .pitch = 42.0F};
    const Drawing marked{.logo_on_centre = Color::White};

    const std::optional<SideRead> plain = read_side(draw_side(kMixed, pose));
    const std::optional<SideRead> logo = read_side(draw_side(kMixed, pose, marked));
    const auto& plain_read = ::cube::test::must_have(plain);
    const auto& logo_read = ::cube::test::must_have(logo);

    CHECK(logo_read.stickers_found == 9);
    CHECK(cv::norm(logo_read.lab.at(4) - plain_read.lab.at(4)) < 3.0);
    CHECK(cv::norm(logo_read.lab.at(4) - logo_read.lab.at(8)) < 3.0);
    // The other eight stickers are not what the logo is on.
    for (const std::size_t i : {0U, 1U, 2U, 3U, 5U, 6U, 7U, 8U}) {
        CHECK(cv::norm(logo_read.lab.at(i) - plain_read.lab.at(i)) < 1.0);
    }
}

TEST_CASE("a logo on a white centre does not tint it by lamplight", "[direct-scan]") {
    // The same side under an evening lamp: everything shifted towards red,
    // the wordmark's red letter included. The white centre has to come out
    // the colour of the white beside it, and not pinker.
    const Drawing lamp_lit{
        .paint =
            [](Color colour) {
                const cv::Scalar white = paint(colour);
                return cv::Scalar(0.70 * white(0), 0.86 * white(1), 1.00 * white(2));
            },
        .logo_on_centre = Color::White,
    };
    const Pose pose{.centre = {300.0F, 170.0F}, .pitch = 42.0F};

    const std::optional<SideRead> read = read_side(draw_side(kMixed, pose, lamp_lit));
    const auto& side = ::cube::test::must_have(read);

    // A hue shift is what the eye catches: compare chroma direction too.
    const cv::Vec3f centre = side.lab.at(4);
    const cv::Vec3f beside = side.lab.at(8);
    CHECK(cv::norm(centre - beside) < 3.0);
    CHECK(std::abs(centre(1) - beside(1)) < 2.0F);
    CHECK(std::abs(centre(2) - beside(2)) < 2.0F);
}

TEST_CASE("a logo on a centre does not stop six sides making the cube", "[direct-scan]") {
    for (const unsigned seed : {41U, 42U, 43U}) {
        const Cube cube = cube::test::scramble(seed);
        SideScan scan;
        show_cube(scan, cube, seed, Drawing{.logo_on_centre = Color::White});

        REQUIRE(scan.sides().size() == 6);
        const auto read = scan.cube();
        REQUIRE(read.has_value());
        CHECK(*read == cube);

        // What the screen shows for the white side: its centre in the white
        // of the stickers around it.
        for (const SideScan::Side& side : scan.sides()) {
            if (nearest_colour(side.lab.at(4)) != Color::White) {
                continue;
            }
            const cv::Vec3f white = lab_of(paint(Color::White));
            CHECK(cv::norm(side.lab.at(4) - white) < 4.0);
        }
    }
}

TEST_CASE("a frame larger than the working size is read in its own pixels", "[direct-scan]") {
    const Pose pose{.centre = {960.0F, 520.0F}, .pitch = 120.0F, .degrees = 6.0F};
    const std::optional<SideRead> read =
        read_side(draw_side(kMixed, pose, Drawing{.size = {1920, 1080}}));

    const auto& read_value = ::cube::test::must_have(read);
    CHECK(std::abs(read_value.pitch - pose.pitch) < 4.0F);
    CHECK(cv::norm(read_value.centres.at(4) - pose.centre) < 5.0);
    CHECK(cv::norm(read_value.centres.at(0) - place(pose, -1.0F, -1.0F)) < 5.0);
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(nearest_colour(read_value.lab.at(i)) == kMixed.at(i));
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

    CHECK(scan.reads_in_view() == SideScan::kMinReads - 1);

    scan.add(read_side(frame));
    REQUIRE(scan.sides().size() == 1);
    CHECK(scan.sides().front().reads == SideScan::kMinReads);
    CHECK(scan.sides().front().in_view);
    CHECK(scan.reads_in_view() == SideScan::kMinReads);

    scan.add(std::nullopt);
    CHECK_FALSE(scan.reading());
    CHECK(scan.reads_in_view() == 0);
    CHECK_FALSE(scan.sides().front().in_view);
    CHECK(scan.cube().error() == CubeError::face_capture_incomplete);
}

TEST_CASE("a side that moves between frames is not read", "[direct-scan]") {
    SideScan scan;
    for (int i = 0; i < 30; ++i) {
        scan.add(read_side(
            draw_side(kMixed, Pose{.centre = {200.0F + (9.0F * static_cast<float>(i)), 180.0F}})));
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
        CHECK(nearest_colour(scan.sides().front().lab.at(i)) == turned.at(i));
    }
}

TEST_CASE("six sides shown in any order and orientation give the cube", "[direct-scan]") {
    for (const unsigned seed : {1U, 2U, 3U, 4U, 5U}) {
        const Cube cube = cube::test::scramble(seed);
        SideScan scan;
        show_cube(scan, cube, seed);

        REQUIRE(scan.sides().size() == 6);
        const auto read = scan.cube();
        REQUIRE(read.has_value());
        CHECK(*read == cube);
    }
}

TEST_CASE("sides that put together two ways are reported, not guessed", "[direct-scan]") {
    // What libc++'s random_scramble(14) draws. Read perfectly, its six sides
    // are still two valid cubes: each is seen square-on and in a roll the
    // reader cannot know, and nothing in the flat views says which roll goes
    // with which. About one scramble in eighty is like this (see
    // cube::test::scramble). Naming one would be a coin toss, so the scan
    // says the sides are ambiguous - until a corner view settles it
    // (corner_view_test.cpp).
    const Cube cube = after("R F' D' L L B B' D' R F2 L' U' F2 B B2 U B2 R D' U2 R2 L' L2 F L2");
    REQUIRE_FALSE(cube::test::sides_make_one_cube(cube));

    SideScan scan;
    show_cube(scan, cube, 14);

    CHECK(scan.sides().size() == 6);
    const auto read = scan.cube();
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error() == cube::CubeError::ambiguous_scan);
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
    const Drawing lamp_lit = tungsten_lamp();

    for (const unsigned seed : {21U, 22U, 23U}) {
        const Cube cube = cube::test::scramble(seed);
        SideScan scan;
        show_cube(scan, cube, seed, lamp_lit);
        REQUIRE(scan.sides().size() == 6);

        // The premise: named by colour alone, these sides are not a cube.
        std::array<cube::vision::CapturedFace, 6> faces{};
        for (std::size_t i = 0; i < 6; ++i) {
            faces.at(i).lab = scan.sides().at(i).lab;
        }
        const auto named = cube::vision::resolve_colors(faces);
        REQUIRE(named.has_value());
        REQUIRE_FALSE(cube::solver::assemble_scanned_faces(*named).has_value());

        const auto read = scan.cube();
        REQUIRE(read.has_value());
        CHECK(*read == cube);
    }
}

namespace {

using Lab9 = std::array<cv::Vec3f, 9>;

cv::Vec3f lamp_red() {
    return cube::test::warm_lamp_lab(Color::Red);
}
cv::Vec3f lamp_orange() {
    return cube::test::warm_lamp_lab(Color::Orange);
}
Drawing lamp_drawing() {
    return cube::test::warm_lamp_drawing();
}

// A read as the reader gives it, from nine measured colours: the lattice
// stays where it is, so every read after the first of a hold is steady.
SideRead read_of(const Lab9& lab) {
    SideRead read;
    read.lab = lab;
    read.pitch = 40.0F;
    read.stickers_found = 9;
    read.found.fill(true);
    for (std::size_t i = 0; i < 9; ++i) {
        const std::size_t column = i % 3;
        const std::size_t row = i / 3;
        read.centres.at(i) = {100.0F + (40.0F * static_cast<float>(column)),
                              100.0F + (40.0F * static_cast<float>(row))};
    }
    return read;
}

Lab9 red_orange_side(std::initializer_list<std::size_t> orange_cells) {
    Lab9 lab;
    lab.fill(lamp_red());
    for (const std::size_t cell : orange_cells) {
        lab.at(cell) = lamp_orange();
    }
    return lab;
}

// The two sides of a partly solved cube that look the same but for which of
// red and orange sits where: the orange-centred one has orange on a P of
// five stickers and red on an L of four; the red-centred one has them the
// other way about. They differ in three stickers, the centre one of them.
Lab9 orange_centred() {
    return red_orange_side({1, 2, 4, 5, 8});
}
Lab9 red_centred() {
    return red_orange_side({0, 1, 2, 5});
}

Lab9 turned_once(const Lab9& side) {
    constexpr std::array<std::size_t, 9> kSource = {6, 3, 0, 7, 4, 1, 8, 5, 2};
    Lab9 out{};
    for (std::size_t i = 0; i < 9; ++i) {
        out.at(i) = side.at(kSource.at(i));
    }
    return out;
}

// What a camera does to a held side: noise on every sticker, a lamp that
// drifts darker, a thumb over a sticker now and then, a glare across the
// centre now and then.
struct Hand {
    std::mt19937 rng = cube::test::deterministic_rng(7);
    float drift = 1.0F;
    bool glare_and_thumbs = true;

    Lab9 look(const Lab9& side) {
        std::normal_distribution<float> noise(0.0F, 1.6F);
        std::uniform_real_distribution<float> chance(0.0F, 1.0F);
        std::uniform_int_distribution<int> cell(0, 8);
        Lab9 lab = side;
        for (cv::Vec3f& sticker : lab) {
            sticker = cv::Vec3f((sticker(0) * drift) + noise(rng), sticker(1) + noise(rng),
                                sticker(2) + noise(rng));
        }
        if (glare_and_thumbs) {
            if (chance(rng) < 0.15F) {
                const auto at = static_cast<std::size_t>(cell(rng));
                if (at != 4) {
                    lab.at(at) = cv::Vec3f(62.0F, 18.0F, 22.0F);  // skin
                }
            }
            if (chance(rng) < 0.06F) {
                lab.at(4) += cv::Vec3f(8.0F, -6.0F, 14.0F);
            }
        }
        return lab;
    }
};

void hold_lab(SideScan& scan, Hand& hand, const Lab9& side, int reads) {
    for (int i = 0; i < reads; ++i) {
        scan.add(read_of(hand.look(side)));
    }
    scan.add(std::nullopt);
}

float lightness_of(const cv::Vec3f& lab) {
    return lab(0);
}

// Each side's centre, read back as the colour of that face's centre. The
// sides are shown in the order of kAllFaces, so side i is face i.
void expect_centres_of(const SideScan& scan, const Cube& cube) {
    // A copy: sides() returns its vector by value, and what is taken from
    // it below must outlive the call.
    const std::vector<SideScan::Side> sides = scan.sides();
    REQUIRE(sides.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
        const Color centre = cube.face(kAllFaces.at(i)).at(4);
        const cv::Vec3f& measured = sides.at(i).lab.at(4);
        Color nearest = Color::White;
        float best = std::numeric_limits<float>::max();
        for (const Color colour : kColours) {
            const float gap =
                static_cast<float>(cv::norm(measured - cube::test::warm_lamp_lab(colour)));
            if (gap < best) {
                best = gap;
                nearest = colour;
            }
        }
        CHECK(nearest == centre);
    }
}

}  // namespace

TEST_CASE("two sides that differ only in where red and orange sit are two sides", "[direct-scan]") {
    // Shown alternately, as a hand turns a partly solved cube over. Judged by
    // the mean of colour_distance under 12 they were one side - a whole side
    // of red and orange traded scores under 10 a sticker - and the scan's
    // medians were a blend of both.
    SideScan scan;
    Hand hand{.glare_and_thumbs = false};
    for (int round = 0; round < 4; ++round) {
        hold_lab(scan, hand, orange_centred(), 14);
        hold_lab(scan, hand, red_centred(), 14);
    }

    const std::vector<SideScan::Side> sides = scan.sides();
    REQUIRE(sides.size() == 2);
    const SideScan::Side& orange_side = sides.at(0);
    const SideScan::Side& red_side = sides.at(1);
    CHECK(orange_side.reads == 52);
    CHECK(red_side.reads == 52);
    // Each keeps the layout it was shown in, not a blend of the two.
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(lightness_of(orange_side.lab.at(i)) ==
              Catch::Approx(lightness_of(orange_centred().at(i))).margin(3.0));
        CHECK(lightness_of(red_side.lab.at(i)) ==
              Catch::Approx(lightness_of(red_centred().at(i))).margin(3.0));
    }
}

TEST_CASE("a side held under noise, a thumb and a drifting lamp stays the one side",
          "[direct-scan]") {
    SideScan scan;
    Hand hand;
    for (int round = 0; round < 8; ++round) {
        hand.drift = 1.0F - (0.015F * static_cast<float>(round));
        hold_lab(scan, hand, orange_centred(), 16);
        hold_lab(scan, hand, red_centred(), 16);
    }
    CHECK(scan.sides().size() == 2);
}

TEST_CASE("a side keeps the quarter-turn it was shown in when its stickers are close in colour",
          "[direct-scan]") {
    // The turn was picked by a metric that barely sees red against orange,
    // so a red and orange side was laid down at whichever turn the noise
    // preferred and the fused side was a smear of all four.
    SideScan scan;
    Hand hand{.glare_and_thumbs = false};
    Lab9 shown = orange_centred();
    for (int turns = 0; turns < 4; ++turns) {
        hold_lab(scan, hand, shown, 12);
        hold_lab(scan, hand, red_centred(), 12);
        REQUIRE(scan.sides().size() == 2);
        // Reported the way it was last shown.
        const Lab9 side = scan.sides().at(0).lab;
        for (std::size_t i = 0; i < 9; ++i) {
            CHECK(lightness_of(side.at(i)) == Catch::Approx(lightness_of(shown.at(i))).margin(4.0));
        }
        shown = turned_once(shown);
    }
}

TEST_CASE("a scan says how far the light was from neutral, read from the cube's own white",
          "[direct-scan]") {
    SECTION("under a warm lamp the white measures pink") {
        const Cube cube = cube::test::scramble(21);
        SideScan scan;
        show_cube(scan, cube, 21, lamp_drawing());
        const auto whole = scan.scanned();
        REQUIRE(whole.has_value());
        CHECK(whole->cube == cube);
        // The white of the lamp palette is (65.5, 22.5, 16.5): chroma 27.9.
        CHECK(whole->cast == Catch::Approx(27.9).margin(3.0));
    }
    SECTION("in even light it measures neutral") {
        const Cube cube = cube::test::scramble(22);
        SideScan scan;
        show_cube(scan, cube, 22);
        const auto whole = scan.scanned();
        REQUIRE(whole.has_value());
        CHECK(whole->cube == cube);
        CHECK(whole->cast < 4.0F);
    }
}

TEST_CASE("two sides whose centres read as one colour are still two sides", "[direct-scan]") {
    // Orange lit to read as red: the centre of one side is the colour of the
    // centre of another, and only the rest says they are two. A hand covers
    // at most three stickers of a read the lattice found; these differ in
    // all eight.
    const cv::Vec3f green = cube::test::warm_lamp_lab(Color::Green);
    Lab9 other;
    other.fill(green);
    other.at(4) = lamp_orange();

    SideScan scan;
    Hand hand{.glare_and_thumbs = false};
    for (int round = 0; round < 3; ++round) {
        hold_lab(scan, hand, orange_centred(), 12);
        hold_lab(scan, hand, other, 12);
    }
    CHECK(scan.sides().size() == 2);
}

TEST_CASE("a flood of lattices that are no side does not cost the side being held",
          "[direct-scan]") {
    // A chessboard or a patterned shirt gives steady reads of colours no side
    // has. Each starts a group of its own; they are bounded, and a side that
    // has been held is not among those dropped.
    SideScan scan;
    Hand hand{.glare_and_thumbs = false};
    hold_lab(scan, hand, orange_centred(), 12);
    std::mt19937 rng = cube::test::deterministic_rng(11);
    std::uniform_real_distribution<float> lightness(5.0F, 95.0F);
    std::uniform_real_distribution<float> chroma(-60.0F, 60.0F);
    for (int stray = 0; stray < 80; ++stray) {
        Lab9 junk;
        for (cv::Vec3f& sticker : junk) {
            sticker = cv::Vec3f(lightness(rng), chroma(rng), chroma(rng));
        }
        scan.add(read_of(junk));
        scan.add(read_of(junk));
        scan.add(std::nullopt);
    }
    hold_lab(scan, hand, orange_centred(), 12);

    REQUIRE(scan.sides().size() == 1);
    CHECK(scan.sides().at(0).reads == 22);
}

TEST_CASE("a side that is wholly red and a side that is wholly orange are two sides",
          "[direct-scan]") {
    // The two closest colours of a cube, each filling a side - the shape of a
    // cube solved on two sides and muddled between. Judged by colour_distance
    // under 12 a solid red against a solid orange scores 9.5 a sticker, so
    // the two were one side.
    Lab9 red;
    red.fill(lamp_red());
    Lab9 orange;
    orange.fill(lamp_orange());

    SideScan scan;
    Hand hand{.glare_and_thumbs = false};
    for (int round = 0; round < 3; ++round) {
        hold_lab(scan, hand, red, 12);
        hold_lab(scan, hand, orange, 12);
    }
    REQUIRE(scan.sides().size() == 2);
    CHECK(lightness_of(scan.sides().at(0).lab.at(4)) ==
          Catch::Approx(lightness_of(lamp_red())).margin(3.0));
    CHECK(lightness_of(scan.sides().at(1).lab.at(4)) ==
          Catch::Approx(lightness_of(lamp_orange())).margin(3.0));
}

TEST_CASE("solid red and solid orange sides stay two sides as the lamp closes the gap",
          "[direct-scan]") {
    // The centre is the side's identity down to the least two centres can be
    // apart and still be two colours at all (kMinCentreSeedSeparationLab:
    // closer than that, resolve_colors calls them one colour and refuses the
    // scan). Orange is slid towards red along the line between them.
    for (const float gap : {16.4F, 15.0F, 14.0F, 13.0F}) {
        const float whole = static_cast<float>(cv::norm(lamp_orange() - lamp_red()));
        const cv::Vec3f closer = lamp_red() + ((lamp_orange() - lamp_red()) * (gap / whole));
        Lab9 red;
        red.fill(lamp_red());
        Lab9 orange;
        orange.fill(closer);

        SideScan scan;
        Hand hand{.glare_and_thumbs = false};
        for (int round = 0; round < 3; ++round) {
            hold_lab(scan, hand, red, 12);
            hold_lab(scan, hand, orange, 12);
        }
        INFO("centres " << gap << " Lab apart");
        CHECK(scan.sides().size() == 2);
    }
}

TEST_CASE("a cube with a whole red side and a whole orange side is read as six sides",
          "[direct-scan]") {
    // Turning only the right and left leaves those two sides whole and
    // stripes the four between them. Drawn through the reader in the colours
    // of the warm lamp. A whole side and four striped ones put together more
    // than one way, which is the scan's honest answer from flat views.
    const Cube cube = after("R2 L");
    REQUIRE(cube.face(Face::R) == Cube::solved().face(Face::R));
    REQUIRE(cube.face(Face::L) == Cube::solved().face(Face::L));
    REQUIRE_FALSE(cube.face(Face::F) == Cube::solved().face(Face::F));

    SideScan scan;
    show_cube(scan, cube, 5, lamp_drawing());
    expect_centres_of(scan, cube);
    const auto flat = scan.cube();
    REQUIRE_FALSE(flat.has_value());
    CHECK(flat.error() == cube::CubeError::ambiguous_scan);
}

TEST_CASE("a side with dark stickers beside bright ones is read at every quarter-turn",
          "[direct-scan]") {
    // The warm lamp's green and blue read nearly black (L 16.8 and 11) beside
    // whites and yellows at 55 to 65. The outlines the reader first takes are
    // against the mean of a block as wide as a sticker spacing, which a bright
    // neighbour drags up: the dark sticker passes only where the block has not
    // reached it, so it comes out small and off-centre, or, in a row between
    // bright stickers and a bright room, not at all. Of these 24 frames - the
    // six sides of seed 5's cube of the case above, each at all four
    // quarter-turns of the pose show_cube gives it - four were not read, with
    // dark stickers at the left or top: two had all nine as outlines whose
    // centres sat 4 pixels off, which made the spacing 32 for a true 40 and
    // snapped two of nine to the grid, and two had only six, the dark column
    // or row not an outline at all, so that they spanned two by three.
    const Cube cube = after("R2 L");
    std::mt19937 rng = cube::test::deterministic_rng(5);
    for (const Face face : kAllFaces) {
        const Placement placement = next_placement(rng);
        std::array<Color, 9> stickers = outside_view(cube, face);
        for (int turned = 0; turned < 4; ++turned) {
            INFO("side " << static_cast<int>(face) << " turned " << turned << " quarter-turns");
            const std::optional<SideRead> read =
                read_side(draw_side(stickers, placement.pose, lamp_drawing()));
            const auto& side = ::cube::test::must_have(read);
            CHECK(side.stickers_found >= 6);
            for (std::size_t i = 0; i < 9; ++i) {
                const std::size_t column = i % 3;
                const std::size_t row = i / 3;
                const cv::Point2f expected =
                    place(placement.pose, static_cast<float>(column) - 1.0F,
                          static_cast<float>(row) - 1.0F);
                // A clipped sticker's outline is a few pixels off its centre,
                // and the lattice is fitted through them: the patch sampled
                // must still land on its own sticker, which is a sixth of a
                // spacing's room. The colours say it did.
                CHECK(cv::norm(side.centres.at(i) - expected) <
                      0.15 * static_cast<double>(placement.pose.pitch));
                CHECK(cv::norm(side.lab.at(i) - cube::test::warm_lamp_lab(stickers.at(i))) < 4.0);
            }
            stickers = turned_clockwise(stickers);
        }
    }
}

TEST_CASE("a cube whose red and orange sides are each the other's colours is read as six sides",
          "[direct-scan]") {
    // Half-turns of the top, bottom and front leave only red and orange on
    // the left and right and make each exactly the other's colours.
    const Cube cube = after("F2 D2 U2");
    REQUIRE(cube.face(Face::R) != Cube::solved().face(Face::R));
    REQUIRE(cube.face(Face::R).at(4) != cube.face(Face::L).at(4));

    SideScan scan;
    show_cube(scan, cube, 3, lamp_drawing());

    expect_centres_of(scan, cube);
}

TEST_CASE("a stray capture gives way to a side that is shown for longer", "[direct-scan]") {
    const Cube cube = cube::test::scramble(31);
    SideScan scan;
    // Something that looked like a side for just long enough to be captured.
    const Pose square_on;
    show(scan, kMixed, square_on, static_cast<int>(SideScan::kMinReads) + 1);
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
    show_cube(scan, cube::test::scramble(11), 11);
    REQUIRE(scan.cube().has_value());

    scan.reset();
    CHECK(scan.sides().empty());
    CHECK(scan.cube().error() == CubeError::face_capture_incomplete);
}

TEST_CASE("a frame is prepared once, at the working width, for every reader", "[direct-scan]") {
    const Pose pose{.centre = {960.0F, 520.0F}, .pitch = 120.0F, .degrees = 6.0F};

    SECTION("a wide frame comes down to the working width and remembers the factor") {
        const auto prepared_read =
            prepare_frame(draw_side(kMixed, pose, Drawing{.size = {1920, 1080}}));
        const auto& prepared = ::cube::test::must_have(prepared_read);

        CHECK(prepared.working.cols == 640);
        CHECK(prepared.working.rows == 360);
        CHECK(std::abs(prepared.scale - 3.0F) < 1e-4F);
        // Nine stickers and whatever else the drawing has bright and bounded.
        CHECK(prepared.contours.size() >= 9);
    }
    SECTION("a frame no wider than that is used as it is") {
        const cv::Mat frame = draw_side(kMixed, Pose{.centre = {300.0F, 170.0F}, .pitch = 42.0F});
        const auto prepared_read = prepare_frame(frame);
        const auto& prepared = ::cube::test::must_have(prepared_read);

        CHECK(prepared.working.data == frame.data);
        CHECK(prepared.scale == 1.0F);
    }
    SECTION("nothing is made of a frame that is not 8-bit BGR") {
        CHECK_FALSE(prepare_frame(cv::Mat()).has_value());
        CHECK_FALSE(prepare_frame(cv::Mat(360, 640, CV_8UC1, cv::Scalar(0))).has_value());
    }
    SECTION("a read from the preparation is the read of the frame") {
        const cv::Mat frame = draw_side(kMixed, pose, Drawing{.size = {1920, 1080}});
        const auto prepared_read = prepare_frame(frame);
        const auto& prepared = ::cube::test::must_have(prepared_read);

        const std::optional<SideRead> direct_read = read_side(frame);
        const std::optional<SideRead> shared_read = read_side(prepared);
        const auto& direct = ::cube::test::must_have(direct_read);
        const auto& shared = ::cube::test::must_have(shared_read);
        CHECK(shared.pitch == direct.pitch);
        CHECK(shared.stickers_found == direct.stickers_found);
        CHECK(shared.found == direct.found);
        for (std::size_t i = 0; i < 9; ++i) {
            CHECK(shared.centres.at(i) == direct.centres.at(i));
            CHECK(shared.lab.at(i) == direct.lab.at(i));
        }
    }
}

// ---------------------------------------------------------------------------
// SideScan::diagnose: the evaluation scanned() makes, with its working kept
// (docs/diagnostics.md).
// ---------------------------------------------------------------------------

TEST_CASE("diagnose is the evaluation scanned makes, with the working kept", "[direct-scan]") {
    const Cube cube = cube::test::scramble(3);
    SideScan scan;
    show_cube(scan, cube, 3);

    const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
    const auto scanned = scan.scanned();
    REQUIRE(scanned.has_value());
    REQUIRE(diagnosis.result.has_value());
    CHECK(diagnosis.result->cube == scanned->cube);
    CHECK(diagnosis.result->cube == cube);

    // The six sides, each with the reads that made it.
    CHECK(diagnosis.captured >= 6);
    REQUIRE(diagnosis.sides.size() == 6);
    for (const auto& side : diagnosis.sides) {
        CHECK(side.reads >= SideScan::kMinReads);
        CHECK(side.roll >= 0);
        CHECK(side.roll < 4);
    }

    // How the colours were named, and how the cube was put together.
    const auto& naming = ::cube::test::must_have(diagnosis.naming);
    CHECK(naming.iterations >= 1);
    const auto& assembly = ::cube::test::must_have(diagnosis.assembly);
    CHECK(assembly.path == cube::vision::AssemblyPath::single_candidate);
    CHECK(assembly.candidates == 1);
    CHECK_FALSE(assembly.error.has_value());

    // The same colour naming the assembly was given.
    std::array<cube::vision::CapturedFace, 6> faces{};
    for (std::size_t i = 0; i < 6; ++i) {
        faces.at(i).lab = diagnosis.sides.at(i).lab;
    }
    const auto named = cube::vision::resolve_colors(faces);
    REQUIRE(named.has_value());
    for (std::size_t side = 0; side < 6; ++side) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            CHECK(naming.faces.at(side).at(cell).color == named->at(side).at(cell).color);
        }
    }

    // What SideScan::sides shows the person is these sides, turned to how
    // they were last held.
    const auto shown = scan.sides();
    REQUIRE(shown.size() == 6);
    // Same reads, same count, in the order they were first shown.
    for (const auto& shown_side : shown) {
        std::size_t reads_here = 0;
        for (const auto& side : diagnosis.sides) {
            if (side.reads == shown_side.reads) {
                ++reads_here;
            }
        }
        CHECK(reads_here >= 1);
    }
}

TEST_CASE("diagnose lists the sides captured so far when there are fewer than six",
          "[direct-scan]") {
    const Cube cube = cube::test::scramble(4);
    SideScan scan;
    show(scan, outside_view(cube, Face::U), Pose{.centre = {320.0F, 180.0F}});
    scan.add(std::nullopt);
    show(scan, outside_view(cube, Face::F), Pose{.centre = {320.0F, 180.0F}});
    scan.add(std::nullopt);

    const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
    CHECK(diagnosis.captured == 2);
    CHECK(diagnosis.sides.size() == 2);
    CHECK_FALSE(diagnosis.naming.has_value());
    CHECK_FALSE(diagnosis.assembly.has_value());
    REQUIRE_FALSE(diagnosis.result.has_value());
    CHECK(diagnosis.result.error() == CubeError::face_capture_incomplete);
}

TEST_CASE("diagnose names the pair of colours that were exchanged to make the cube",
          "[direct-scan]") {
    // Under the plain tungsten lamp the cube's own white now says what the
    // light did, the colours are named right, and nothing is exchanged (the
    // test after this one). The exchange stays as the backstop for a scene
    // in which the white cannot be told, which this lamp with a pale green
    // is: nothing is taken out of the light and red and orange come out
    // traded, as they always did.
    const Drawing lamp_lit = tungsten_lamp(true);
    for (const unsigned seed : {21U, 22U, 23U}) {
        const Cube cube = cube::test::scramble(seed);
        SideScan scan;
        show_cube(scan, cube, seed, lamp_lit);

        const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
        REQUIRE(diagnosis.result.has_value());
        CHECK(diagnosis.result->cube == cube);

        // Naming by colour made no cube; trading two names did.
        const cube::vision::AssemblyTrace& trace = ::cube::test::must_have(diagnosis.assembly);
        CHECK(trace.path == cube::vision::AssemblyPath::names_exchanged);
        CHECK(trace.candidates == 0);
        CHECK(::cube::test::must_have(trace.direct_error) == CubeError::invalid_cube_state);
        const auto& exchange = ::cube::test::must_have(trace.exchange);

        // The two colours that look alike by tungsten light: red and orange.
        const cube::model::FacetColor first = exchange.first_name;
        const cube::model::FacetColor second = exchange.second_name;
        const bool red_and_orange =
            (first == cube::model::FacetColor::red && second == cube::model::FacetColor::orange) ||
            (first == cube::model::FacetColor::orange && second == cube::model::FacetColor::red);
        CHECK(red_and_orange);
        CHECK(exchange.first_side != exchange.second_side);
        CHECK(exchange.first_side < 6);
        CHECK(exchange.second_side < 6);
        CHECK(exchange.centre_distance >= 0.0F);
    }
}

TEST_CASE("diagnose shows the light taken out and a cube that needed no exchange",
          "[direct-scan]") {
    const Drawing lamp_lit = tungsten_lamp();
    for (const unsigned seed : {21U, 22U, 23U}) {
        const Cube cube = cube::test::scramble(seed);
        SideScan scan;
        show_cube(scan, cube, seed, lamp_lit);

        const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
        REQUIRE(diagnosis.result.has_value());
        CHECK(diagnosis.result->cube == cube);

        // The white is the tungsten lamp's, chroma 17.6, and was taken out.
        const cube::vision::WhiteReference& white = cube::test::must_have(diagnosis.white);
        CHECK(white.cast == Catch::Approx(17.6).margin(2.0));
        CHECK(diagnosis.result->cast == white.cast);
        CHECK(white.gain.at(0) < 1.0F);  // red is taken down
        CHECK(white.gain.at(2) > 1.0F);  // blue is brought up

        // Named right at the first attempt: no exchange, no failed assembly.
        const cube::vision::AssemblyTrace& assembly = cube::test::must_have(diagnosis.assembly);
        CHECK(assembly.path == cube::vision::AssemblyPath::single_candidate);
        CHECK_FALSE(assembly.exchange.has_value());
    }
}

TEST_CASE("diagnose reports the candidates when the sides make the cube two ways",
          "[direct-scan]") {
    // The scramble of "sides that put together two ways are reported".
    const Cube cube = after("R F' D' L L B B' D' R F2 L' U' F2 B B2 U B2 R D' U2 R2 L' L2 F L2");
    REQUIRE_FALSE(cube::test::sides_make_one_cube(cube));

    SideScan scan;
    show_cube(scan, cube, 14);

    const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
    REQUIRE_FALSE(diagnosis.result.has_value());
    CHECK(diagnosis.result.error() == CubeError::ambiguous_scan);
    const auto& assembly = ::cube::test::must_have(diagnosis.assembly);
    CHECK(assembly.path == cube::vision::AssemblyPath::ambiguous);
    CHECK(assembly.candidates >= 2);
    CHECK(assembly.corner_fits.size() == assembly.candidates);
    CHECK_FALSE(assembly.chosen.has_value());
    CHECK(::cube::test::must_have(assembly.error) == CubeError::ambiguous_scan);
}

TEST_CASE("a scan that does not make a cube is reported with the cause", "[direct-scan]") {
    // A fixed scramble, so that the sides stay distinct from one another
    // once the colours are traded on every platform.
    const Cube cube = after("R2 D' B2 U2 L2 F2 D' R2 U R2 B' F L' D2 R F' L B' D U2");
    REQUIRE(cube::test::sides_make_one_cube(cube));
    SideScan scan;
    show_cube(scan, cube, 9);
    REQUIRE(scan.diagnose().result.has_value());

    // The same cube lit so that orange reads as red: two of the six centres
    // are one colour, which no cube has, and the naming refuses it.
    SideScan broken;
    const Drawing both_red{
        .paint =
            [](Color colour) {
                return colour == Color::Orange ? cube::test::paint(Color::Red)
                                               : cube::test::paint(colour);
            },
    };
    show_cube(broken, cube, 9, both_red);
    const cube::vision::ScanDiagnosis diagnosis = broken.diagnose();
    REQUIRE_FALSE(diagnosis.result.has_value());
    CHECK(diagnosis.result.error() == CubeError::no_valid_cube_detected);
    CHECK_FALSE(diagnosis.naming.has_value());
    CHECK_FALSE(diagnosis.assembly.has_value());
    CHECK(diagnosis.captured >= 6);
}
