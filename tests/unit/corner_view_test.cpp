#include <catch2/catch_test_macros.hpp>

#include "drawn_corner.hpp"
#include "drawn_cube.hpp"
#include "must_have.hpp"

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/prepared_frame.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::test::draw_corner;
using cube::test::draw_corner3;
using cube::test::draw_side;
using cube::test::Pose;
using cube::test::room;
using cube::vision::CornerRead;
using cube::vision::fit_corner;
using cube::vision::GridEdge;
using cube::vision::prepare_frame;
using cube::vision::read_corner;
using cube::vision::SessionColours;
using cube::vision::SideScan;

namespace {

// The ambiguous cube of tests/unit/direct_scan_test.cpp: six sides read
// perfectly still put together two ways.
constexpr std::string_view kAmbiguous =
    "R F' D' L L B B' D' R F2 L' U' F2 B B2 U B2 R D' U2 R2 L' L2 F L2";

Cube after(std::string_view algorithm) {
    Cube cube = Cube::solved();
    for (const cube::solver::Move move : *cube::solver::parse_algorithm(algorithm)) {
        cube = cube.apply(move);
    }
    return cube;
}

// What each colour measures in the drawings: the Lab of the paint.
SessionColours drawn_colours() {
    SessionColours colours{};
    for (const Color colour :
         {Color::White, Color::Yellow, Color::Green, Color::Blue, Color::Orange, Color::Red}) {
        const cv::Scalar bgr = cube::test::paint(colour);
        const cv::Mat3f source(
            1, 1,
            cv::Vec3f(static_cast<float>(bgr(0)) / 255.0F, static_cast<float>(bgr(1)) / 255.0F,
                      static_cast<float>(bgr(2)) / 255.0F));
        cv::Mat3f lab;
        cv::cvtColor(source, lab, cv::COLOR_BGR2Lab);
        colours.at(static_cast<std::size_t>(colour)) = lab(0, 0);
    }
    return colours;
}

// The cells along an edge of a 3x3, in the order the edge is walked.
std::array<std::size_t, 3> edge_cells(GridEdge edge) {
    switch (edge) {
        case GridEdge::Top:
            return {0, 1, 2};
        case GridEdge::Right:
            return {2, 5, 8};
        case GridEdge::Bottom:
            return {8, 7, 6};
        case GridEdge::Left:
            return {6, 3, 0};
    }
    return {};
}

cv::Mat corner_frame(const Cube& cube, Face left) {
    return cube::test::draw_cube_corner(cube, left);
}

void show_sides(SideScan& scan, const Cube& cube) {
    constexpr int kFrames = static_cast<int>(SideScan::kMinReads) + 4;
    for (const Face face : kAllFaces) {
        const cv::Mat frame = draw_side(cube::solver::outside_view(cube, face), Pose{});
        for (int i = 0; i < kFrames; ++i) {
            scan.add(cube::vision::read_side(frame));
        }
        scan.add(cube::vision::read_side(room({640, 360})));
    }
}

}  // namespace

TEST_CASE("two sides turned corner-on are two lattices that share an edge", "[corner]") {
    const Cube cube = cube::test::scramble(2);
    const std::optional<CornerRead> read = read_corner(corner_frame(cube, Face::F));
    const CornerRead& corner = ::cube::test::must_have(read);

    REQUIRE(corner.sides.size() == 2);
    REQUIRE(corner.edges.size() == 1);

    // Whichever way round each lattice was indexed, the stickers along the
    // shared edge sit against one another: first with last.
    const auto& edge = corner.edges.front();
    const auto& first = corner.sides.at(edge.first);
    const auto& second = corner.sides.at(edge.second);
    const auto cells_first = edge_cells(edge.on_first);
    const auto cells_second = edge_cells(edge.on_second);
    for (std::size_t t = 0; t < 3; ++t) {
        const cv::Point2f gap =
            first.centres.at(cells_first.at(t)) - second.centres.at(cells_second.at(2 - t));
        CHECK(std::hypot(gap.x, gap.y) < 1.2F * first.pitch);
    }
    for (const auto& side : corner.sides) {
        CHECK(side.pitch > 15.0F);
        for (const bool found : side.found) {
            CHECK(found);
        }
    }
}

TEST_CASE("a frame no side is square in goes from one preparation to both readers", "[corner]") {
    // What the app does with a frame: prepare it once, ask for a side, and only
    // when there is none ask for the corner - the same reads as asking each
    // reader of the frame itself.
    const Cube cube = cube::test::scramble(2);
    const cv::Mat frame = corner_frame(cube, Face::F);
    const auto prepared_read = prepare_frame(frame);
    const auto& prepared = ::cube::test::must_have(prepared_read);

    CHECK_FALSE(cube::vision::read_side(prepared).has_value());
    CHECK_FALSE(cube::vision::read_side(frame).has_value());

    const auto shared_read = read_corner(prepared);
    const auto direct_read = read_corner(frame);
    const auto& shared = ::cube::test::must_have(shared_read);
    const auto& direct = ::cube::test::must_have(direct_read);
    REQUIRE(shared.sides.size() == direct.sides.size());
    REQUIRE(shared.edges.size() == direct.edges.size());
    for (std::size_t i = 0; i < shared.sides.size(); ++i) {
        CHECK(shared.sides.at(i).pitch == direct.sides.at(i).pitch);
        CHECK(shared.sides.at(i).found == direct.sides.at(i).found);
        for (std::size_t cell = 0; cell < 9; ++cell) {
            CHECK(shared.sides.at(i).centres.at(cell) == direct.sides.at(i).centres.at(cell));
            CHECK(shared.sides.at(i).lab.at(cell) == direct.sides.at(i).lab.at(cell));
        }
    }
}

TEST_CASE("a side seen square-on, or an empty room, is not a corner", "[corner]") {
    CHECK_FALSE(read_corner(room({640, 360})).has_value());
    CHECK_FALSE(
        read_corner(draw_side(cube::solver::outside_view(cube::test::scramble(2), Face::F), Pose{}))
            .has_value());
}

TEST_CASE("a corner view fits the cube it was drawn from and not a cube one turn away",
          "[corner]") {
    const Cube cube = cube::test::scramble(2);
    const SessionColours colours = drawn_colours();

    for (const Face left : kAllFaces) {
        INFO("left side " << static_cast<int>(left));
        const auto read = read_corner(corner_frame(cube, left));
        CHECK(fit_corner(::cube::test::must_have(read), cube, colours).fits);
    }

    // A turn of the front, right or back layer changes stickers in this view.
    const auto front_read = read_corner(corner_frame(cube, Face::F));
    const auto& front = ::cube::test::must_have(front_read);
    for (const Face turned : {Face::F, Face::R, Face::U, Face::D}) {
        INFO("turned " << static_cast<int>(turned));
        CHECK_FALSE(fit_corner(front, cube.apply({turned, 1}), colours).fits);
    }
}

TEST_CASE("a corner view with one side turned is not the cube", "[corner]") {
    // Both sides are the cube's own, but the right one is a quarter-turn out
    // from where the edge says it goes: its stickers do not meet the left
    // side's across the shared edge.
    const Cube cube = cube::test::scramble(2);
    const SessionColours colours = drawn_colours();
    // F, and the side to its right turned a quarter out of place.
    std::array<Color, 9> right = cube::solver::outside_view(cube, cube::solver::right_of(Face::F));
    right = cube::test::turned_clockwise(right);
    const auto read = read_corner(draw_corner(cube::solver::outside_view(cube, Face::F), right));
    CHECK_FALSE(fit_corner(::cube::test::must_have(read), cube, colours).fits);
}

TEST_CASE("sides that put together two ways are settled by the corners seen", "[corner]") {
    const Cube cube = after(kAmbiguous);
    REQUIRE_FALSE(cube::test::sides_make_one_cube(cube));

    SideScan scan;
    show_sides(scan, cube);
    REQUIRE(scan.sides().size() == 6);
    const auto before = scan.cube();
    REQUIRE_FALSE(before.has_value());
    CHECK(before.error() == cube::CubeError::ambiguous_scan);

    // A view that touches nothing the candidates disagree on leaves it open;
    // a corner read of nothing changes nothing.
    scan.add_corner(std::nullopt);
    CHECK(scan.corner_reads() == 0);
    CHECK(scan.cube().error() == cube::CubeError::ambiguous_scan);

    for (const Face left : kAllFaces) {
        scan.add_corner(read_corner(corner_frame(cube, left)));
    }
    CHECK(scan.corner_reads() == 6);

    const auto settled = scan.cube();
    REQUIRE(settled.has_value());
    CHECK(*settled == cube);

    scan.reset();
    CHECK(scan.corner_reads() == 0);
}

TEST_CASE("a corner view of the wrong cube does not settle it", "[corner]") {
    // The same six sides, but corner views drawn from the cube's other
    // reading: each side is its own, and none of the edges agree with the
    // candidates the sides make.
    const Cube cube = after(kAmbiguous);
    const Cube elsewhere = cube::test::scramble(5);

    SideScan scan;
    show_sides(scan, cube);
    for (const Face left : kAllFaces) {
        scan.add_corner(read_corner(corner_frame(elsewhere, left)));
    }
    const auto read = scan.cube();
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error() == cube::CubeError::ambiguous_scan);
}

namespace {

// The three sides of `cube` that meet at one corner, as arrays drawn
// outside-in: the faces given, in the order given.
std::array<std::array<Color, 9>, 3> three_sides(const Cube& cube, Face a, Face b, Face c) {
    return {cube::test::outside_view(cube, a), cube::test::outside_view(cube, b),
            cube::test::outside_view(cube, c)};
}

// Hold a frame in front of the camera as the app does: the frame is asked for
// a side square to the camera first, and for a corner only when there is none.
void hold(SideScan& scan, const cv::Mat& frame, int frames = 12) {
    const auto prepared = prepare_frame(frame);
    REQUIRE(prepared.has_value());
    for (int i = 0; i < frames; ++i) {
        const auto side = cube::vision::read_side(*prepared);
        scan.add(side);
        if (!side.has_value()) {
            scan.add_corner(read_corner(*prepared));
        }
    }
}

}  // namespace

TEST_CASE("three sides turned corner-on are three lattices, each against the next", "[corner]") {
    const Cube cube = cube::test::scramble(3);
    const cv::Mat frame =
        cube::test::draw_corner3(three_sides(cube, Face::U, Face::R, Face::F), {});
    const std::optional<CornerRead> read = read_corner(frame);
    const CornerRead& corner = ::cube::test::must_have(read);

    REQUIRE(corner.sides.size() == 3);
    CHECK(corner.edges.size() == 3);
    for (const auto& side : corner.sides) {
        CHECK(side.pitch > 15.0F);
        for (const bool found : side.found) {
            CHECK(found);
        }
    }
}

TEST_CASE("a cube held on a corner is read three sides at a time", "[corner]") {
    const Cube cube = cube::test::scramble(3);
    const auto sides = three_sides(cube, Face::U, Face::R, Face::F);

    SideScan scan;
    // One frame in, nothing has held still yet.
    hold(scan, cube::test::draw_corner3(sides), 1);
    CHECK(scan.sides().empty());
    CHECK_FALSE(scan.reading());

    // Held for as long as one side needs, all three are captured.
    hold(scan, cube::test::draw_corner3(sides), static_cast<int>(SideScan::kMinReads) + 1);
    CHECK(scan.reading());
    REQUIRE(scan.sides().size() == 3);
    for (const SideScan::Side& side : scan.sides()) {
        CHECK(side.reads >= SideScan::kMinReads);
        CHECK(side.in_view);
    }

    // Each side is what was drawn: its nine colours are one of the three
    // drawn sides, whichever way up the lattice happened to lay it.
    const SessionColours colours = drawn_colours();
    const auto nearest = [&](const cv::Vec3f& lab) {
        std::size_t best = 0;
        float best_distance = 1.0e9F;
        for (std::size_t i = 0; i < colours.size(); ++i) {
            const float d = cube::vision::colour_distance(lab, colours.at(i));
            if (d < best_distance) {
                best_distance = d;
                best = i;
            }
        }
        return best;
    };
    std::size_t matched = 0;
    for (const SideScan::Side& side : scan.sides()) {
        for (const auto& drawn : sides) {
            std::array<Color, 9> turned = drawn;
            for (int turn = 0; turn < 4; ++turn) {
                bool same = true;
                for (std::size_t i = 0; i < 9; ++i) {
                    same =
                        same && nearest(side.lab.at(i)) == static_cast<std::size_t>(turned.at(i));
                }
                if (same) {
                    ++matched;
                }
                turned = cube::test::turned_clockwise(turned);
            }
        }
    }
    CHECK(matched >= 3);
}

TEST_CASE("two corner views are a whole cube", "[corner]") {
    // The corner a cube is first held on and the one across from it show every
    // side between them.
    for (const unsigned seed : {3U, 4U, 5U}) {
        const Cube cube = cube::test::scramble(seed);
        SideScan scan;
        hold(scan, cube::test::draw_corner3(three_sides(cube, Face::U, Face::R, Face::F)), 12);
        scan.add(std::nullopt);
        scan.add_corner(std::nullopt);
        CHECK(scan.sides().size() == 3);
        hold(scan, cube::test::draw_corner3(three_sides(cube, Face::D, Face::L, Face::B)), 12);

        REQUIRE(scan.sides().size() == 6);
        const auto read = scan.cube();
        REQUIRE(read.has_value());
        CHECK(*read == cube);
    }
}

TEST_CASE("a side read square-on and then from a corner is one side", "[corner]") {
    const Cube cube = cube::test::scramble(3);
    const auto sides = three_sides(cube, Face::U, Face::R, Face::F);

    SideScan scan;
    // The up side, square to the camera, first.
    const cv::Mat flat = draw_side(sides.at(0), Pose{});
    for (int i = 0; i < 12; ++i) {
        scan.add(cube::vision::read_side(flat));
    }
    REQUIRE(scan.sides().size() == 1);
    const std::size_t flat_reads = scan.sides().front().reads;
    scan.add(std::nullopt);

    hold(scan, cube::test::draw_corner3(sides), 12);
    // The up side again is the same side, and the other two are new.
    REQUIRE(scan.sides().size() == 3);
    std::size_t most = 0;
    for (const SideScan::Side& side : scan.sides()) {
        most = std::max(most, side.reads);
    }
    CHECK(most > flat_reads);
}

TEST_CASE("a corner that is moving is not counted", "[corner]") {
    const Cube cube = cube::test::scramble(3);
    const auto sides = three_sides(cube, Face::U, Face::R, Face::F);

    SideScan scan;
    // A cube swung about by a pitch (40 px) from frame to frame: the same
    // three sides in view every time, never in the same place twice.
    for (int i = 0; i < 24; ++i) {
        const float swing = (i % 2 == 0) ? -30.0F : 30.0F;
        const cube::test::Corner3Pose pose{.vertex = {320.0F + swing, 190.0F}};
        hold(scan, cube::test::draw_corner3(sides, pose), 1);
    }
    CHECK(scan.sides().empty());
    CHECK_FALSE(scan.reading());
}

namespace {

// A side of a corner read, built rather than drawn: `columns` and `rows` are
// the steps between neighbouring stickers in pixels, `found` how many of the
// nine had a blob, and every sticker the one colour given.
cube::vision::ObliqueSide made_side(cv::Point2f origin, cv::Point2f columns, cv::Point2f rows,
                                    std::size_t found, const cv::Vec3f& colour) {
    cube::vision::ObliqueSide side;
    for (std::size_t cell = 0; cell < 9; ++cell) {
        side.centres.at(cell) =
            origin + static_cast<float>(cell % 3) * columns + static_cast<float>(cell / 3) * rows;
        side.lab.at(cell) = colour;
        side.found.at(cell) = cell < found;
    }
    side.pitch = 0.5F * static_cast<float>(cv::norm(columns) + cv::norm(rows));
    return side;
}

// A corner of two sides held still for `frames` frames, the way the app feeds
// it.
SideScan hold_made(const cube::vision::ObliqueSide& first, const cube::vision::ObliqueSide& second,
                   int frames) {
    CornerRead read;
    read.sides = {first, second};
    SideScan scan;
    for (int i = 0; i < frames; ++i) {
        scan.add(std::nullopt);
        scan.add_corner(read);
    }
    return scan;
}

const cv::Vec3f kWhiteish{90.0F, 0.0F, 0.0F};
const cv::Vec3f kRedish{45.0F, 60.0F, 40.0F};

}  // namespace

TEST_CASE("a side of a corner with too few stickers found is not counted", "[corner]") {
    const auto whole = made_side({100, 100}, {40, 0}, {0, 40}, 9, kWhiteish);
    // Six of nine, which the corner reader will lay a lattice through: the
    // other three are where the lattice puts them, and may be a finger.
    const auto six = made_side({300, 100}, {40, 0}, {0, 40}, 6, kRedish);
    const auto seven = made_side({300, 100}, {40, 0}, {0, 40}, 7, kRedish);

    const SideScan with_six = hold_made(whole, six, 12);
    REQUIRE(with_six.sides().size() == 1);
    CHECK(with_six.sides().front().reads == 11);

    const SideScan with_seven = hold_made(whole, seven, 12);
    CHECK(with_seven.sides().size() == 2);
}

TEST_CASE("a side of a corner that is only a strip is not counted", "[corner]") {
    const auto whole = made_side({100, 100}, {40, 0}, {0, 40}, 9, kWhiteish);
    // A column step under half the row step: a side seen nearly edge-on, whose
    // patches are mostly the neighbouring sticker.
    const auto strip = made_side({300, 100}, {14, 0}, {0, 40}, 9, kRedish);
    const auto slanted = made_side({300, 100}, {20, 0}, {0, 40}, 9, kRedish);

    const SideScan with_strip = hold_made(whole, strip, 12);
    CHECK(with_strip.sides().size() == 1);

    const SideScan with_slanted = hold_made(whole, slanted, 12);
    CHECK(with_slanted.sides().size() == 2);
}

TEST_CASE("the side with the fewest reads is the one a corner is waiting for", "[corner]") {
    const auto first = made_side({100, 100}, {40, 0}, {0, 40}, 9, kWhiteish);
    const auto second = made_side({300, 100}, {40, 0}, {0, 40}, 9, kRedish);

    SideScan scan;
    CornerRead one_side_late;
    one_side_late.sides = {first};
    CornerRead both;
    both.sides = {first, second};
    // The first side on its own for five frames, then both.
    for (int i = 0; i < 5; ++i) {
        scan.add(std::nullopt);
        scan.add_corner(one_side_late);
    }
    for (int i = 0; i < 3; ++i) {
        scan.add(std::nullopt);
        scan.add_corner(both);
    }
    CHECK(scan.reading());
    // First has 4 + 3 reads; second 2 (it was steady on the 2nd and 3rd).
    CHECK(scan.reads_in_view() == 2);
}

TEST_CASE("a corner side nearly as close to two sides is not counted for either", "[corner]") {
    // Two sides 17 apart in colour, and a read between them: 8.8 from one,
    // 7.9 from the other. Within the same-side distance of both, and nothing
    // in it says which it is.
    const cv::Vec3f first{60.0F, 20.0F, 0.0F};
    const cv::Vec3f second{60.0F, 20.0F, 18.0F};
    const cv::Vec3f between{60.0F, 20.0F, 9.0F};
    const cv::Vec3f near_first{60.0F, 20.0F, 2.0F};

    SideScan scan;
    const auto feed = [&scan](const std::vector<cube::vision::ObliqueSide>& sides, int frames) {
        CornerRead read;
        read.sides = sides;
        for (int i = 0; i < frames; ++i) {
            scan.add(std::nullopt);
            scan.add_corner(read);
        }
    };
    const auto reads_of = [&scan](const cv::Vec3f& colour) {
        for (const SideScan::Side& side : scan.sides()) {
            if (cube::vision::colour_distance(side.lab.at(4), colour) < 1.0F) {
                return side.reads;
            }
        }
        return std::size_t{0};
    };
    const auto left = [](const cv::Vec3f& colour) {
        return made_side({100, 100}, {40, 0}, {0, 40}, 9, colour);
    };
    const auto right = [](const cv::Vec3f& colour) {
        return made_side({300, 100}, {40, 0}, {0, 40}, 9, colour);
    };

    feed({left(first), right(second)}, 12);
    REQUIRE(scan.sides().size() == 2);
    const std::size_t first_before = reads_of(first);
    const std::size_t second_before = reads_of(second);
    REQUIRE(first_before > 0);
    REQUIRE(second_before > 0);

    // The left side alone, in the colour between the two: counted for neither,
    // and no side is made of it.
    feed({left(between)}, 6);
    CHECK(scan.sides().size() == 2);
    CHECK(reads_of(first) == first_before);
    CHECK(reads_of(second) == second_before);
    CHECK_FALSE(scan.reading());

    // A read plainly nearer one of them counts, for that one.
    feed({left(near_first)}, 3);
    CHECK(reads_of(first) > first_before);
    CHECK(reads_of(second) == second_before);
}

TEST_CASE("a side only a corner has shown does not take the place of one read square-on",
          "[corner]") {
    // Six sides read square-on, eight reads each, in six distinct colours.
    const std::array<cv::Vec3f, 6> flat = {
        cv::Vec3f{90.0F, 0.0F, 0.0F},    cv::Vec3f{50.0F, 60.0F, 40.0F},
        cv::Vec3f{60.0F, -40.0F, 50.0F}, cv::Vec3f{30.0F, 20.0F, -50.0F},
        cv::Vec3f{80.0F, -5.0F, 70.0F},  cv::Vec3f{45.0F, 50.0F, 10.0F}};
    const cv::Vec3f stray{70.0F, 30.0F, -30.0F};

    SideScan scan;
    for (const cv::Vec3f& colour : flat) {
        cube::vision::SideRead read;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            read.centres.at(cell) = cv::Point2f(200.0F + 40.0F * static_cast<float>(cell % 3),
                                                100.0F + 40.0F * static_cast<float>(cell / 3));
            read.lab.at(cell) = colour;
            read.found.at(cell) = true;
        }
        read.pitch = 40.0F;
        read.stickers_found = 9;
        for (int i = 0; i < static_cast<int>(SideScan::kMinReads) + 1; ++i) {
            scan.add(read);
        }
        scan.add(std::nullopt);
    }
    REQUIRE(scan.sides().size() == 6);

    // A seventh colour held on a corner for far longer than any of them.
    const auto left = made_side({100, 100}, {40, 0}, {0, 40}, 9, stray);
    const auto right = made_side({300, 100}, {40, 0}, {0, 40}, 9, stray);
    CornerRead read;
    read.sides = {left, right};
    for (int i = 0; i < 40; ++i) {
        scan.add(std::nullopt);
        scan.add_corner(read);
    }

    // The six it shows are still the six read square-on.
    const auto shown = scan.sides();
    REQUIRE(shown.size() == 6);
    for (const SideScan::Side& side : shown) {
        CHECK(cube::vision::colour_distance(side.lab.at(4), stray) > 5.0F);
    }
}

TEST_CASE("a corner lit differently from the scan still fits its cube", "[corner]") {
    // By lamplight a side seen at an angle sits further from the colours the
    // session measured than the one square-on reads. Here the whole view is
    // warmer than the scan's colours, which puts every side 9.7 from them: out
    // of reach at the old fitting distance of 8, and still plainly the
    // cube's at 12, where a cube a turn away is as far off as ever.
    const Cube cube = cube::test::scramble(2);
    const SessionColours colours = drawn_colours();
    cube::test::Drawing warmer;
    warmer.paint = [](Color colour) {
        const cv::Scalar plain = cube::test::paint(colour);
        return cv::Scalar(plain(0) * 0.7, plain(1) * 0.92, plain(2));
    };
    const auto left = cube::solver::outside_view(cube, Face::F);
    const auto right = cube::solver::outside_view(cube, cube::solver::right_of(Face::F));
    const auto read_value = read_corner(draw_corner(left, right, cube::test::CornerPose{}, warmer));
    const auto& read = ::cube::test::must_have(read_value);

    const cube::vision::CornerFit fit = fit_corner(read, cube, colours);
    CHECK(fit.distance > 8.0F);
    CHECK(fit.distance < cube::vision::kCornerFitDistance);
    CHECK(fit.fits);
    CHECK_FALSE(fit_corner(read, cube, colours, 8.0F).fits);

    for (const Face turned : {Face::F, Face::R, Face::U, Face::D}) {
        INFO("turned " << static_cast<int>(turned));
        CHECK_FALSE(fit_corner(read, cube.apply({turned, 1}), colours).fits);
    }
}

namespace {

// The face of `cube` whose centre is each colour of `corner`.
std::array<Face, 3> faces_of(const Cube& cube, const std::array<Color, 3>& corner) {
    std::array<Face, 3> faces{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (const Face face : kAllFaces) {
            if (cube.face(face).at(4) == corner.at(i)) {
                faces.at(i) = face;
            }
        }
    }
    return faces;
}

Cube patterned_cube() {
    const auto cube = cube::test::patterned_cube();
    REQUIRE(cube.has_value());
    return *cube;
}

}  // namespace

TEST_CASE("a corner drawn with all three sides fits its cube and not one a turn away", "[corner]") {
    const Cube cube = cube::test::scramble(2);
    const SessionColours colours = drawn_colours();
    for (const Face vertical : {Face::U, Face::D}) {
        for (const Face depth : {Face::F, Face::B}) {
            for (const Face side : {Face::L, Face::R}) {
                INFO("corner " << static_cast<int>(vertical) << " " << static_cast<int>(depth)
                               << " " << static_cast<int>(side));
                const cv::Mat frame = cube::test::draw_cube_corner3(cube, vertical, depth, side);
                REQUIRE_FALSE(frame.empty());
                const auto read_value = read_corner(frame);
                const auto& read = ::cube::test::must_have(read_value);
                REQUIRE(read.sides.size() == 3);
                REQUIRE(read.edges.size() == 3);
                CHECK(fit_corner(read, cube, colours).fits);
                for (const Face turned : kAllFaces) {
                    CHECK_FALSE(fit_corner(read, cube.apply({turned, 1}), colours).fits);
                }
            }
        }
    }
}

TEST_CASE("a cube the sides settle has no corner to ask for", "[corner]") {
    const Cube cube = cube::test::scramble(3);
    SideScan scan;
    show_sides(scan, cube);
    REQUIRE(scan.cube().has_value());
    const auto diagnosis = scan.diagnose();
    REQUIRE(diagnosis.assembly.has_value());
    CHECK_FALSE(diagnosis.assembly->settling_corner.has_value());
}

TEST_CASE("an ambiguous cube says which corner to show, and showing it settles it", "[corner]") {
    const Cube cube = patterned_cube();
    SideScan scan;
    show_sides(scan, cube);
    REQUIRE(scan.sides().size() == 6);
    const auto before = scan.cube();
    REQUIRE_FALSE(before.has_value());
    REQUIRE(before.error() == cube::CubeError::ambiguous_scan);

    // Follow what it asks for, as a person would, until it is settled. A view
    // of the corner it names tells the cubes it was choosing between apart; a
    // couple of those may be left, and then it asks for the next.
    int asked = 0;
    for (; asked < 4 && !scan.cube().has_value(); ++asked) {
        const auto diagnosis = scan.diagnose();
        REQUIRE(diagnosis.assembly.has_value());
        const auto& corner = ::cube::test::must_have(diagnosis.assembly->settling_corner);
        const std::array<Face, 3> faces = faces_of(cube, corner);
        const cv::Mat frame =
            cube::test::draw_cube_corner3(cube, faces.at(0), faces.at(1), faces.at(2));
        REQUIRE_FALSE(frame.empty());
        hold(scan, frame, 6);
    }
    const auto settled = scan.cube();
    REQUIRE(settled.has_value());
    CHECK(*settled == cube);
    CHECK(asked <= 3);
}
