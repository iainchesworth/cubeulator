#include <catch2/catch_test_macros.hpp>

#include <cube/vision/edge_observation.hpp>
#include <cube/vision/orientation_lattice.hpp>
#include <cube/vision/orientation_tracker.hpp>
#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

using namespace cube::vision;

namespace {

constexpr int kFrame = 480;

// Renders a two-face edge view of the centred unit cube at display
// orientation `q`: model F's nine tiles (left of the shared vertical
// edge under the yaws these tests use) and model R's nine, projected
// through the SAME approximate camera model the pose path assumes, filled
// with saturated tile colours over a dark background. Tile half-extent is
// 80% of a half-cell, mirroring a real flush-tile cube's gap fraction.
cv::Mat render_edge_view(const Quat& q, double distance = 2.6) {
    cv::Mat frame(kFrame, kFrame, CV_8UC3, cv::Scalar(20, 20, 20));

    const cv::Matx33d rotation = opencv_rotation_from_display(q);
    cv::Vec3d rvec;
    cv::Rodrigues(rotation, rvec);
    const cv::Vec3d tvec(0.0, 0.0, distance);
    const cv::Matx33d camera = approximate_camera_matrix(frame.size());

    const std::array<cv::Scalar, 6> palette = {
        cv::Scalar(0, 0, 200),     // red
        cv::Scalar(60, 170, 60),   // green
        cv::Scalar(200, 80, 0),    // blue
        cv::Scalar(0, 120, 240),   // orange
        cv::Scalar(0, 200, 230),   // yellow
        cv::Scalar(180, 60, 180),  // magenta stand-in: any saturated tile works
    };

    constexpr double kHalfTile = 0.8 / 6.0;
    int tile_index = 0;
    const auto draw_face = [&](bool is_left_face) {
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col, ++tile_index) {
                const double u = -0.5 + (col + 0.5) / 3.0;
                const double v = -0.5 + (row + 0.5) / 3.0;
                std::vector<cv::Point3d> corners;
                for (const auto [du, dv] :
                     std::array<std::array<double, 2>, 4>{{{-kHalfTile, -kHalfTile},
                                                           {kHalfTile, -kHalfTile},
                                                           {kHalfTile, kHalfTile},
                                                           {-kHalfTile, kHalfTile}}}) {
                    if (is_left_face) {
                        corners.emplace_back(u + du, v + dv, -0.5);  // model F plane
                    } else {
                        corners.emplace_back(0.5, v + dv, u + du);  // model R plane
                    }
                }
                std::vector<cv::Point2d> projected;
                cv::projectPoints(corners, rvec, tvec, camera, cv::noArray(), projected);
                std::vector<cv::Point> poly;
                poly.reserve(projected.size());
                for (const auto& point : projected) {
                    poly.emplace_back(static_cast<int>(std::lround(point.x)),
                                      static_cast<int>(std::lround(point.y)));
                }
                cv::fillConvexPoly(frame, poly,
                                   palette[static_cast<std::size_t>(tile_index) % palette.size()],
                                   cv::LINE_8);
            }
        }
    };
    draw_face(true);
    draw_face(false);
    return frame;
}

// Display orientation of an edge view: yaw about +y (negative = F leans
// screen-left, the arrangement observe_edge's model expects under these
// renders), then roll about +z.
[[nodiscard]] Quat edge_pose(float yaw_deg, float roll_deg) {
    return quat_multiply(quat_from_rotvec_deg({0.0F, 0.0F, roll_deg}),
                         quat_from_rotvec_deg({0.0F, -yaw_deg, 0.0F}));
}

}  // namespace

TEST_CASE("observe_edge + project_cube_from_edge recover synthetic edge poses within 8 degrees",
          "[vision]") {
    for (const float yaw : {40.0F, 45.0F, 50.0F}) {
        for (const float roll : {-15.0F, 0.0F, 15.0F}) {
            CAPTURE(yaw, roll);
            const Quat q_true = edge_pose(yaw, roll);
            const cv::Mat frame = render_edge_view(q_true);

            detail::EdgeDiagnostics diag;
            const auto edge = detail::observe_edge_diagnosed(frame, nullptr, &diag);
            CAPTURE(diag.candidate_count, diag.splits_evaluated, diag.paired_fits,
                    diag.best_split_x, diag.best_left_inliers, diag.best_right_inliers,
                    diag.best_pitch_ratio, diag.best_shared_edge_px);
            REQUIRE(edge.has_value());
            REQUIRE(edge->geometry_confidence > 0.0F);

            const auto pose = project_cube_from_edge(*edge, frame.size());
            REQUIRE(pose.has_value());
            REQUIRE(pose->orientation_valid);
            // The render's left face IS model F, so the gauge is identity
            // and the estimate must land on the true pose directly.
            CHECK(quat_angle_deg(pose->orientation, q_true) < 8.0F);
        }
    }
}

TEST_CASE("a pose-derived edge hint reads the same synthetic views", "[vision]") {
    // While tracking, observe_edge is hinted from the predicted pose
    // instead of probing canonical bases; the hint must at minimum not
    // lose views the canonical probe finds (here: exact prediction).
    const Quat q_true = edge_pose(45.0F, 10.0F);
    const cv::Mat frame = render_edge_view(q_true);

    cv::Vec3d rvec;
    cv::Rodrigues(opencv_rotation_from_display(q_true), rvec);
    const PoseSeed seed{rvec, cv::Vec3d(0.0, 0.0, 2.6)};
    const auto hint = compute_edge_hint(seed);
    REQUIRE(hint.has_value());
    // Rows run down-screen and dominate the (more foreshortened) columns
    // after shorter-axis normalisation.
    for (std::size_t role = 0; role < 2; ++role) {
        CHECK(hint->row_dir[role].y > 0.0F);
        CHECK(std::hypot(hint->row_dir[role].x, hint->row_dir[role].y) >=
              std::hypot(hint->col_dir[role].x, hint->col_dir[role].y) - 1e-3F);
    }

    const auto edge = observe_edge(frame, &*hint);
    REQUIRE(edge.has_value());
    const auto pose = project_cube_from_edge(*edge, frame.size(), &seed);
    REQUIRE(pose.has_value());
    CHECK(quat_angle_deg(pose->orientation, q_true) < 8.0F);
}

TEST_CASE("compute_edge_hint refuses non-edge presentations", "[vision]") {
    const auto seed_for = [](const Quat& q) {
        cv::Vec3d rvec;
        cv::Rodrigues(opencv_rotation_from_display(q), rvec);
        return PoseSeed{rvec, cv::Vec3d(0.0, 0.0, 3.0)};
    };
    // Frontal: one face visible.
    CHECK_FALSE(compute_edge_hint(seed_for({1.0F, 0.0F, 0.0F, 0.0F})).has_value());
    // Corner presentation: three faces visible - compute_corner_hint's
    // territory, and the two hints must partition cleanly.
    const Quat corner = quat_multiply(quat_from_rotvec_deg({-30.0F, 0.0F, 0.0F}),
                                      quat_from_rotvec_deg({0.0F, -45.0F, 0.0F}));
    CHECK_FALSE(compute_edge_hint(seed_for(corner)).has_value());
    CHECK(compute_corner_hint(seed_for(corner)).has_value());
    // Horizontal shared edge (pitch instead of yaw): two faces visible but
    // not the vertical-edge presentation this detector models.
    CHECK_FALSE(
        compute_edge_hint(seed_for(quat_from_rotvec_deg({-45.0F, 0.0F, 0.0F}))).has_value());
}

TEST_CASE("observe_edge rejects frames without a two-face edge view", "[vision]") {
    // Blank and near-blank frames: the candidate floor.
    CHECK_FALSE(observe_edge(cv::Mat()).has_value());
    CHECK_FALSE(observe_edge(cv::Mat::zeros(kFrame, kFrame, CV_8UC3)).has_value());

    // A flat frontal 3x3 face: the classic false-positive risk (split the
    // one grid in two and call it an edge). The split sides' 2-3 column
    // subsets centre their anchors BETWEEN columns, so the per-side fits
    // die on the provisional inlier floor - and must keep dying.
    cv::Mat flat(kFrame, kFrame, CV_8UC3, cv::Scalar(20, 20, 20));
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            cv::rectangle(flat, cv::Rect(150 + col * 65, 150 + row * 65, 55, 55),
                          cv::Scalar(0, 60 + 60 * col, 200 - 60 * row), cv::FILLED);
        }
    }
    CHECK_FALSE(observe_edge(flat).has_value());

    // Deterministic scattered clutter: saturated blobs with no shared-edge
    // structure must fail the pairing gates even when both sides can be
    // fitted SOMETHING.
    std::mt19937 rng(20260720);
    std::uniform_int_distribution<int> position(20, kFrame - 40);
    std::uniform_int_distribution<int> size(12, 24);
    cv::Mat clutter(kFrame, kFrame, CV_8UC3, cv::Scalar(25, 25, 25));
    for (int blob = 0; blob < 24; ++blob) {
        const int side = size(rng);
        cv::rectangle(clutter, cv::Rect(position(rng), position(rng), side, side),
                      cv::Scalar(40 + 8 * blob, 80, 220 - 7 * blob), cv::FILLED);
    }
    CHECK_FALSE(observe_edge(clutter).has_value());
}
