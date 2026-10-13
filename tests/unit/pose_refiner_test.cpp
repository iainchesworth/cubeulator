// Edge/lattice pose refinement (P2.8): the refiner must RECOVER a small
// pose error against a rendered cube, REFUSE the one-cell/quarter-turn
// aliasing basin, and stay deterministic. The scene is rendered with the
// same projection the refiner measures against, so ground truth is exact.

#include <cube/vision/pipeline/pose_refiner.hpp>
#include <cube/vision/pose_from_corners.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

namespace {

using cube::vision::kSingmasterCornerModel;
using cube::vision::pipeline::PoseRefineParams;
using cube::vision::pipeline::refine_pose;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

cv::Matx33d camera_matrix() {
    return {static_cast<double>(kWidth),
            0.0,
            kWidth / 2.0,  //
            0.0,
            static_cast<double>(kWidth),
            kHeight / 2.0,  //
            0.0,
            0.0,
            1.0};
}

// Render the cube at a pose: light-grey faces (distinct shades so ridge
// edges carry gradient), dark sticker-gap lattice lines, dark
// background - the geometry the refiner's measurement model assumes,
// drawn with the same projection it uses.
cv::Mat render_cube(const cv::Vec3d& rvec, const cv::Vec3d& tvec) {
    cv::Mat frame(kHeight, kWidth, CV_8UC1, cv::Scalar(40));
    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);
    const auto project = [&](const cv::Point3f& p) {
        const cv::Matx31d c =
            rotation * cv::Matx31d(p.x, p.y, p.z) + cv::Matx31d(tvec[0], tvec[1], tvec[2]);
        const cv::Matx33d k = camera_matrix();
        return cv::Point(static_cast<int>(std::lround(k(0, 0) * c(0, 0) / c(2, 0) + k(0, 2))),
                         static_cast<int>(std::lround(k(1, 1) * c(1, 0) / c(2, 0) + k(1, 2))));
    };

    // Faces ordered +x,-x,+y,-y,+z,-z; corner quads in each face plane.
    const std::array<std::array<cv::Point3f, 4>, 6> faces{{
        {{{1, 1, 1}, {1, 1, -1}, {1, -1, -1}, {1, -1, 1}}},
        {{{-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}}},
        {{{1, 1, 1}, {-1, 1, 1}, {-1, 1, -1}, {1, 1, -1}}},
        {{{1, -1, 1}, {-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}}},
        {{{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}}},
        {{{1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}, {1, -1, -1}}},
    }};
    const std::array<int, 6> shades{200, 200, 170, 170, 230, 230};
    for (int face = 0; face < 6; ++face) {
        // Painter's visibility: draw only front-facing faces.
        const float sign = (face % 2 == 0) ? 1.0F : -1.0F;
        cv::Point3f normal(0, 0, 0);
        cv::Point3f centre(0, 0, 0);
        if (face / 2 == 0) {
            normal.x = sign;
        } else if (face / 2 == 1) {
            normal.y = sign;
        } else {
            normal.z = sign;
        }
        centre = normal;
        const cv::Matx31d normal_c = rotation * cv::Matx31d(normal.x, normal.y, normal.z);
        const cv::Matx31d centre_c = rotation * cv::Matx31d(centre.x, centre.y, centre.z) +
                                     cv::Matx31d(tvec[0], tvec[1], tvec[2]);
        if (normal_c(0, 0) * centre_c(0, 0) + normal_c(1, 0) * centre_c(1, 0) +
                normal_c(2, 0) * centre_c(2, 0) >=
            0.0) {
            continue;
        }
        std::vector<cv::Point> quad;
        for (const auto& corner : faces[static_cast<std::size_t>(face)]) {
            quad.push_back(project(corner));
        }
        cv::fillConvexPoly(frame, quad, cv::Scalar(shades[static_cast<std::size_t>(face)]),
                           cv::LINE_AA);
        // Sticker-gap lattice: the four internal lines of the face.
        const int face_axis = face / 2;
        const int axis_u = (face_axis + 1) % 3;
        const int axis_v = (face_axis + 2) % 3;
        for (const int fixed_axis : {axis_u, axis_v}) {
            const int run_axis = fixed_axis == axis_u ? axis_v : axis_u;
            for (const float fixed : {1.0F / 3.0F, -1.0F / 3.0F}) {
                std::array<float, 3> lo{};
                lo[static_cast<std::size_t>(face_axis)] = sign;
                lo[static_cast<std::size_t>(fixed_axis)] = fixed;
                lo[static_cast<std::size_t>(run_axis)] = -1.0F;
                std::array<float, 3> hi = lo;
                hi[static_cast<std::size_t>(run_axis)] = 1.0F;
                cv::line(frame, project({lo[0], lo[1], lo[2]}), project({hi[0], hi[1], hi[2]}),
                         cv::Scalar(60), 3, cv::LINE_AA);
            }
        }
    }
    return frame;
}

// Mean projected-corner distance between two poses - the "how wrong is
// this pose" yardstick for every assertion below.
double corner_error_px(const cv::Vec3d& rvec_a, const cv::Vec3d& tvec_a, const cv::Vec3d& rvec_b,
                       const cv::Vec3d& tvec_b) {
    std::vector<cv::Point3f> model(kSingmasterCornerModel.begin(), kSingmasterCornerModel.end());
    std::vector<cv::Point2f> projected_a;
    std::vector<cv::Point2f> projected_b;
    cv::projectPoints(model, rvec_a, tvec_a, camera_matrix(), cv::noArray(), projected_a);
    cv::projectPoints(model, rvec_b, tvec_b, camera_matrix(), cv::noArray(), projected_b);
    double total = 0.0;
    for (std::size_t i = 0; i < projected_a.size(); ++i) {
        total += cv::norm(projected_a[i] - projected_b[i]);
    }
    return total / static_cast<double>(projected_a.size());
}

// A three-quarter view: three faces visible, all edge families
// informative.
const cv::Vec3d kTrueRvec{0.5, -0.6, 0.1};
const cv::Vec3d kTrueTvec{0.3, -0.2, 9.0};

}  // namespace

TEST_CASE("refiner recovers a small pose offset", "[pose_refiner]") {
    const cv::Mat frame = render_cube(kTrueRvec, kTrueTvec);
    const cv::Vec3d off_rvec = kTrueRvec + cv::Vec3d(0.02, -0.015, 0.01);  // ~1.6 degrees
    const cv::Vec3d off_tvec = kTrueTvec + cv::Vec3d(0.04, 0.03, 0.08);

    const double error_before = corner_error_px(off_rvec, off_tvec, kTrueRvec, kTrueTvec);
    const auto refined = refine_pose(frame, off_rvec, off_tvec, camera_matrix());
    REQUIRE(refined.applied);
    const double error_after = corner_error_px(refined.rvec, refined.tvec, kTrueRvec, kTrueTvec);
    // Recovery, not perfection: one pass must claw back most of the
    // offset (sub-pixel convergence is the tracker loop's job, which
    // runs this every frame).
    REQUIRE(error_after < 0.5 * error_before);
    REQUIRE(refined.matches >= 12);
}

TEST_CASE("refiner is a no-op at the true pose", "[pose_refiner]") {
    const cv::Mat frame = render_cube(kTrueRvec, kTrueTvec);
    const auto refined = refine_pose(frame, kTrueRvec, kTrueTvec, camera_matrix());
    if (refined.applied) {
        const double moved = corner_error_px(refined.rvec, refined.tvec, kTrueRvec, kTrueTvec);
        REQUIRE(moved < 1.5);  // px - anti-aliased rendering leaves sub-pixel wiggle
    }
}

TEST_CASE("refiner refuses the one-cell aliasing basin", "[pose_refiner]") {
    const cv::Mat frame = render_cube(kTrueRvec, kTrueTvec);
    // Offset by a whole sticker pitch along the cube's own x axis: the
    // lattice of the WRONG cell alignment now sits where the true
    // lattice was. The sub-cell search window must keep those matches
    // out of reach - the refiner either fails to apply or, at most,
    // moves within its caps; it must NOT lock onto the aliased grid.
    cv::Matx33d rotation;
    cv::Rodrigues(kTrueRvec, rotation);
    const cv::Matx31d shift = rotation * cv::Matx31d(2.0 / 3.0, 0.0, 0.0);
    const cv::Vec3d off_tvec = kTrueTvec + cv::Vec3d(shift(0, 0), shift(1, 0), shift(2, 0));

    const auto refined = refine_pose(frame, kTrueRvec, off_tvec, camera_matrix());
    if (refined.applied) {
        // If anything was accepted, it must not have snapped the pose
        // onto the aliased alignment: distance to the TRUE pose must
        // still be dominated by the injected whole-cell offset.
        const double to_truth = corner_error_px(refined.rvec, refined.tvec, kTrueRvec, kTrueTvec);
        const double injected = corner_error_px(kTrueRvec, off_tvec, kTrueRvec, kTrueTvec);
        REQUIRE(to_truth > 0.6 * injected);
    }
}

TEST_CASE("refiner is deterministic", "[pose_refiner]") {
    const cv::Mat frame = render_cube(kTrueRvec, kTrueTvec);
    const cv::Vec3d off_rvec = kTrueRvec + cv::Vec3d(0.015, 0.01, -0.02);
    const cv::Vec3d off_tvec = kTrueTvec + cv::Vec3d(-0.03, 0.02, 0.05);
    const auto first = refine_pose(frame, off_rvec, off_tvec, camera_matrix());
    const auto second = refine_pose(frame, off_rvec, off_tvec, camera_matrix());
    REQUIRE(first.applied == second.applied);
    REQUIRE(first.matches == second.matches);
    REQUIRE(first.rvec == second.rvec);
    REQUIRE(first.tvec == second.tvec);
}

TEST_CASE("refiner rejects an empty frame", "[pose_refiner]") {
    const auto refined = refine_pose(cv::Mat(), kTrueRvec, kTrueTvec, camera_matrix());
    REQUIRE_FALSE(refined.applied);
    REQUIRE(refined.reject_reason != nullptr);
}
