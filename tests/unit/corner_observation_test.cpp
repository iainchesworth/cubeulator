#include <catch2/catch_test_macros.hpp>

#include <cube/model/scan_progress.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_conversion.hpp>
#include <cube/vision/color_classification.hpp>
#include <cube/vision/corner_observation.hpp>

#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace cube::vision;
using cube::model::FacetColor;

namespace {

// The BGR references default_palette()'s Lab entries were computed from
// (the palette itself only stores Lab, so the renderer needs its own copy).
cv::Scalar bgr_of(FacetColor color) {
    switch (color) {
        case FacetColor::white:
            return {255, 255, 255};
        case FacetColor::yellow:
            return {0, 213, 255};
        case FacetColor::red:
            return {0, 0, 196};
        case FacetColor::orange:
            return {0, 106, 255};
        case FacetColor::green:
            return {72, 155, 0};
        case FacetColor::blue:
            return {173, 70, 0};
        case FacetColor::unknown:
            return {0, 0, 0};
    }
    return {0, 0, 0};
}

FacetColor classify_lab(const cv::Vec3f& lab) {
    const auto palette = default_palette();
    FacetColor best = FacetColor::unknown;
    float best_distance = std::numeric_limits<float>::max();
    for (const auto& entry : palette.entries) {
        const cv::Vec3f diff = lab - entry.lab;
        const float distance = diff.dot(diff);
        if (distance < best_distance) {
            best_distance = distance;
            best = entry.color;
        }
    }
    return best;
}

// Isometric screen bases for the documented corner pose, pitch 40px: the
// top face's rhombus edges run down-right (+col) and down-left (+row); the
// side faces share the diagonal top edges and drop straight down.
constexpr float kPitch = 40.0F;
const cv::Point2f kSharedCorner(200.0F, 200.0F);
const cv::Point2f kColIso(34.641F, 20.0F);       // pitch * (cos30, sin30)
const cv::Point2f kRowIsoLeft(-34.641F, 20.0F);  // top face row basis
const cv::Point2f kColIsoUp(34.641F, -20.0F);    // lower-right face col basis
const cv::Point2f kRowDown(0.0F, 40.0F);         // side faces row basis
constexpr float kCellFillHalfSpan = 0.4F;        // 80% fill leaves a 20% gap between cells

void draw_face(cv::Mat& frame, cv::Point2f anchor, cv::Point2f col_step, cv::Point2f row_step,
               const std::array<FacetColor, 9>& colors) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const cv::Point2f center =
                anchor + static_cast<float>(col - 1) * col_step + static_cast<float>(row - 1) * row_step;
            const cv::Point2f half_col = kCellFillHalfSpan * col_step;
            const cv::Point2f half_row = kCellFillHalfSpan * row_step;
            const std::array<cv::Point2f, 4> quad = {center - half_col - half_row, center + half_col - half_row,
                                                     center + half_col + half_row, center - half_col + half_row};
            std::vector<cv::Point> points;
            for (const auto& corner : quad) {
                points.emplace_back(static_cast<int>(std::lround(corner.x)),
                                    static_cast<int>(std::lround(corner.y)));
            }
            cv::fillConvexPoly(frame, points, bgr_of(colors[static_cast<std::size_t>(row * 3 + col)]));
        }
    }
}

// Renders the documented iso arrangement on a dark canvas: the top face's
// rhombus with its bottom vertex at the shared corner, the two side
// parallelograms hanging below-left and below-right of it.
cv::Mat synthetic_corner_frame(const std::array<FacetColor, 9>& top, const std::array<FacetColor, 9>& lower_left,
                               const std::array<FacetColor, 9>& lower_right) {
    cv::Mat frame(400, 400, CV_8UC3, cv::Scalar(20, 20, 20));
    // Anchors are each face's centre cell; the face corner touching the
    // shared corner pins them: top's bottom vertex, lower-left's top-right
    // corner, lower-right's top-left corner all sit at kSharedCorner.
    const cv::Point2f top_anchor = kSharedCorner - 1.5F * (kColIso + kRowIsoLeft);
    const cv::Point2f lower_left_anchor = kSharedCorner - 1.5F * kColIso + 1.5F * kRowDown;
    const cv::Point2f lower_right_anchor = kSharedCorner + 1.5F * kColIsoUp + 1.5F * kRowDown;
    draw_face(frame, top_anchor, kColIso, kRowIsoLeft, top);
    draw_face(frame, lower_left_anchor, kColIso, kRowDown, lower_left);
    draw_face(frame, lower_right_anchor, kColIsoUp, kRowDown, lower_right);
    return frame;
}

// A flat single-face frame like grid_detection_test.cpp's: 9 white 60x60
// squares - the false capture observe_corner must never accept.
cv::Mat synthetic_flat_face_frame() {
    cv::Mat frame = cv::Mat::zeros(230, 230, CV_8UC3);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            cv::rectangle(frame, cv::Rect(15 + col * 70, 15 + row * 70, 60, 60), cv::Scalar(255, 255, 255),
                          cv::FILLED);
        }
    }
    return frame;
}

// --- round-trip ground truth ------------------------------------------------

// Solver facelet index shown at each on-screen row-major corner-view cell,
// re-derived here independently of corner_slot_mappings() straight from
// cube.hpp's facelet layout (U/D: row=z+1,col=x+1; R/L: row=1-y,col=z+1;
// F/B: row=1-y,col=x+1; x: L->R, y: D->U, z: B->F) and the documented
// poses - so the test checks the shipped tables against geometry rather
// than against themselves.
//
// first_corner (viewer along +x+y+z, shared corner URF):
//   top U: rows advance along +z, cols along +x -> (z+1, x+1) = (r, c).
//   lower-left F: rows along -y, cols along +x -> (1-y, x+1) = (r, c).
//   lower-right R: rows along -y, cols along -z -> (1-y, z+1) = (r, 2-c).
// second_corner (viewer along -x-y-z, shared corner DLB, D up-screen):
//   top D: rows along -x, cols along -z -> (z+1, x+1) = (2-c, 2-r).
//   lower-left L: rows along +y, cols along -z -> (1-y, z+1) = (2-r, 2-c).
//   lower-right B: rows along +y, cols along +x -> (1-y, x+1) = (2-r, c).
constexpr std::array<int, 9> kGeomIdentity = {0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr std::array<int, 9> kGeomFlipHorizontal = {2, 1, 0, 5, 4, 3, 8, 7, 6};
constexpr std::array<int, 9> kGeomFlipVertical = {6, 7, 8, 3, 4, 5, 0, 1, 2};
constexpr std::array<int, 9> kGeomRotate180 = {8, 7, 6, 5, 4, 3, 2, 1, 0};
constexpr std::array<int, 9> kGeomAntiTranspose = {8, 5, 2, 7, 4, 1, 6, 3, 0};

std::array<std::array<int, 9>, 3> corner_view_geometry(CornerViewId view) {
    if (view == CornerViewId::first_corner) {
        return {kGeomIdentity, kGeomIdentity, kGeomFlipHorizontal};
    }
    return {kGeomAntiTranspose, kGeomRotate180, kGeomFlipVertical};
}

cube::solver::Face to_solver_face(cube::model::Face face) {
    switch (face) {
        case cube::model::Face::U:
            return cube::solver::Face::U;
        case cube::model::Face::R:
            return cube::solver::Face::R;
        case cube::model::Face::F:
            return cube::solver::Face::F;
        case cube::model::Face::D:
            return cube::solver::Face::D;
        case cube::model::Face::L:
            return cube::solver::Face::L;
        case cube::model::Face::B:
            return cube::solver::Face::B;
    }
    return cube::solver::Face::U;
}

FacetColor to_model_color(cube::solver::Color color) {
    switch (color) {
        case cube::solver::Color::White:
            return FacetColor::white;
        case cube::solver::Color::Yellow:
            return FacetColor::yellow;
        case cube::solver::Color::Red:
            return FacetColor::red;
        case cube::solver::Color::Orange:
            return FacetColor::orange;
        case cube::solver::Color::Green:
            return FacetColor::green;
        case cube::solver::Color::Blue:
            return FacetColor::blue;
    }
    return FacetColor::unknown;
}

}  // namespace

TEST_CASE("observe_corner reads a synthetic corner view's 27 stickers", "[vision]") {
    // Deliberately asymmetric colour patterns so any cell-order or
    // face-assignment mistake shows up as a colour mismatch.
    const std::array<FacetColor, 9> top = {FacetColor::white, FacetColor::red,   FacetColor::green,
                                           FacetColor::blue,  FacetColor::yellow, FacetColor::orange,
                                           FacetColor::red,   FacetColor::white,  FacetColor::blue};
    const std::array<FacetColor, 9> lower_left = {FacetColor::green,  FacetColor::orange, FacetColor::white,
                                                  FacetColor::yellow, FacetColor::blue,   FacetColor::red,
                                                  FacetColor::orange, FacetColor::green,  FacetColor::yellow};
    const std::array<FacetColor, 9> lower_right = {FacetColor::blue,   FacetColor::yellow, FacetColor::orange,
                                                   FacetColor::red,    FacetColor::white,  FacetColor::green,
                                                   FacetColor::yellow, FacetColor::blue,   FacetColor::red};

    const auto observation = observe_corner(synthetic_corner_frame(top, lower_left, lower_right));
    REQUIRE(observation.has_value());
    REQUIRE(observation->geometry_confidence > 0.9F);

    for (std::size_t k = 0; k < 9; ++k) {
        REQUIRE(classify_lab(observation->top.lab[k]) == top[k]);
        REQUIRE(classify_lab(observation->lower_left.lab[k]) == lower_left[k]);
        REQUIRE(classify_lab(observation->lower_right.lab[k]) == lower_right[k]);
    }
}

TEST_CASE("observe_corner rejects a flat single-face frame", "[vision]") {
    const auto observation = observe_corner(synthetic_flat_face_frame());
    REQUIRE_FALSE(observation.has_value());
    REQUIRE(observation.error() == cube::CubeError::no_valid_cube_detected);
}

TEST_CASE("observe_corner rejects empty and blank frames", "[vision]") {
    REQUIRE_FALSE(observe_corner(cv::Mat()).has_value());
    REQUIRE_FALSE(observe_corner(cv::Mat::zeros(240, 240, CV_8UC3)).has_value());
}

TEST_CASE("corner_slot_mappings cover all six faces and fix the centre", "[vision]") {
    const auto first = corner_slot_mappings(CornerViewId::first_corner);
    const auto second = corner_slot_mappings(CornerViewId::second_corner);

    REQUIRE(first[0].face == cube::model::Face::U);
    REQUIRE(first[1].face == cube::model::Face::F);
    REQUIRE(first[2].face == cube::model::Face::R);
    REQUIRE(second[0].face == cube::model::Face::D);
    REQUIRE(second[1].face == cube::model::Face::L);
    REQUIRE(second[2].face == cube::model::Face::B);

    for (const auto& mappings : {first, second}) {
        for (const auto& mapping : mappings) {
            REQUIRE(mapping.to_scan_order[4] == 4);
            // A valid permutation of 0..8: every index hit exactly once.
            std::array<bool, 9> seen{};
            for (const int index : mapping.to_scan_order) {
                REQUIRE(index >= 0);
                REQUIRE(index < 9);
                seen[static_cast<std::size_t>(index)] = true;
            }
            for (const bool hit : seen) {
                REQUIRE(hit);
            }
        }
    }
}

TEST_CASE("corner slot mappings round-trip a scrambled cube through cube_from_scan", "[vision]") {
    using cube::solver::Cube;
    using cube::solver::Move;
    using SFace = cube::solver::Face;

    const Cube truth = Cube::solved()
                           .apply(Move{SFace::R, 1})
                           .apply(Move{SFace::U, 1})
                           .apply(Move{SFace::F, 3})
                           .apply(Move{SFace::D, 2})
                           .apply(Move{SFace::L, 1})
                           .apply(Move{SFace::B, 3});

    // Synthesize each corner view's 27 stickers straight from the cube via
    // the independent geometric derivation above, then push them through
    // the shipped mappings into a ScanProgress exactly as a corner capture
    // would. cube_from_scan()'s own guided-scan permutations sit between
    // the two, so this only closes if to_scan_order composes correctly
    // with them - the end-to-end property corner capture relies on.
    cube::model::ScanProgress progress;
    for (const auto view : {CornerViewId::first_corner, CornerViewId::second_corner}) {
        const auto mappings = corner_slot_mappings(view);
        const auto geometry = corner_view_geometry(view);
        for (std::size_t slot = 0; slot < 3; ++slot) {
            std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets{};
            for (std::size_t k = 0; k < 9; ++k) {
                const auto color = truth.at(to_solver_face(mappings[slot].face), geometry[slot][k]);
                facelets[static_cast<std::size_t>(mappings[slot].to_scan_order[k])] =
                    cube::model::Facelet{.color = to_model_color(color), .confidence = 1.0F};
            }
            progress.set_face(mappings[slot].face, facelets);
        }
    }
    REQUIRE(progress.is_complete());

    const auto rebuilt = cube::solver::cube_from_scan(progress);
    REQUIRE(rebuilt.has_value());
    REQUIRE(*rebuilt == truth);
}
