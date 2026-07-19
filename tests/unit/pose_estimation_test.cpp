#include <catch2/catch_test_macros.hpp>

#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using namespace cube::vision;

namespace {

// --- independent pinhole renderer for the face path -------------------------
//
// Mirrors the camera model pose_estimation.cpp documents (fx = fy = frame
// width, principal point at the frame centre, no distortion) but is written
// from scratch here so the test checks the implementation against the
// contract rather than against itself.

const cv::Size kFrame(640, 480);

// The same unit-cube model the implementation poses: near face at z = 0
// (tl, tr, br, bl with y down), rear face directly behind at z = +1.
const std::array<cv::Point3d, 8> kCube = {{
    {-0.5, -0.5, 0.0},
    {0.5, -0.5, 0.0},
    {0.5, 0.5, 0.0},
    {-0.5, 0.5, 0.0},
    {-0.5, -0.5, 1.0},
    {0.5, -0.5, 1.0},
    {0.5, 0.5, 1.0},
    {-0.5, 0.5, 1.0},
}};

// Rotation about the camera x-axis (tilting the face's bottom edge away
// from the camera for positive angles), translation, then pinhole project.
cv::Point2f render_point(const cv::Point3d& point, double tilt_radians, const cv::Vec3d& translation) {
    const double cos_t = std::cos(tilt_radians);
    const double sin_t = std::sin(tilt_radians);
    const double x = point.x + translation[0];
    const double y = cos_t * point.y - sin_t * point.z + translation[1];
    const double z = sin_t * point.y + cos_t * point.z + translation[2];
    const double focal = static_cast<double>(kFrame.width);
    return {static_cast<float>(focal * x / z + 0.5 * kFrame.width),
            static_cast<float>(focal * y / z + 0.5 * kFrame.height)};
}

FaceObservation observation_with_quad(const std::array<cv::Point2f, 4>& quad) {
    FaceObservation observation;
    observation.face_quad = quad;
    observation.geometry_confidence = 1.0F;
    return observation;
}

FaceObservation rendered_face(double tilt_radians, const cv::Vec3d& translation) {
    std::array<cv::Point2f, 4> quad{};
    for (std::size_t i = 0; i < 4; ++i) {
        quad[i] = render_point(kCube[i], tilt_radians, translation);
    }
    return observation_with_quad(quad);
}

float quad_area(const cv::Point2f* quad) {
    float twice_area = 0.0F;
    for (std::size_t i = 0; i < 4; ++i) {
        const cv::Point2f& a = quad[i];
        const cv::Point2f& b = quad[(i + 1) % 4];
        twice_area += a.x * b.y - b.x * a.y;
    }
    return 0.5F * std::abs(twice_area);
}

bool inside_frame(cv::Point2f point, cv::Size frame) {
    return point.x >= 0.0F && point.y >= 0.0F && point.x <= static_cast<float>(frame.width) &&
           point.y <= static_cast<float>(frame.height);
}

float distance(cv::Point2f a, cv::Point2f b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// --- orientation helpers ----------------------------------------------------
//
// The orientation contract is checked by ROTATING BASIS VECTORS, never by
// comparing raw quaternion components: q and -q are the same rotation
// (double cover), and componentwise checks would also silently bake in an
// axis-convention assumption the header deliberately owns instead.

// v' = v + 2u x (u x v + w v), the standard quaternion sandwich for a unit
// quaternion {w, u}.
cv::Vec3d rotate_by_quaternion(const std::array<float, 4>& q, const cv::Vec3d& v) {
    const cv::Vec3d u(static_cast<double>(q[1]), static_cast<double>(q[2]), static_cast<double>(q[3]));
    const double w = static_cast<double>(q[0]);
    return v + 2.0 * u.cross(u.cross(v) + w * v);
}

double angle_between(const cv::Vec3d& a, const cv::Vec3d& b) {
    const double denom = cv::norm(a) * cv::norm(b);
    const double cosine = std::clamp(a.dot(b) / denom, -1.0, 1.0);
    return std::acos(cosine);
}

// Total rotation angle encoded by a unit quaternion, sign-insensitive.
double rotation_angle(const std::array<float, 4>& q) {
    return 2.0 * std::acos(std::clamp(std::abs(static_cast<double>(q[0])), 0.0, 1.0));
}

constexpr double kDegree = std::numbers::pi / 180.0;

// A face render as render_point, but rotated about the camera's y axis
// (yawing the face's right edge away from the camera for positive angles
// in OpenCV's frame).
FaceObservation rendered_face_yawed(double yaw_radians, const cv::Vec3d& translation) {
    const double cos_y = std::cos(yaw_radians);
    const double sin_y = std::sin(yaw_radians);
    std::array<cv::Point2f, 4> quad{};
    for (std::size_t i = 0; i < 4; ++i) {
        const cv::Point3d& point = kCube[i];
        const double x = cos_y * point.x + sin_y * point.z + translation[0];
        const double y = point.y + translation[1];
        const double z = -sin_y * point.x + cos_y * point.z + translation[2];
        const double focal = static_cast<double>(kFrame.width);
        quad[i] = {static_cast<float>(focal * x / z + 0.5 * kFrame.width),
                   static_cast<float>(focal * y / z + 0.5 * kFrame.height)};
    }
    return observation_with_quad(quad);
}

}  // namespace

TEST_CASE("project_cube_from_face recovers a tilted face and a plausible rear", "[vision]") {
    // ~20 degree tilt, cube three units out: projects at ~210px, matching
    // the real capture scale.
    const double tilt = 20.0 * std::numbers::pi / 180.0;
    const cv::Vec3d translation(0.0, 0.0, 3.0);
    const FaceObservation observation = rendered_face(tilt, translation);

    const auto projection = project_cube_from_face(observation, kFrame);
    REQUIRE(projection.has_value());

    // Near face: the pose was recovered from exact synthetic data, so the
    // reprojected near corners must land back on the input quad.
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(distance(projection->vertices[i], observation.face_quad[i]) < 3.0F);
    }
    // Rear face: sanity only (the planar pose leaves the rear less tightly
    // determined). It must be drawable and, being farther from the camera
    // than a face tilted away from it, project smaller than the near face.
    for (std::size_t i = 4; i < 8; ++i) {
        REQUIRE(inside_frame(projection->vertices[i], kFrame));
    }
    REQUIRE(quad_area(projection->vertices.data() + 4) < quad_area(projection->vertices.data()));
}

TEST_CASE("project_cube_from_face projects a frontal cube's rear inside the near face", "[vision]") {
    const FaceObservation observation = rendered_face(0.0, cv::Vec3d(0.0, 0.0, 2.5));

    const auto projection = project_cube_from_face(observation, kFrame);
    REQUIRE(projection.has_value());

    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(distance(projection->vertices[i], observation.face_quad[i]) < 3.0F);
    }

    // Seen dead-on and centred, every rear corner recedes toward the
    // principal point, so the rear square sits strictly inside the near
    // square's bounds.
    float min_x = static_cast<float>(kFrame.width);
    float max_x = 0.0F;
    float min_y = static_cast<float>(kFrame.height);
    float max_y = 0.0F;
    for (std::size_t i = 0; i < 4; ++i) {
        min_x = std::min(min_x, projection->vertices[i].x);
        max_x = std::max(max_x, projection->vertices[i].x);
        min_y = std::min(min_y, projection->vertices[i].y);
        max_y = std::max(max_y, projection->vertices[i].y);
    }
    for (std::size_t i = 4; i < 8; ++i) {
        REQUIRE(projection->vertices[i].x > min_x);
        REQUIRE(projection->vertices[i].x < max_x);
        REQUIRE(projection->vertices[i].y > min_y);
        REQUIRE(projection->vertices[i].y < max_y);
    }
}

TEST_CASE("project_cube_from_corner pins seven corners and completes the eighth", "[vision]") {
    // A generic (deliberately non-isometric) parallelepiped projection in
    // the style of corner_observation_test.cpp's renderer: shared near
    // corner S, screen edge vectors A (top face's down-right columns),
    // B (top face's down-left rows) and D (the side faces' vertical drop).
    // The lower-right face's columns run along the cube edge shared with
    // the top face's rows, i.e. -B on screen.
    const cv::Point2f s(200.0F, 200.0F);
    const cv::Point2f a(110.0F, 55.0F);
    const cv::Point2f b(-95.0F, 65.0F);
    const cv::Point2f d(8.0F, 125.0F);
    const cv::Point2f jitter(2.0F, 0.0F);

    CornerObservation observation;
    observation.top.quad = {s - a - b, s - b + jitter, s, s - a};
    observation.lower_left.quad = {s - a, s, s + d, s - a + d};
    observation.lower_right.quad = {s, s - b - jitter, s - b + d, s + d};
    observation.geometry_confidence = 1.0F;

    const auto projection = project_cube_from_corner(observation, cv::Size(400, 400));
    REQUIRE(projection.has_value());

    // The seven directly-pinned vertices, with the UBR estimate perturbed
    // symmetrically (+/- jitter) in the two quads that see it so the test
    // proves coincident estimates are averaged, not just copied.
    const std::array<cv::Point2f, 8> expected = {
        s - a - b,      // 0 UBL
        s - b,          // 1 UBR (midpoint of the jittered pair)
        s,              // 2 URF, the shared near corner
        s - a,          // 3 UFL
        s - a - b + d,  // 4 DLB, hidden: bottom-face parallelogram completion
        s - b + d,      // 5 DRB
        s + d,          // 6 DRF
        s - a + d,      // 7 DLF
    };
    for (std::size_t i = 0; i < 8; ++i) {
        REQUIRE(distance(projection->vertices[i], expected[i]) < 0.01F);
    }

    // Orientation contract for the corner path: this synthetic view is an
    // AFFINE construction, not a true perspective projection, so whether
    // the PnP re-derivation accepts it is not pinned down here - only the
    // documented either/or is: a valid orientation is a unit quaternion,
    // an invalid one is exactly identity (the UI's idle-rotation fallback).
    const auto& q = projection->orientation;
    const double norm = std::sqrt(static_cast<double>(q[0]) * q[0] + static_cast<double>(q[1]) * q[1] +
                                  static_cast<double>(q[2]) * q[2] + static_cast<double>(q[3]) * q[3]);
    if (projection->orientation_valid) {
        REQUIRE(std::abs(norm - 1.0) < 1e-3);
    } else {
        REQUIRE(q[0] == 1.0F);
        REQUIRE(q[1] == 0.0F);
        REQUIRE(q[2] == 0.0F);
        REQUIRE(q[3] == 0.0F);
    }
}

TEST_CASE("project_cube_from_face reports a near-identity orientation for a frontal face", "[vision]") {
    const auto projection = project_cube_from_face(rendered_face(0.0, cv::Vec3d(0.0, 0.0, 2.5)), kFrame);
    REQUIRE(projection.has_value());
    REQUIRE(projection->orientation_valid);

    // A flat-on, unrotated face is the canonical pose by definition, so the
    // quaternion's total rotation angle must be (numerically) nil.
    REQUIRE(rotation_angle(projection->orientation) < 5.0 * kDegree);
}

TEST_CASE("project_cube_from_face recovers a rotation about the vertical axis", "[vision]") {
    // 25 degrees of yaw: enough that IPPE can't confuse it with frontal,
    // shallow enough that the quad stays comfortably non-degenerate.
    const double yaw = 25.0 * kDegree;
    const auto projection =
        project_cube_from_face(rendered_face_yawed(yaw, cv::Vec3d(0.0, 0.0, 3.0)), kFrame);
    REQUIRE(projection.has_value());
    REQUIRE(projection->orientation_valid);
    const auto& q = projection->orientation;

    // A rotation about the vertical axis leaves the display-frame y basis
    // vector fixed...
    REQUIRE(angle_between(rotate_by_quaternion(q, {0.0, 1.0, 0.0}), {0.0, 1.0, 0.0}) < 3.0 * kDegree);
    // ...and swings the x basis vector by exactly the yaw angle. Both
    // checks are agnostic to the yaw's sign convention and to which way
    // the y axis points, per the helper comment.
    const double swung = angle_between(rotate_by_quaternion(q, {1.0, 0.0, 0.0}), {1.0, 0.0, 0.0});
    REQUIRE(std::abs(swung - yaw) < 3.0 * kDegree);
    // The total rotation is the yaw and nothing else - catches a solution
    // that adds a spurious roll/tilt component yet keeps x's swing right.
    REQUIRE(std::abs(rotation_angle(q) - yaw) < 3.0 * kDegree);
}

TEST_CASE("project_cube_from_corner recovers orientation from a true perspective corner view", "[vision]") {
    // A cube genuinely rotated and pinhole-projected (same camera model as
    // the implementation: f = width, principal point centred), so an exact
    // pose exists and the PnP re-derivation must find it. Display-frame
    // rotation: 35 degrees of pitch about x after -40 degrees of yaw about
    // y - a generic top-front-right corner presentation.
    const double yaw = -40.0 * kDegree;
    const double pitch = 35.0 * kDegree;
    const cv::Matx33d rot_y(std::cos(yaw), 0.0, std::sin(yaw), 0.0, 1.0, 0.0, -std::sin(yaw), 0.0,
                            std::cos(yaw));
    const cv::Matx33d rot_x(1.0, 0.0, 0.0, 0.0, std::cos(pitch), -std::sin(pitch), 0.0, std::sin(pitch),
                            std::cos(pitch));
    const cv::Matx33d display_rotation = rot_x * rot_y;

    // Display frame (y up, z toward viewer) to OpenCV camera frame (y
    // down, z into screen): conjugate by the y/z flip, mirroring the
    // reconciliation the header documents.
    const cv::Matx33d flip(1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0);
    const cv::Matx33d camera_rotation = flip * display_rotation * flip;

    // The seven detector-pinned corners in display coordinates (unit cube
    // centred at the origin: U = +y, F = +z, R = +x).
    struct NamedCorner {
        cv::Point3d display;
        cv::Point2f projected;
    };
    std::array<NamedCorner, 7> corners = {{
        {{-0.5, 0.5, -0.5}, {}},  // UBL
        {{0.5, 0.5, -0.5}, {}},   // UBR
        {{0.5, 0.5, 0.5}, {}},    // URF
        {{-0.5, 0.5, 0.5}, {}},   // UFL
        {{0.5, -0.5, -0.5}, {}},  // DRB
        {{0.5, -0.5, 0.5}, {}},   // DRF
        {{-0.5, -0.5, 0.5}, {}},  // DLF
    }};
    const cv::Vec3d translation(0.0, 0.0, 4.0);
    for (auto& corner : corners) {
        const cv::Vec3d model_cv = flip * cv::Vec3d(corner.display.x, corner.display.y, corner.display.z);
        const cv::Vec3d in_camera = camera_rotation * model_cv + translation;
        const double focal = static_cast<double>(kFrame.width);
        corner.projected = {static_cast<float>(focal * in_camera[0] / in_camera[2] + 0.5 * kFrame.width),
                            static_cast<float>(focal * in_camera[1] / in_camera[2] + 0.5 * kFrame.height)};
    }
    const cv::Point2f ubl = corners[0].projected;
    const cv::Point2f ubr = corners[1].projected;
    const cv::Point2f urf = corners[2].projected;
    const cv::Point2f ufl = corners[3].projected;
    const cv::Point2f drb = corners[4].projected;
    const cv::Point2f drf = corners[5].projected;
    const cv::Point2f dlf = corners[6].projected;

    // Quad slot layout per pose_estimation.cpp's documented coincidences.
    CornerObservation observation;
    observation.top.quad = {ubl, ubr, urf, ufl};
    observation.lower_left.quad = {ufl, urf, drf, dlf};
    observation.lower_right.quad = {urf, ubr, drb, drf};
    observation.geometry_confidence = 1.0F;

    const auto projection = project_cube_from_corner(observation, kFrame);
    REQUIRE(projection.has_value());
    REQUIRE(projection->orientation_valid);

    // The quaternion must act on the display basis exactly as the known
    // rotation does - basis-vector comparison, per the helper comment.
    const std::array<cv::Vec3d, 3> basis = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (const auto& axis : basis) {
        const cv::Vec3d expected = display_rotation * axis;
        REQUIRE(angle_between(rotate_by_quaternion(projection->orientation, axis), expected) < 3.0 * kDegree);
    }
}

TEST_CASE("approximate_camera_matrix exposes the documented approximate intrinsics", "[vision]") {
    // The hoisted camera model must stay byte-for-byte the projectors'
    // documented one (fx = fy = width, principal point at the centre) -
    // seeds and reprojection checks built on it are only commensurable
    // with the projectors' output if the model never drifts.
    const cv::Matx33d camera = approximate_camera_matrix(kFrame);
    REQUIRE(camera(0, 0) == static_cast<double>(kFrame.width));
    REQUIRE(camera(1, 1) == static_cast<double>(kFrame.width));
    REQUIRE(camera(0, 2) == 0.5 * kFrame.width);
    REQUIRE(camera(1, 2) == 0.5 * kFrame.height);
    REQUIRE(camera(2, 2) == 1.0);
    REQUIRE(camera(0, 1) == 0.0);
    REQUIRE(camera(1, 0) == 0.0);
    REQUIRE(camera(2, 0) == 0.0);
    REQUIRE(camera(2, 1) == 0.0);
}

TEST_CASE("display_quaternion and opencv_rotation_from_display are mutual inverses", "[vision]") {
    // A generic display-frame rotation (nothing axis-aligned): 35 degrees
    // of pitch after -40 of yaw, same construction as the corner test.
    const double yaw = -40.0 * kDegree;
    const double pitch = 35.0 * kDegree;
    const cv::Matx33d rot_y(std::cos(yaw), 0.0, std::sin(yaw), 0.0, 1.0, 0.0, -std::sin(yaw), 0.0,
                            std::cos(yaw));
    const cv::Matx33d rot_x(1.0, 0.0, 0.0, 0.0, std::cos(pitch), -std::sin(pitch), 0.0, std::sin(pitch),
                            std::cos(pitch));
    const cv::Matx33d display_rotation = rot_x * rot_y;

    // Display -> OpenCV -> display must reproduce the rotation's ACTION
    // (basis-vector comparison per the helper comment, never raw
    // components).
    const std::array<float, 4> via_opencv = display_quaternion(
        opencv_rotation_from_display(display_quaternion(
            cv::Matx33d(1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0) * display_rotation *
            cv::Matx33d(1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0))));
    const std::array<cv::Vec3d, 3> basis = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (const auto& axis : basis) {
        const cv::Vec3d expected = display_rotation * axis;
        REQUIRE(angle_between(rotate_by_quaternion(via_opencv, axis), expected) < 0.1 * kDegree);
    }

    // And the inverse composed the other way is the identity map on any
    // pose the face path actually produces.
    const auto projection = project_cube_from_face(rendered_face_yawed(25.0 * kDegree, {0.0, 0.0, 3.0}), kFrame);
    REQUIRE(projection.has_value());
    cv::Matx33d recovered;
    cv::Rodrigues(projection->rvec, recovered);
    const std::array<float, 4> round_tripped = display_quaternion(
        opencv_rotation_from_display(projection->orientation));
    for (const auto& axis : basis) {
        REQUIRE(angle_between(rotate_by_quaternion(round_tripped, axis),
                              rotate_by_quaternion(projection->orientation, axis)) < 0.1 * kDegree);
    }
    // opencv_rotation_from_display really is the solvePnP-frame rotation:
    // it must match the exported rvec's matrix.
    const cv::Matx33d from_display = opencv_rotation_from_display(projection->orientation);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            REQUIRE(std::abs(from_display(r, c) - recovered(r, c)) < 1e-4);
        }
    }
}

TEST_CASE("project_cube_from_face exports reusable extrinsics and a clean residual", "[vision]") {
    const FaceObservation observation = rendered_face(20.0 * kDegree, cv::Vec3d(0.0, 0.0, 3.0));
    const auto projection = project_cube_from_face(observation, kFrame);
    REQUIRE(projection.has_value());
    REQUIRE(projection->orientation_valid);
    REQUIRE_FALSE(projection->from_seed);

    // The exported rvec/tvec must reproject the model onto the returned
    // vertices under the shared camera model - that identity is what makes
    // them usable as the next pass's PoseSeed.
    const std::vector<cv::Point3d> model(kCube.begin(), kCube.end());
    std::vector<cv::Point2d> reprojected;
    cv::projectPoints(model, projection->rvec, projection->tvec, approximate_camera_matrix(kFrame),
                      cv::noArray(), reprojected);
    for (std::size_t i = 0; i < 8; ++i) {
        REQUIRE(distance(projection->vertices[i],
                         cv::Point2f(static_cast<float>(reprojected[i].x),
                                     static_cast<float>(reprojected[i].y))) < 0.01F);
    }
    // Exact synthetic input: the residual must be numerically nil.
    REQUIRE(projection->max_reproj_fraction >= 0.0);
    REQUIRE(projection->max_reproj_fraction < 0.01);
}

TEST_CASE("project_cube_from_face seeded with its own pose keeps the unseeded answer", "[vision]") {
    // A consistent seed must be a no-op: IPPE agrees with history, so
    // arbitration never fires and the exported pose is the primary one.
    const FaceObservation observation = rendered_face_yawed(25.0 * kDegree, cv::Vec3d(0.0, 0.0, 3.0));
    const auto unseeded = project_cube_from_face(observation, kFrame);
    REQUIRE(unseeded.has_value());

    const PoseSeed seed{unseeded->rvec, unseeded->tvec};
    const auto seeded = project_cube_from_face(observation, kFrame, &seed);
    REQUIRE(seeded.has_value());
    REQUIRE_FALSE(seeded->from_seed);
    const std::array<cv::Vec3d, 3> basis = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (const auto& axis : basis) {
        REQUIRE(angle_between(rotate_by_quaternion(seeded->orientation, axis),
                              rotate_by_quaternion(unseeded->orientation, axis)) < 0.1 * kDegree);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        REQUIRE(distance(seeded->vertices[i], unseeded->vertices[i]) < 0.01F);
    }
}

TEST_CASE("project_cube_from_face mirror arbitration prefers the seed-consistent twin", "[vision]") {
    // The planar two-fold ambiguity in its near-affine regime: a face
    // tilted +50 degrees at z = 9 subtends little perspective, so the
    // mirror pose (-50 degrees) reprojects the same quad almost exactly -
    // precisely the frames where IPPE's choice of twin is luck. A seed on
    // the mirror side is >90 degrees from IPPE's answer (100 degrees of
    // relative rotation), which must trip arbitration; the seeded solve
    // settles in the seed's basin, passes the gates, and wins - temporal
    // continuity overriding the single-frame coin flip.
    const double tilt = 50.0 * kDegree;
    const cv::Vec3d translation(0.0, 0.0, 9.0);
    const FaceObservation observation = rendered_face(tilt, translation);

    // Build the mirror-side seed from a genuinely rendered mirror view so
    // it is a realistic previous-pass pose, not a hand-crafted matrix.
    const auto mirror_pose = project_cube_from_face(rendered_face(-tilt, translation), kFrame);
    REQUIRE(mirror_pose.has_value());
    const PoseSeed seed{mirror_pose->rvec, mirror_pose->tvec};

    const auto unseeded = project_cube_from_face(observation, kFrame);
    REQUIRE(unseeded.has_value());

    const auto seeded = project_cube_from_face(observation, kFrame, &seed);
    REQUIRE(seeded.has_value());
    REQUIRE(seeded->orientation_valid);
    REQUIRE(seeded->from_seed);
    // The arbitrated pose sits on the seed's side of the ambiguity, far
    // from IPPE's twin (they are ~100 degrees apart)...
    const cv::Vec3d probe(0.0, 1.0, 0.0);
    const double to_seed = angle_between(rotate_by_quaternion(seeded->orientation, probe),
                                         rotate_by_quaternion(mirror_pose->orientation, probe));
    const double to_ippe = angle_between(rotate_by_quaternion(seeded->orientation, probe),
                                         rotate_by_quaternion(unseeded->orientation, probe));
    REQUIRE(to_seed < to_ippe);
    REQUIRE(to_seed < 25.0 * kDegree);
    // ...and it genuinely explains the observed quad (the gate that keeps
    // arbitration honest).
    REQUIRE(seeded->max_reproj_fraction <= 0.15);
}

TEST_CASE("project_cube_from_face survives a wild seed unharmed", "[vision]") {
    // A garbage seed (cube behind the camera, upside down) must never
    // corrupt a confident estimate: whichever solve wins the arbitration,
    // the exported ORIENTATION must match the unseeded answer.
    const FaceObservation observation = rendered_face_yawed(25.0 * kDegree, cv::Vec3d(0.0, 0.0, 3.0));
    const auto unseeded = project_cube_from_face(observation, kFrame);
    REQUIRE(unseeded.has_value());

    const PoseSeed seed{cv::Vec3d(std::numbers::pi, 0.0, 0.0), cv::Vec3d(0.0, 0.0, -5.0)};
    const auto seeded = project_cube_from_face(observation, kFrame, &seed);
    REQUIRE(seeded.has_value());
    REQUIRE(seeded->orientation_valid);
    const std::array<cv::Vec3d, 3> basis = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (const auto& axis : basis) {
        REQUIRE(angle_between(rotate_by_quaternion(seeded->orientation, axis),
                              rotate_by_quaternion(unseeded->orientation, axis)) < 3.0 * kDegree);
    }
}

TEST_CASE("project_cube_from_corner seeded with its own pose reproduces the unseeded answer", "[vision]") {
    // Same true-perspective corner construction as the orientation test
    // above, reduced to its essentials.
    const double yaw = -40.0 * kDegree;
    const double pitch = 35.0 * kDegree;
    const cv::Matx33d rot_y(std::cos(yaw), 0.0, std::sin(yaw), 0.0, 1.0, 0.0, -std::sin(yaw), 0.0,
                            std::cos(yaw));
    const cv::Matx33d rot_x(1.0, 0.0, 0.0, 0.0, std::cos(pitch), -std::sin(pitch), 0.0, std::sin(pitch),
                            std::cos(pitch));
    const cv::Matx33d flip(1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0);
    const cv::Matx33d camera_rotation = flip * (rot_x * rot_y) * flip;
    const std::array<cv::Point3d, 7> display_corners = {{
        {-0.5, 0.5, -0.5},  // UBL
        {0.5, 0.5, -0.5},   // UBR
        {0.5, 0.5, 0.5},    // URF
        {-0.5, 0.5, 0.5},   // UFL
        {0.5, -0.5, -0.5},  // DRB
        {0.5, -0.5, 0.5},   // DRF
        {-0.5, -0.5, 0.5},  // DLF
    }};
    std::array<cv::Point2f, 7> projected{};
    const cv::Vec3d translation(0.0, 0.0, 4.0);
    for (std::size_t i = 0; i < display_corners.size(); ++i) {
        const auto& corner = display_corners[i];
        const cv::Vec3d model_cv = flip * cv::Vec3d(corner.x, corner.y, corner.z);
        const cv::Vec3d in_camera = camera_rotation * model_cv + translation;
        const double focal = static_cast<double>(kFrame.width);
        projected[i] = {static_cast<float>(focal * in_camera[0] / in_camera[2] + 0.5 * kFrame.width),
                        static_cast<float>(focal * in_camera[1] / in_camera[2] + 0.5 * kFrame.height)};
    }
    CornerObservation observation;
    observation.top.quad = {projected[0], projected[1], projected[2], projected[3]};
    observation.lower_left.quad = {projected[3], projected[2], projected[5], projected[6]};
    observation.lower_right.quad = {projected[2], projected[1], projected[4], projected[5]};
    observation.geometry_confidence = 1.0F;

    const auto unseeded = project_cube_from_corner(observation, kFrame);
    REQUIRE(unseeded.has_value());
    REQUIRE(unseeded->orientation_valid);
    REQUIRE_FALSE(unseeded->from_seed);
    REQUIRE(unseeded->max_reproj_fraction <= 0.15);

    const PoseSeed seed{unseeded->rvec, unseeded->tvec};
    const auto seeded = project_cube_from_corner(observation, kFrame, &seed);
    REQUIRE(seeded.has_value());
    REQUIRE(seeded->orientation_valid);
    // The seeded attempt runs first and its own gates accepted it.
    REQUIRE(seeded->from_seed);
    const std::array<cv::Vec3d, 3> basis = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (const auto& axis : basis) {
        REQUIRE(angle_between(rotate_by_quaternion(seeded->orientation, axis),
                              rotate_by_quaternion(unseeded->orientation, axis)) < 0.5 * kDegree);
    }

    // A garbage seed falls back to the cold-start solve rather than
    // failing or drifting: same orientation, just not seed-flagged.
    const PoseSeed garbage{cv::Vec3d(std::numbers::pi, 0.0, 0.0), cv::Vec3d(0.0, 0.0, -5.0)};
    const auto recovered = project_cube_from_corner(observation, kFrame, &garbage);
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->orientation_valid);
    for (const auto& axis : basis) {
        REQUIRE(angle_between(rotate_by_quaternion(recovered->orientation, axis),
                              rotate_by_quaternion(unseeded->orientation, axis)) < 0.5 * kDegree);
    }
}

TEST_CASE("project_cube_from_face rejects degenerate quads", "[vision]") {
    // Collinear corners: zero area, no homography exists.
    const auto collinear = project_cube_from_face(
        observation_with_quad({{{100.0F, 200.0F}, {200.0F, 200.0F}, {300.0F, 200.0F}, {400.0F, 200.0F}}}),
        kFrame);
    REQUIRE_FALSE(collinear.has_value());
    REQUIRE(collinear.error() == cube::CubeError::no_valid_cube_detected);

    // A default-constructed observation (all corners at the origin).
    const auto empty = project_cube_from_face(FaceObservation{}, kFrame);
    REQUIRE_FALSE(empty.has_value());
    REQUIRE(empty.error() == cube::CubeError::no_valid_cube_detected);
}
