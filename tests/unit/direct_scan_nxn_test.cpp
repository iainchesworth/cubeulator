#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "deterministic_rng.hpp"
#include "drawn_cube.hpp"

#include <cube/error.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/prepared_frame.hpp>
#include <cube/vision/read_set.hpp>
#include <cube/vision/side_grid.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

// The direct-scan reader on cubes of N x N, N from 2 to 9, drawn
// (drawn_cube.hpp) because no recording of one exists: what the reader does
// with the geometry of a bigger side, and the scan built on it. The sizes of
// gate S0 of docs/nxn-app-plan.md, at the sticker sizes it measures them at
// (tools/scan/s0.cpp has the full sweep).

namespace {

using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;
using cube::test::Drawing;
using cube::test::Pose;
using cube::vision::SideRead;
using cube::vision::SideScan;

constexpr std::array<Color, 6> kColours = {
    Color::White, Color::Yellow, Color::Green, Color::Blue, Color::Orange, Color::Red,
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

Color nearest_colour(const cv::Vec3f& lab, const Drawing& drawing) {
    Color best = Color::White;
    double best_distance = std::numeric_limits<double>::max();
    for (const Color colour : kColours) {
        const double distance = cv::norm(lab - lab_of(drawing.paint(colour)));
        if (distance < best_distance) {
            best_distance = distance;
            best = colour;
        }
    }
    return best;
}

// A cube drawn at `pitch` pixels a sticker, which for a 640 x 360 frame and a
// cube spanning half its height is 360 / 2 / N.
Pose pose_for(int size, float span_of_height) {
    Pose pose;
    pose.pitch = 360.0F * span_of_height / static_cast<float>(size);
    return pose;
}

// Whether `made` is `truth` or the same cube held another way up: the scan puts
// a side where its centre colour says it goes, so a cube scrambled with middle
// layers comes back in the orientation its centres name.
bool is_cube(const NCube& made, const NCube& truth) {
    const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(truth.size());
    for (std::size_t rotation = 0; rotation < geometry.rotations().size(); ++rotation) {
        NCube held = truth;
        held.apply(geometry.rotation_permutation(rotation));
        if (held == made) {
            return true;
        }
    }
    return false;
}

// Holds the six sides of `cube` up in turn, each in its own place, a random
// quarter-turn and a little turned in its plane, and shaken by a pixel.
void show_cube(SideScan& scan, const NCube& cube, unsigned seed, const Drawing& drawing,
               float span_of_height, int frames = 12) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> tilt(-15.0F, 15.0F);
    std::uniform_real_distribution<float> shift(-30.0F, 30.0F);
    std::uniform_real_distribution<float> shake(-1.0F, 1.0F);
    std::array<std::size_t, 6> order = {0, 1, 2, 3, 4, 5};
    // A fixed shuffle of the six: std::shuffle's draws are the library's own.
    for (std::size_t i = order.size() - 1; i > 0; --i) {
        std::swap(order.at(i), order.at(static_cast<std::size_t>(rng() >> 16U) % (i + 1)));
    }
    for (const std::size_t which : order) {
        std::vector<Color> stickers =
            cube::nxn::outside_view(cube, cube::solver::kAllFaces.at(which));
        for (auto turns = static_cast<std::uint32_t>(rng() >> 30U); turns > 0; --turns) {
            stickers = cube::test::turned_clockwise(stickers);
        }
        Pose pose = pose_for(cube.size(), span_of_height);
        pose.centre = {320.0F + shift(rng), 180.0F + (shift(rng) / 2.0F)};
        pose.degrees = tilt(rng);
        for (int frame = 0; frame < frames; ++frame) {
            Pose shaken = pose;
            shaken.centre += cv::Point2f(shake(rng), shake(rng));
            scan.add(cube::vision::read_side(cube::test::draw_side(stickers, shaken, drawing),
                                             cube.size()),
                     33.0F);
        }
        scan.add(std::nullopt);
    }
}

}  // namespace

TEST_CASE(
    "a side of N x N is cells numbered row-major, and a quarter-turn is a permutation of them",
    "[vision][nxn]") {
    using cube::vision::middle_cell;
    using cube::vision::rolled_cell;
    using cube::vision::side_length_of;

    CHECK(side_length_of(9) == 3);
    CHECK(side_length_of(25) == 5);
    CHECK(side_length_of(49) == 7);
    CHECK(side_length_of(4) == 2);
    CHECK(side_length_of(10) == 0);
    CHECK(side_length_of(1) == 0);
    CHECK(middle_cell(3) == 4);
    CHECK(middle_cell(5) == 12);

    for (int size = 2; size <= 9; ++size) {
        CAPTURE(size);
        const auto cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
        for (int roll = 0; roll < 4; ++roll) {
            // A permutation of the cells...
            std::vector<bool> taken(cells, false);
            for (std::size_t cell = 0; cell < cells; ++cell) {
                const std::size_t to = rolled_cell(size, roll, cell);
                REQUIRE(to < cells);
                CHECK_FALSE(taken.at(to));
                taken.at(to) = true;
            }
            // ...which is `roll` applications of the first quarter-turn.
            for (std::size_t cell = 0; cell < cells; ++cell) {
                std::size_t moved = cell;
                for (int i = 0; i < roll; ++i) {
                    moved = rolled_cell(size, 1, moved);
                }
                CHECK(rolled_cell(size, roll, cell) == moved);
            }
        }
        // Four quarter-turns are the identity, and an odd size's middle stays put.
        for (std::size_t cell = 0; cell < cells; ++cell) {
            std::size_t moved = cell;
            for (int i = 0; i < 4; ++i) {
                moved = rolled_cell(size, 1, moved);
            }
            CHECK(moved == cell);
        }
        if (size % 2 == 1) {
            for (int roll = 0; roll < 4; ++roll) {
                CHECK(rolled_cell(size, roll, middle_cell(size)) == middle_cell(size));
            }
        }
    }
}

TEST_CASE("a read set of 25 cells fuses reads turned under a quarter-turn", "[vision][nxn]") {
    using cube::vision::ReadSet;
    using cube::vision::rolled_cell;
    std::mt19937 rng = cube::test::deterministic_rng(5U);
    std::uniform_real_distribution<float> value(0.0F, 100.0F);
    const auto random_read = [&] {
        std::vector<cv::Vec3f> read(25);
        for (cv::Vec3f& cell : read) {
            cell = {value(rng), value(rng), value(rng)};
        }
        return read;
    };
    for (int roll = 0; roll < 4; ++roll) {
        ReadSet kept;
        ReadSet other;
        std::vector<std::vector<cv::Vec3f>> all;
        for (int i = 0; i < 9; ++i) {
            all.push_back(random_read());
            kept.add(all.back());
        }
        for (int i = 0; i < 12; ++i) {
            const std::vector<cv::Vec3f> read = random_read();
            other.add(read);
            std::vector<cv::Vec3f> turned(25);
            for (std::size_t cell = 0; cell < 25; ++cell) {
                turned.at(cell) = read.at(rolled_cell(5, roll, cell));
            }
            all.push_back(turned);
        }
        kept.absorb(other, roll);
        REQUIRE(kept.size() == all.size());
        const std::vector<cv::Vec3f> fused = kept.median();
        REQUIRE(fused.size() == 25);
        for (std::size_t cell = 0; cell < 25; ++cell) {
            for (int channel = 0; channel < 3; ++channel) {
                std::vector<float> values;
                for (const auto& read : all) {
                    values.push_back(read.at(cell)(channel));
                }
                std::ranges::sort(values);
                const float median = values.at(values.size() / 2);  // an odd count
                REQUIRE(fused.at(cell)(channel) == median);
            }
        }
    }
}

TEST_CASE("the reader finds the lattice of a 5x5 and a 7x7, and every sticker's colour",
          "[vision][nxn]") {
    for (const int size : {5, 7}) {
        CAPTURE(size);
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 4, .moves = 60});
        const Drawing drawing;
        const std::vector<Color> stickers = cube::nxn::outside_view(cube, Face::F);

        for (const float degrees : {0.0F, -18.0F, 17.0F}) {
            for (const float squash : {1.0F, 0.93F}) {
                CAPTURE(degrees, squash);
                Pose pose = pose_for(size, 0.55F);
                pose.degrees = degrees;
                pose.squash = squash;
                pose.centre = {300.0F, 190.0F};
                const cv::Mat frame = cube::test::draw_side(stickers, pose, drawing);

                const std::optional<SideRead> read = cube::vision::read_side(frame, size);
                REQUIRE(read.has_value());
                CHECK(read->size == size);
                REQUIRE(read->lab.size() == stickers.size());
                REQUIRE(read->centres.size() == stickers.size());
                CHECK(read->stickers_found == size * size);
                CHECK(read->pitch == Catch::Approx(pose.pitch).epsilon(0.08));
                for (std::size_t i = 0; i < stickers.size(); ++i) {
                    CHECK(nearest_colour(read->lab.at(i), drawing) == stickers.at(i));
                    // Where the drawing put it, to a fraction of a sticker.
                    const float column = static_cast<float>(i % static_cast<std::size_t>(size)) -
                                         (0.5F * static_cast<float>(size - 1));
                    const float row = static_cast<float>(i / static_cast<std::size_t>(size)) -
                                      (0.5F * static_cast<float>(size - 1));
                    const cv::Point2f want = cube::test::place(pose, column, row);
                    CHECK(cv::norm(read->centres.at(i) - want) <
                          static_cast<double>(0.2F * pose.pitch));
                }
            }
        }
    }
}

TEST_CASE("a side is read at the size it is asked for and no other", "[vision][nxn]") {
    const NCube five = cube::nxn::random_scramble({.size = 5, .seed = 2, .moves = 40});
    const cv::Mat frame = cube::test::draw_side(cube::nxn::outside_view(five, Face::U),
                                                pose_for(5, 0.55F), Drawing{});
    CHECK(cube::vision::read_side(frame, 5).has_value());
    // Nine of its twenty-five stickers are not a 3x3's lattice, and a 7x7
    // wants more stickers than there are.
    CHECK_FALSE(cube::vision::read_side(frame, 7).has_value());
    CHECK_FALSE(cube::vision::read_side(frame, 1).has_value());
    CHECK_FALSE(cube::vision::read_side(frame, 10).has_value());
}

TEST_CASE("a frame is worked at more width for a bigger cube, and read the same where it is not",
          "[vision][nxn]") {
    const cv::Mat wide(720, 1280, CV_8UC3, cv::Scalar(90, 90, 90));
    CHECK(cube::vision::prepare_frame(wide)->working.cols == 640);
    CHECK(cube::vision::prepare_frame(wide, 3)->working.cols == 640);
    CHECK(cube::vision::prepare_frame(wide, 5)->working.cols == 960);
    CHECK(cube::vision::prepare_frame(wide, 7)->working.cols == 1280);
    CHECK(cube::vision::prepare_frame(wide, 7)->scale == Catch::Approx(1.0F));
    // A frame that is already no wider is read as it is.
    const cv::Mat narrow(360, 640, CV_8UC3, cv::Scalar(90, 90, 90));
    CHECK(cube::vision::prepare_frame(narrow, 7)->working.cols == 640);
}

TEST_CASE("steadiness is judged in sticker spacings, so a small sticker is held to a smaller shake",
          "[vision][nxn]") {
    for (const int size : {3, 5, 7}) {
        CAPTURE(size);
        SideRead read;
        read.size = size;
        const auto cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
        read.centres.assign(cells, {100.0F, 100.0F});
        read.lab.assign(cells, cv::Vec3f{50.0F, 0.0F, 0.0F});
        read.found.assign(cells, true);
        // An odd size's middle sticker is the point that is watched.
        read.centres.at(cube::vision::middle_cell(size)) = {200.0F, 120.0F};
        read.pitch = 40.0F * 3.0F / static_cast<float>(size);

        const float limit = cube::vision::kSteadyFraction * read.pitch;
        CHECK(cube::vision::steady_point(read) == cv::Point2f(200.0F, 120.0F));
        CHECK(cube::vision::held_still(read, {200.0F + (0.8F * limit), 120.0F}));
        CHECK_FALSE(cube::vision::held_still(read, {200.0F + (1.2F * limit), 120.0F}));
    }
    // An even size has no middle sticker: it is the middle of the four.
    SideRead even;
    even.size = 4;
    even.centres.assign(16, {0.0F, 0.0F});
    even.centres.at(5) = {10.0F, 10.0F};
    even.centres.at(6) = {20.0F, 10.0F};
    even.centres.at(9) = {10.0F, 20.0F};
    even.centres.at(10) = {20.0F, 20.0F};
    CHECK(cube::vision::steady_point(even) == cv::Point2f(15.0F, 15.0F));
}

TEST_CASE("six sides of a 5x5 and a 7x7 are read into the cube they came from", "[vision][nxn]") {
    for (const int size : {5, 7}) {
        CAPTURE(size);
        const NCube cube = cube::nxn::random_scramble(
            {.size = size, .seed = static_cast<unsigned>(10 + size), .moves = 20 * size});
        SideScan scan(size);
        REQUIRE(scan.size() == size);
        show_cube(scan, cube, 77U, Drawing{}, 0.6F);

        REQUIRE(scan.sides().size() == 6);
        for (const SideScan::Side& side : scan.sides()) {
            CHECK(side.lab.size() == static_cast<std::size_t>(size * size));
            CHECK(side.reads >= SideScan::kMinReads);
        }
        const auto made = scan.scanned_n();
        REQUIRE(made.has_value());
        CHECK(is_cube(made->cube, cube));
        CHECK(made->cube.size() == size);
        REQUIRE(scan.cube_n().has_value());
        CHECK(*scan.cube_n() == made->cube);

        // The 3x3's answers are the 3x3's: nothing there for another size.
        CHECK_FALSE(scan.scanned().has_value());
        CHECK_FALSE(scan.cube().has_value());

        // Every sticker was given its right name.
        const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
        REQUIRE(diagnosis.naming.has_value());
        REQUIRE(diagnosis.result_n.has_value());
        REQUIRE(diagnosis.assembly.has_value());
        CHECK(diagnosis.assembly->path == cube::vision::AssemblyPath::single_candidate);
        for (const auto& face : diagnosis.naming->faces) {
            CHECK(face.size() == static_cast<std::size_t>(size * size));
        }
    }
}

TEST_CASE("a 5x5 is read in a warm lamp's light, where red and orange are 16 Lab apart",
          "[vision][nxn]") {
    const NCube cube = cube::nxn::random_scramble({.size = 5, .seed = 31, .moves = 100});
    SideScan scan(5);
    show_cube(scan, cube, 9U, cube::test::warm_lamp_drawing(), 0.65F);
    const auto made = scan.scanned_n();
    REQUIRE(made.has_value());
    CHECK(is_cube(made->cube, cube));
}

TEST_CASE("a 3x3 scanned as a size-3 scan has the same cube from both answers", "[vision][nxn]") {
    const NCube cube = cube::nxn::random_scramble({.size = 3, .seed = 5, .moves = 25});
    // A scramble of outer turns keeps the centres where the scheme puts them.
    SideScan scan(3);
    show_cube(scan, cube, 3U, Drawing{}, 0.5F);
    const auto three = scan.scanned();
    const auto any = scan.scanned_n();
    if (three.has_value()) {
        REQUIRE(any.has_value());
        CHECK(NCube::from_cube3(three->cube) == any->cube);
        CHECK(three->cast == any->cast);
    } else {
        // Six flat sides that make several cubes: the same verdict twice.
        REQUIRE_FALSE(any.has_value());
        CHECK(three.error() == any.error());
    }
}

TEST_CASE("a finger across a sticker of a 5x5 does not stop the side being read", "[vision][nxn]") {
    const NCube cube = cube::nxn::random_scramble({.size = 5, .seed = 6, .moves = 100});
    SideScan scan(5);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> shake(-0.8F, 0.8F);
    for (const Face face : cube::solver::kAllFaces) {
        const std::vector<Color> stickers = cube::nxn::outside_view(cube, face);
        Pose pose = pose_for(5, 0.6F);
        for (int frame = 0; frame < 12; ++frame) {
            Pose shaken = pose;
            shaken.centre += cv::Point2f(shake(rng), shake(rng));
            cv::Mat image = cube::test::draw_side(stickers, shaken, Drawing{});
            // Over a different sticker of the right-hand column each time (a
            // finger comes in from the edge and covers what it reaches).
            cube::test::lay_finger(image, shaken, 4 + (5 * (frame % 5)), 5);
            scan.add(cube::vision::read_side(image, 5), 33.0F);
        }
        scan.add(std::nullopt);
    }
    const auto made = scan.scanned_n();
    REQUIRE(made.has_value());
    CHECK(is_cube(made->cube, cube));
}

TEST_CASE("a read of another size is not a read of this cube", "[vision][nxn]") {
    const NCube three = cube::nxn::random_scramble({.size = 3, .seed = 8, .moves = 25});
    const cv::Mat frame = cube::test::draw_side(cube::nxn::outside_view(three, Face::F),
                                                pose_for(3, 0.5F), Drawing{});
    const auto read = cube::vision::read_side(frame, 3);
    REQUIRE(read.has_value());
    SideScan five(5);
    for (int i = 0; i < 20; ++i) {
        five.add(read, 33.0F);
    }
    CHECK(five.sides().empty());
    CHECK_FALSE(five.reading());
}

TEST_CASE("an even size is read side by side but not named", "[vision][nxn]") {
    // A 2x2 and a 4x4 have no middle sticker to seed a colour's cluster from.
    // What is read is read; what cannot be named says so (P3 of the plan).
    for (const int size : {2, 4, 6}) {
        CAPTURE(size);
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 9, .moves = 30});
        const Pose pose = pose_for(size, 0.6F);
        const cv::Mat frame =
            cube::test::draw_side(cube::nxn::outside_view(cube, Face::F), pose, Drawing{});
        const auto read = cube::vision::read_side(frame, size);
        REQUIRE(read.has_value());
        CHECK(read->size == size);
        CHECK(read->lab.size() == static_cast<std::size_t>(size * size));

        std::array<cube::vision::CapturedFace, 6> faces{};
        for (cube::vision::CapturedFace& face : faces) {
            face.lab = read->lab;
        }
        CHECK_FALSE(cube::vision::resolve_colors(faces).has_value());
        CHECK_FALSE(cube::vision::find_white_reference(faces).has_value());
    }
}

TEST_CASE("sticker colours are named at any odd size, exactly N^2 of each", "[vision][nxn]") {
    for (const int size : {3, 5, 7, 9}) {
        CAPTURE(size);
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 12, .moves = 50});
        const Drawing drawing;
        std::array<cube::vision::CapturedFace, 6> faces{};
        std::array<std::vector<Color>, 6> truth;
        for (std::size_t side = 0; side < 6; ++side) {
            truth.at(side) = cube::nxn::outside_view(cube, cube::solver::kAllFaces.at(side));
            faces.at(side).lab.clear();
            for (const Color colour : truth.at(side)) {
                faces.at(side).lab.push_back(lab_of(drawing.paint(colour)));
            }
        }
        const auto named = cube::vision::resolve_colors(faces);
        REQUIRE(named.has_value());
        std::array<int, 7> counts{};
        for (std::size_t side = 0; side < 6; ++side) {
            REQUIRE(named->at(side).size() == truth.at(side).size());
            for (const cube::model::Facelet& facelet : named->at(side)) {
                ++counts.at(static_cast<std::size_t>(facelet.color));
            }
        }
        for (std::size_t colour = 1; colour < counts.size(); ++colour) {
            CHECK(counts.at(colour) == size * size);
        }
    }
}
