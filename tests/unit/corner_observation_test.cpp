#include <catch2/catch_test_macros.hpp>

#include <cube/model/scan_progress.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_conversion.hpp>
#include <cube/vision/color_classification.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/orientation_lattice.hpp>
#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <numbers>
#include <utility>
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

// --- hinted rotated-corner infrastructure -----------------------------------
//
// An independent perspective renderer in the style of pose_estimation_test's
// corner construction: the unit cube (display frame, centred at the origin,
// U = +y / R = +x / F = +z) genuinely rotated and pinhole-projected under
// the SAME camera model the projectors document (f = frame width, principal
// point centred), each visible face's nine sticker quads filled with that
// face's solid colour. Solid per-face colours keep the ground truth free of
// any cell-order bookkeeping: a correctly hinted observation must read each
// view role as one uniform colour, and WHICH colour pins the role-to-
// physical-face assignment.

constexpr double kDegree = std::numbers::pi / 180.0;

// Display frame (y up, z toward viewer) <-> OpenCV camera frame (y down,
// z into screen), the conjugation pose_estimation.hpp documents.
const cv::Matx33d kFlip(1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0);

cv::Matx33d rot_x_display(double deg) {
    const double c = std::cos(deg * kDegree);
    const double s = std::sin(deg * kDegree);
    return {1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c};
}

cv::Matx33d rot_y_display(double deg) {
    const double c = std::cos(deg * kDegree);
    const double s = std::sin(deg * kDegree);
    return {c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c};
}

cv::Matx33d rot_z_display(double deg) {
    const double c = std::cos(deg * kDegree);
    const double s = std::sin(deg * kDegree);
    return {c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0};
}

// The exact isometric corner presentation the fixed-pose detector prompts:
// URF toward the viewer with U most up-screen (yaw -45 about y, then pitch
// atan(1/sqrt(2)) about x).
cv::Matx33d iso_corner_rotation() {
    return rot_x_display(std::atan(1.0 / std::sqrt(2.0)) / kDegree) * rot_y_display(-45.0);
}

struct RenderedModelFace {
    cv::Vec3d normal;  // display frame, outward
    cv::Vec3d u;       // in-face edge directions (any orthonormal pair)
    cv::Vec3d v;
    FacetColor color;
};

const std::array<RenderedModelFace, 6> kRenderedFaces = {{
    {{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, FacetColor::white},    // U
    {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, FacetColor::red},      // R
    {{0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, FacetColor::green},    // F
    {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, FacetColor::yellow},  // D
    {{-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, FacetColor::orange},  // L
    {{0.0, 0.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, FacetColor::blue},    // B
}};

const cv::Size kHintFrame(480, 480);
const cv::Vec3d kHintTranslation(0.0, 0.0, 3.4);

cv::Point2f project_display_point(const cv::Vec3d& point_display, const cv::Matx33d& camera_rotation,
                                  const cv::Vec3d& translation, cv::Size frame) {
    const cv::Vec3d in_camera = camera_rotation * (kFlip * point_display) + translation;
    const double focal = static_cast<double>(frame.width);
    return {static_cast<float>(focal * in_camera[0] / in_camera[2] + 0.5 * frame.width),
            static_cast<float>(focal * in_camera[1] / in_camera[2] + 0.5 * frame.height)};
}

cv::Mat render_rotated_cube(const cv::Matx33d& display_rotation, const cv::Vec3d& translation, cv::Size size) {
    cv::Mat frame(size, CV_8UC3, cv::Scalar(20, 20, 20));
    const cv::Matx33d camera_rotation = kFlip * display_rotation * kFlip;
    for (const auto& face : kRenderedFaces) {
        // Front faces of a convex cube never overlap in projection, so no
        // draw-order care is needed; slightly-past-edge-on faces render as
        // slivers just like a real camera would see them.
        const cv::Vec3d normal_camera = camera_rotation * (kFlip * face.normal);
        if (normal_camera[2] >= -0.05) {
            continue;
        }
        for (int i = -1; i <= 1; ++i) {
            for (int j = -1; j <= 1; ++j) {
                const cv::Vec3d center =
                    0.5 * face.normal + (static_cast<double>(i) / 3.0) * face.u + (static_cast<double>(j) / 3.0) * face.v;
                constexpr double kHalf = 0.4 / 3.0;  // 80% fill, 20% gaps
                const std::array<std::pair<double, double>, 4> corner_signs = {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
                std::vector<cv::Point> polygon;
                for (const auto& [su, sv] : corner_signs) {
                    const cv::Vec3d corner = center + su * kHalf * face.u + sv * kHalf * face.v;
                    const cv::Point2f pixel = project_display_point(corner, camera_rotation, translation, size);
                    polygon.emplace_back(static_cast<int>(std::lround(pixel.x)),
                                         static_cast<int>(std::lround(pixel.y)));
                }
                cv::fillConvexPoly(frame, polygon, bgr_of(face.color));
            }
        }
    }
    return frame;
}

PoseSeed seed_for_rotation(const cv::Matx33d& display_rotation, const cv::Vec3d& translation) {
    const cv::Matx33d camera_rotation = kFlip * display_rotation * kFlip;
    cv::Vec3d rvec;
    cv::Rodrigues(camera_rotation, rvec);
    return PoseSeed{rvec, translation};
}

void require_uniform_view(const CornerFaceView& view, FacetColor expected) {
    for (const auto& lab : view.lab) {
        REQUIRE(classify_lab(lab) == expected);
    }
}

// The end-to-end property the tracker consumes: a hinted rotated-corner
// detection must yield a pose whose lattice resolution against the ground
// truth is tight and unambiguous (the gauge itself is free - role cycling
// is a lattice element by design).
AlignmentResolution require_pose_resolves(const CornerObservation& observation, const PoseSeed& seed,
                                          const cv::Matx33d& display_rotation) {
    const auto pose = project_cube_from_corner(observation, kHintFrame, &seed);
    REQUIRE(pose.has_value());
    REQUIRE(pose->orientation_valid);
    REQUIRE(pose->max_reproj_fraction <= 0.15);
    const Quat truth = display_quaternion(kFlip * display_rotation * kFlip);
    const auto resolution = resolve_alignment(pose->orientation, truth);
    REQUIRE(resolution.theta_deg < 6.0F);
    REQUIRE(resolution.margin_deg > 20.0F);
    return resolution;
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

TEST_CASE("compute_corner_hint reproduces the fixed isometric arrangement", "[vision]") {
    // At the exact prompted pose the hint must collapse to the historical
    // constants: sector centres at -90/+150/+30 image degrees and the
    // documented unit bases - the "strict generalisation" claim, checked
    // numerically. (All three faces foreshorten equally at iso, so the
    // shorter-axis normalisation must land every basis at unit length.)
    const auto hint = compute_corner_hint(seed_for_rotation(iso_corner_rotation(), kHintTranslation));
    REQUIRE(hint.has_value());

    const auto near_point = [](cv::Point2f actual, cv::Point2f expected) {
        return std::hypot(actual.x - expected.x, actual.y - expected.y) < 0.02F;
    };
    constexpr float kCos30 = 0.86602540F;
    // Roles {top, lower_left, lower_right}: U up-screen, F lower-left,
    // R lower-right.
    REQUIRE(near_point(hint->face_dir[0], {0.0F, -1.0F}));
    REQUIRE(near_point(hint->face_dir[1], {-kCos30, 0.5F}));
    REQUIRE(near_point(hint->face_dir[2], {kCos30, 0.5F}));
    REQUIRE(near_point(hint->col_dir[0], {kCos30, 0.5F}));   // kTopColDir
    REQUIRE(near_point(hint->row_dir[0], {-kCos30, 0.5F}));  // kTopRowDir
    REQUIRE(near_point(hint->col_dir[1], {kCos30, 0.5F}));   // kSideColRightDir
    REQUIRE(near_point(hint->row_dir[1], {0.0F, 1.0F}));     // kSideRowDir
    REQUIRE(near_point(hint->col_dir[2], {kCos30, -0.5F}));  // kSideColUpDir
    REQUIRE(near_point(hint->row_dir[2], {0.0F, 1.0F}));     // kSideRowDir
}

TEST_CASE("compute_corner_hint refuses non-corner poses", "[vision]") {
    // Frontal: only one face clears the visibility floor.
    REQUIRE_FALSE(compute_corner_hint(PoseSeed{cv::Vec3d(0.0, 0.0, 0.0), cv::Vec3d(0.0, 0.0, 3.0)}).has_value());
    // Edge-on (two faces visible, the third exactly on the limb).
    REQUIRE_FALSE(compute_corner_hint(seed_for_rotation(rot_y_display(-45.0), kHintTranslation)).has_value());
}

TEST_CASE("a hinted observation at the prompted pose matches the unhinted reading", "[vision]") {
    // Same legacy synthetic frame as the fixed-pose test; a hint computed
    // from the iso pose must partition and fit to the same lattices, so
    // every cell classifies identically to the unhinted read (the fits
    // converge through an affine refit, so float-exact equality is not the
    // contract - classification equality is).
    const std::array<FacetColor, 9> top = {FacetColor::white, FacetColor::red,    FacetColor::green,
                                           FacetColor::blue,  FacetColor::yellow, FacetColor::orange,
                                           FacetColor::red,   FacetColor::white,  FacetColor::blue};
    const std::array<FacetColor, 9> lower_left = {FacetColor::green,  FacetColor::orange, FacetColor::white,
                                                  FacetColor::yellow, FacetColor::blue,   FacetColor::red,
                                                  FacetColor::orange, FacetColor::green,  FacetColor::yellow};
    const std::array<FacetColor, 9> lower_right = {FacetColor::blue,   FacetColor::yellow, FacetColor::orange,
                                                   FacetColor::red,    FacetColor::white,  FacetColor::green,
                                                   FacetColor::yellow, FacetColor::blue,   FacetColor::red};
    const cv::Mat frame = synthetic_corner_frame(top, lower_left, lower_right);

    const auto unhinted = observe_corner(frame);
    REQUIRE(unhinted.has_value());
    const auto hint = compute_corner_hint(seed_for_rotation(iso_corner_rotation(), kHintTranslation));
    REQUIRE(hint.has_value());
    const auto hinted = observe_corner(frame, &*hint);
    REQUIRE(hinted.has_value());

    for (std::size_t k = 0; k < 9; ++k) {
        REQUIRE(classify_lab(hinted->top.lab[k]) == classify_lab(unhinted->top.lab[k]));
        REQUIRE(classify_lab(hinted->lower_left.lab[k]) == classify_lab(unhinted->lower_left.lab[k]));
        REQUIRE(classify_lab(hinted->lower_right.lab[k]) == classify_lab(unhinted->lower_right.lab[k]));
    }
}

TEST_CASE("observe_corner with a hint reads an in-plane rotated corner view", "[vision]") {
    // 40 degrees of in-plane spin: far past the fixed sectors' hand-tilt
    // tolerance, but within the +-60 degree band where U is still the most
    // up-screen face - so the roles must come out EXACTLY as at the
    // prompted pose (top U/white, lower-left F/green, lower-right R/red),
    // pinning the hint's role-stability contract, and the resolved gauge
    // must be the identity element.
    const cv::Matx33d rotation = rot_z_display(40.0) * iso_corner_rotation();
    const cv::Mat frame = render_rotated_cube(rotation, kHintTranslation, kHintFrame);
    const PoseSeed seed = seed_for_rotation(rotation, kHintTranslation);

    const auto hint = compute_corner_hint(seed);
    REQUIRE(hint.has_value());
    const auto observation = observe_corner(frame, &*hint);
    REQUIRE(observation.has_value());
    REQUIRE(observation->geometry_confidence > 0.9F);

    require_uniform_view(observation->top, FacetColor::white);
    require_uniform_view(observation->lower_left, FacetColor::green);
    require_uniform_view(observation->lower_right, FacetColor::red);

    const auto resolution = require_pose_resolves(*observation, seed, rotation);
    REQUIRE(resolution.lattice_index == kIdentityLatticeIndex);

    // A hint from a mispredicted pose (8 degrees of extra tilt - a realistic
    // one-pass prediction error) must still read the frame correctly: the
    // per-face affine refit owns the residual, the hint only has to put the
    // sectors and provisional bases in the right neighbourhood.
    const auto perturbed_hint =
        compute_corner_hint(seed_for_rotation(rot_x_display(8.0) * rotation, kHintTranslation));
    REQUIRE(perturbed_hint.has_value());
    const auto perturbed_observation = observe_corner(frame, &*perturbed_hint);
    REQUIRE(perturbed_observation.has_value());
    require_uniform_view(perturbed_observation->top, FacetColor::white);
    require_uniform_view(perturbed_observation->lower_left, FacetColor::green);
    require_uniform_view(perturbed_observation->lower_right, FacetColor::red);
}

TEST_CASE("observe_corner with a hint reads an obliquely tilted corner view", "[vision]") {
    // Out-of-plane tilt: 20 degrees of extra pitch leaves the top face
    // nearly frontal (35 degrees) and both hanging faces at 66 degrees, so
    // their cell lattices foreshorten ~1.6x more along one axis than the
    // other - exactly what the hint's shorter-axis basis scaling exists
    // for (unit bases would push the outer cells past the lattice residual
    // gate). The tilt is capped where it is because the flank faces' inter-
    // sticker gaps are already down to the same ~5 px the prompted pose
    // presents; more tilt makes the render physically undetectable (cells
    // merge), which is the detector's honest limit, not the hint's.
    const cv::Matx33d rotation = rot_x_display(20.0) * iso_corner_rotation();
    const cv::Mat frame = render_rotated_cube(rotation, kHintTranslation, kHintFrame);
    const PoseSeed seed = seed_for_rotation(rotation, kHintTranslation);

    const auto hint = compute_corner_hint(seed);
    REQUIRE(hint.has_value());
    const auto observation = observe_corner(frame, &*hint);
    REQUIRE(observation.has_value());

    // Role determinism under this tilt is not pinned (it is convention, not
    // correctness); what IS pinned: three uniform faces, one of each
    // visible colour, and a tight lattice resolution against the truth.
    std::array<FacetColor, 3> role_colors{};
    const std::array<const CornerFaceView*, 3> views{&observation->top, &observation->lower_left,
                                                     &observation->lower_right};
    for (std::size_t role = 0; role < views.size(); ++role) {
        role_colors[role] = classify_lab(views[role]->lab[0]);
        require_uniform_view(*views[role], role_colors[role]);
    }
    std::ranges::sort(role_colors);
    std::array<FacetColor, 3> expected = {FacetColor::white, FacetColor::red, FacetColor::green};
    std::ranges::sort(expected);
    REQUIRE(role_colors == expected);

    require_pose_resolves(*observation, seed, rotation);
}

TEST_CASE("observe_corner with a hint re-assigns roles past the 120-degree boundary", "[vision]") {
    // 140 degrees of in-plane spin: the physical R face is now the most
    // up-screen one, so the hint must hand the top role to R and cycle the
    // others by chirality (top R/red, lower-left U/white, lower-right
    // F/green) - and the resolved gauge is exactly the non-identity
    // lattice element that cycling represents.
    const cv::Matx33d rotation = rot_z_display(140.0) * iso_corner_rotation();
    const cv::Mat frame = render_rotated_cube(rotation, kHintTranslation, kHintFrame);
    const PoseSeed seed = seed_for_rotation(rotation, kHintTranslation);

    const auto hint = compute_corner_hint(seed);
    REQUIRE(hint.has_value());
    const auto observation = observe_corner(frame, &*hint);
    REQUIRE(observation.has_value());

    require_uniform_view(observation->top, FacetColor::red);
    require_uniform_view(observation->lower_left, FacetColor::white);
    require_uniform_view(observation->lower_right, FacetColor::green);

    const auto resolution = require_pose_resolves(*observation, seed, rotation);
    REQUIRE(resolution.lattice_index != kIdentityLatticeIndex);
}

// Per-pass corner-path budget profiling (the M7 "profiled and reported"
// verification bullet) - hidden tag, never part of the normal suite:
// wall-clock numbers are machine- and build-dependent and belong in a
// report, not an assertion. Run:
//
//   cube_unit_tests.exe "[.corner-profile]"
//
// Synthetic frames on a desktop bound the RELATIVE cost of the hint (its
// absolute phone-class number needs a device this repo cannot drive - a
// user-dependent follow-up, like the recorded corpus). The five scenarios
// bracket what a live pass can spend: hint derivation alone, the historical
// unhinted detector on its prompted pose (hit) and off it (miss - the
// pre-M7 steady state while tracking), and the hinted detector both
// succeeding and failing (a stale/mismatched hint is the bounded worst
// case: the full candidate scan runs and every fit is attempted and
// rejected). The decimation decision recorded at cornerScanArmed
// (scan_session.cpp) cites these numbers.
TEST_CASE("profile the corner path's per-pass cost", "[.corner-profile]") {
    const cv::Matx33d rotated = rot_z_display(40.0) * iso_corner_rotation();
    const cv::Mat iso_frame = render_rotated_cube(iso_corner_rotation(), kHintTranslation, kHintFrame);
    const cv::Mat rotated_frame = render_rotated_cube(rotated, kHintTranslation, kHintFrame);

    const PoseSeed rotated_seed = seed_for_rotation(rotated, kHintTranslation);
    // A hint pointing 100 degrees away from what the frame shows: sectors
    // and bases land on the wrong candidates, so every per-face fit is
    // attempted and fails - the detector's bounded-work worst case.
    const PoseSeed mismatched_seed = seed_for_rotation(rot_z_display(140.0) * iso_corner_rotation(), kHintTranslation);

    const auto time_ms = [](int reps, const auto& body) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i) {
            body();
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
        return elapsed.count() / reps;
    };

    constexpr int kHintReps = 1000;
    constexpr int kDetectorReps = 25;
    const double hint_ms = time_ms(kHintReps, [&] { (void)compute_corner_hint(rotated_seed); });
    const double unhinted_hit_ms = time_ms(kDetectorReps, [&] { (void)observe_corner(iso_frame); });
    const double unhinted_miss_ms = time_ms(kDetectorReps, [&] { (void)observe_corner(rotated_frame); });
    const auto rotated_hint = compute_corner_hint(rotated_seed);
    REQUIRE(rotated_hint.has_value());
    const double hinted_hit_ms = time_ms(kDetectorReps, [&] { (void)observe_corner(rotated_frame, &*rotated_hint); });
    const auto mismatched_hint = compute_corner_hint(mismatched_seed);
    REQUIRE(mismatched_hint.has_value());
    const double hinted_miss_ms =
        time_ms(kDetectorReps, [&] { (void)observe_corner(rotated_frame, &*mismatched_hint); });

    std::printf("corner-profile (%dx%d synthetic frames)\n", kHintFrame.width, kHintFrame.height);
    std::printf("  compute_corner_hint      %9.4f ms\n", hint_ms);
    std::printf("  observe_corner unhinted  %9.4f ms hit   %9.4f ms miss\n", unhinted_hit_ms, unhinted_miss_ms);
    std::printf("  observe_corner hinted    %9.4f ms hit   %9.4f ms miss\n", hinted_hit_ms, hinted_miss_ms);
    SUCCEED();
}
