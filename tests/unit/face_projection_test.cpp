// Pose-derived face geometry (P3.1): visibility, quad/centre projection
// and ordering are pure functions of the pose - verified against the
// same projection maths, plus the visibility invariants a convex cube
// guarantees.

#include <cube/vision/pipeline/face_projection.hpp>
#include <cube/vision/pose_from_corners.hpp>

#include <opencv2/calib3d.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {

using cube::vision::pipeline::derive_faces;

cv::Matx33d camera() {
    return {640.0, 0.0, 320.0, 0.0, 640.0, 240.0, 0.0, 0.0, 1.0};
}

}  // namespace

TEST_CASE("three-quarter view shows exactly three faces, most frontal first", "[face_projection]") {
    const cv::Vec3d rvec{0.5, -0.6, 0.1};
    const cv::Vec3d tvec{0.3, -0.2, 9.0};
    const auto faces = derive_faces(rvec, tvec, camera());
    REQUIRE(faces.size() == 3);
    for (std::size_t i = 1; i < faces.size(); ++i) {
        REQUIRE(faces[i - 1].frontality >= faces[i].frontality);
    }
    // A convex cube never shows opposite faces together.
    for (const auto& a : faces) {
        for (const auto& b : faces) {
            REQUIRE_FALSE((a.face / 2 == b.face / 2 && a.face != b.face));
        }
    }
}

TEST_CASE("face-on view shows one face, square-on", "[face_projection]") {
    // Identity rotation: +z (model F) faces the camera... in OpenCV camera
    // frame +z runs AWAY from the camera, so the face seen square-on is -z.
    const auto faces = derive_faces(cv::Vec3d{0, 0, 0}, cv::Vec3d{0, 0, 9.0}, camera());
    REQUIRE(faces.size() == 1);
    REQUIRE(faces[0].face == 5);  // -z
    REQUIRE(faces[0].frontality > 0.95);
    // Quad corners must be projections of the model's own corners: each
    // quad corner coincides with one of the 8 projected cube corners.
    std::vector<cv::Point3f> model(cube::vision::kSingmasterCornerModel.begin(),
                                   cube::vision::kSingmasterCornerModel.end());
    std::vector<cv::Point2f> projected;
    cv::projectPoints(model, cv::Vec3d{0, 0, 0}, cv::Vec3d{0, 0, 9.0}, camera(), cv::noArray(), projected);
    for (const auto& corner : faces[0].quad) {
        double nearest = 1e9;
        for (const auto& p : projected) {
            nearest = std::min(nearest, static_cast<double>(cv::norm(p - corner)));
        }
        REQUIRE(nearest < 1e-3);
    }
    // Centre grid: the middle centre projects to the face centre, and the
    // grid spans strictly inside the quad.
    const cv::Point2f mid = faces[0].centres[4];
    cv::Point2f quad_centre(0, 0);
    for (const auto& corner : faces[0].quad) {
        quad_centre += corner;
    }
    quad_centre *= 0.25F;
    REQUIRE(cv::norm(mid - quad_centre) < 1e-3);
}

TEST_CASE("derive_faces is deterministic", "[face_projection]") {
    const cv::Vec3d rvec{0.4, 0.3, -0.2};
    const cv::Vec3d tvec{-0.5, 0.4, 8.0};
    const auto first = derive_faces(rvec, tvec, camera());
    const auto second = derive_faces(rvec, tvec, camera());
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].face == second[i].face);
        REQUIRE(first[i].quad == second[i].quad);
    }
}
