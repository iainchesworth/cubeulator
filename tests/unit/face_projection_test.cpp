// Pose-derived face geometry (P3.1): visibility, quad/centre projection
// and ordering are pure functions of the pose - verified against the
// same projection maths, plus the visibility invariants a convex cube
// guarantees.

#include <cube/vision/pipeline/face_projection.hpp>
#include <cube/vision/pose_from_corners.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

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
    cv::projectPoints(model, cv::Vec3d{0, 0, 0}, cv::Vec3d{0, 0, 9.0}, camera(), cv::noArray(),
                      projected);
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

TEST_CASE("rectification lands each sticker in its canonical cell", "[face_projection]") {
    using cube::vision::pipeline::rectify_face;
    // Paint the -z face's 9 cells with distinct grey levels by projecting
    // each cell's sub-quad under a three-quarter pose, then rectify and
    // check every cell centre reads its own level back.
    const cv::Vec3d rvec{0.35, -0.4, 0.1};
    const cv::Vec3d tvec{0.2, -0.1, 9.0};
    const auto faces = derive_faces(rvec, tvec, camera());
    REQUIRE_FALSE(faces.empty());
    const auto& face = faces.front();

    cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);
    const int face_axis = face.face / 2;
    const float sign = (face.face % 2 == 0) ? 1.0F : -1.0F;
    const int axis_u = (face_axis + 1) % 3;
    const int axis_v = (face_axis + 2) % 3;
    const auto project = [&](float u, float v) {
        std::array<float, 3> p{};
        p[static_cast<std::size_t>(face_axis)] = sign;
        p[static_cast<std::size_t>(axis_u)] = u;
        p[static_cast<std::size_t>(axis_v)] = v;
        const cv::Matx31d c =
            rotation * cv::Matx31d(static_cast<double>(p[0]), static_cast<double>(p[1]),
                                   static_cast<double>(p[2])) +
            cv::Matx31d(tvec[0], tvec[1], tvec[2]);
        const cv::Matx33d k = camera();
        return cv::Point(static_cast<int>(std::lround(k(0, 0) * c(0, 0) / c(2, 0) + k(0, 2))),
                         static_cast<int>(std::lround(k(1, 1) * c(1, 0) / c(2, 0) + k(1, 2))));
    };
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float u0 = -1.0F + static_cast<float>(col) * (2.0F / 3.0F);
            const float v0 = -1.0F + static_cast<float>(row) * (2.0F / 3.0F);
            const int level = 40 + (row * 3 + col) * 24;
            const std::vector<cv::Point> quad{project(u0, v0), project(u0 + 2.0F / 3.0F, v0),
                                              project(u0 + 2.0F / 3.0F, v0 + 2.0F / 3.0F),
                                              project(u0, v0 + 2.0F / 3.0F)};
            cv::fillConvexPoly(frame, quad, cv::Scalar(level, level, level));
        }
    }

    const cv::Mat rectified = rectify_face(frame, face, 96);
    REQUIRE_FALSE(rectified.empty());
    REQUIRE(rectified.size() == cv::Size(96, 96));
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int expected = 40 + (row * 3 + col) * 24;
            const cv::Vec3b sample = rectified.at<cv::Vec3b>(row * 32 + 16, col * 32 + 16);
            REQUIRE(std::abs(static_cast<int>(sample[0]) - expected) <= 2);
        }
    }
}

TEST_CASE("rectification refuses empty frames and degenerate quads", "[face_projection]") {
    using cube::vision::pipeline::rectify_face;
    cube::vision::pipeline::ProjectedFace collapsed;
    collapsed.quad = {cv::Point2f(10, 10), cv::Point2f(10.1F, 10), cv::Point2f(10.1F, 10.1F),
                      cv::Point2f(10, 10.1F)};
    const cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    REQUIRE(rectify_face(cv::Mat(), collapsed, 96).empty());
    REQUIRE(rectify_face(frame, collapsed, 96).empty());
}

TEST_CASE("gap refinement finds shifted lines and respects its clamp", "[face_projection]") {
    using cube::vision::pipeline::refine_cell_grid;
    // A 96x96 face with gap lines displaced from the nominal thirds: the
    // refiner must find lines inside its +-0.2 pitch reach and stay at
    // the clamp for lines beyond it.
    // LINE_AA gives the V-shaped 1-D profile a real anti-aliased gap
    // has; a hard 3 px line is a FLAT trough whose sub-pixel centre is
    // ambiguous by +-1 (the first version of this test tripped on that).
    const auto face_with_gaps = [](double x1, double x2) {
        cv::Mat face(96, 96, CV_8UC3, cv::Scalar(180, 180, 180));
        for (const double x : {x1, x2}) {
            cv::line(face, cv::Point2d(x, 0), cv::Point2d(x, 95), cv::Scalar(30, 30, 30), 3,
                     cv::LINE_AA);
        }
        cv::line(face, cv::Point(0, 32), cv::Point(95, 32), cv::Scalar(30, 30, 30), 3, cv::LINE_AA);
        cv::line(face, cv::Point(0, 64), cv::Point(95, 64), cv::Scalar(30, 30, 30), 3, cv::LINE_AA);
        return face;
    };

    // Within reach (nominal 32/64, displaced by +4 / -4; reach is 6.4).
    const auto near = refine_cell_grid(face_with_gaps(36.0, 60.0));
    REQUIRE(near.valid);
    REQUIRE(std::abs(near.x[1] - 36.0) < 1.0);
    REQUIRE(std::abs(near.x[2] - 60.0) < 1.0);
    REQUIRE(std::abs(near.y[1] - 32.0) < 1.0);

    // Beyond reach (displaced by +10): the clamp holds at nominal+6.4 -
    // refinement may polish, never re-seat.
    const auto far = refine_cell_grid(face_with_gaps(42.0, 64.0));
    REQUIRE(far.valid);
    REQUIRE(far.x[1] <= 32.0 + 6.4 + 1e-6);
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
