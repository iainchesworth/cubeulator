#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cube::vision {

namespace {

// A quad smaller than this (or collinear - zero area) cannot be a cube face
// at the scales the detectors accept, and would hand solvePnP a degenerate
// homography; reject before OpenCV gets a chance to misbehave on it.
constexpr double kMinQuadAreaPx2 = 4.0;

// Unit-cube model in the object frame handed to solvePnP: the detected face
// is the z=0 unit square with image-style axes (x right, y down), corners
// ordered tl, tr, br, bl to match FaceObservation::face_quad; the rear face
// sits at z=+1 in the same order. OpenCV's camera looks down +z, and for a
// face presented to the camera the recovered object axes come out roughly
// camera-aligned, so object +z points away from the viewer and z=+1 is the
// farther face (the cheirality check below rejects the poses where it is
// not).
const std::array<cv::Point3d, 8> kCubeModel = {{
    {-0.5, -0.5, 0.0},  // 0: near tl
    {0.5, -0.5, 0.0},   // 1: near tr
    {0.5, 0.5, 0.0},    // 2: near br
    {-0.5, 0.5, 0.0},   // 3: near bl
    {-0.5, -0.5, 1.0},  // 4: rear, behind tl
    {0.5, -0.5, 1.0},   // 5: rear, behind tr
    {0.5, 0.5, 1.0},    // 6: rear, behind br
    {-0.5, 0.5, 1.0},   // 7: rear, behind bl
}};

// Intrinsics approximated per the header contract: fx = fy = frame width
// (a typical webcam's ~53 degree horizontal FOV gives tan(hfov/2) ~= 0.5,
// i.e. f ~= width), principal point at the frame centre, no distortion.
// Good enough for an overlay to track convincingly; not metrology.
[[nodiscard]] cv::Matx33d approximate_camera_matrix(cv::Size frame_size) {
    const double focal = static_cast<double>(frame_size.width);
    return {focal, 0.0,   0.5 * static_cast<double>(frame_size.width),
            0.0,   focal, 0.5 * static_cast<double>(frame_size.height),
            0.0,   0.0,   1.0};
}

// OpenCV's camera frame has +x right, +y DOWN, +z INTO the screen; the
// orientation convention exported by CubePoseProjection (see the header) is
// the graphics-style +x right, +y UP, +z OUT of the screen toward the
// viewer. The two frames differ by a 180-degree rotation about x,
// F = diag(1, -1, -1), which is its own inverse. The object models handed
// to solvePnP below use the same image-style y-down/z-in axes as OpenCV's
// camera, so BOTH sides of a recovered model->camera rotation R need the
// change of basis: R_display = F * R_opencv * F. Sanity anchor: a face
// presented flat-on gives R_opencv ~= I, and F * I * F = I - the exported
// identity means "canonical camera-facing pose" as documented.
const cv::Matx33d kOpenCVToDisplayFlip{1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0};

// Standard rotation-matrix -> quaternion conversion (Shepperd's method):
// four algebraically equivalent formulas, each dividing by one of
// sqrt(1 +/- diagonal combinations); picking the branch with the largest
// such term (trace first, else the max diagonal element) keeps the divisor
// well away from zero, so the conversion stays stable for rotations near
// 180 degrees where the naive trace-only formula catastrophically cancels.
// Returns {w, x, y, z}, normalised.
[[nodiscard]] std::array<float, 4> quaternion_from_rotation(const cv::Matx33d& m) {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    const double trace = m(0, 0) + m(1, 1) + m(2, 2);
    if (trace > 0.0) {
        const double s = 2.0 * std::sqrt(trace + 1.0);  // s = 4w
        w = 0.25 * s;
        x = (m(2, 1) - m(1, 2)) / s;
        y = (m(0, 2) - m(2, 0)) / s;
        z = (m(1, 0) - m(0, 1)) / s;
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const double s = 2.0 * std::sqrt(1.0 + m(0, 0) - m(1, 1) - m(2, 2));  // s = 4x
        w = (m(2, 1) - m(1, 2)) / s;
        x = 0.25 * s;
        y = (m(0, 1) + m(1, 0)) / s;
        z = (m(0, 2) + m(2, 0)) / s;
    } else if (m(1, 1) > m(2, 2)) {
        const double s = 2.0 * std::sqrt(1.0 + m(1, 1) - m(0, 0) - m(2, 2));  // s = 4y
        w = (m(0, 2) - m(2, 0)) / s;
        x = (m(0, 1) + m(1, 0)) / s;
        y = 0.25 * s;
        z = (m(1, 2) + m(2, 1)) / s;
    } else {
        const double s = 2.0 * std::sqrt(1.0 + m(2, 2) - m(0, 0) - m(1, 1));  // s = 4z
        w = (m(1, 0) - m(0, 1)) / s;
        x = (m(0, 2) + m(2, 0)) / s;
        y = (m(1, 2) + m(2, 1)) / s;
        z = 0.25 * s;
    }
    const double norm = std::sqrt(w * w + x * x + y * y + z * z);
    return {static_cast<float>(w / norm), static_cast<float>(x / norm), static_cast<float>(y / norm),
            static_cast<float>(z / norm)};
}

// Converts a solvePnP model->camera rotation (both in OpenCV-style axes)
// into the header's exported display-frame quaternion.
[[nodiscard]] std::array<float, 4> display_quaternion(const cv::Matx33d& opencv_rotation) {
    return quaternion_from_rotation(kOpenCVToDisplayFlip * opencv_rotation * kOpenCVToDisplayFlip);
}

[[nodiscard]] double quad_area(const std::array<cv::Point2f, 4>& quad) {
    // Shoelace over the tl,tr,br,bl ring; absolute so winding is irrelevant.
    double twice_area = 0.0;
    for (std::size_t i = 0; i < 4; ++i) {
        const cv::Point2f& a = quad[i];
        const cv::Point2f& b = quad[(i + 1) % 4];
        twice_area += static_cast<double>(a.x) * static_cast<double>(b.y) -
                      static_cast<double>(b.x) * static_cast<double>(a.y);
    }
    return 0.5 * std::abs(twice_area);
}

// Degeneracy guard shared by both paths: a near-singular pose (or an
// inconsistent set of quads) flings projected vertices far from the frame.
// Anything non-finite or beyond twice the frame diagonal from the centre
// cannot be a plausible overlay of a cube the detectors just saw, so the
// whole projection is rejected rather than drawing garbage.
[[nodiscard]] bool projection_is_plausible(const std::array<cv::Point2f, 8>& vertices, cv::Size frame_size) {
    const float centre_x = 0.5F * static_cast<float>(frame_size.width);
    const float centre_y = 0.5F * static_cast<float>(frame_size.height);
    const float limit =
        2.0F * std::hypot(static_cast<float>(frame_size.width), static_cast<float>(frame_size.height));
    for (const cv::Point2f& vertex : vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) {
            return false;
        }
        if (std::hypot(vertex.x - centre_x, vertex.y - centre_y) > limit) {
            return false;
        }
    }
    return true;
}

}  // namespace

Result<CubePoseProjection> project_cube_from_face(const FaceObservation& observation, cv::Size frame_size) {
    if (frame_size.width <= 0 || frame_size.height <= 0) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    const std::array<cv::Point2f, 4>& quad = observation.face_quad;
    if (quad_area(quad) < kMinQuadAreaPx2) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    // SOLVEPNP_IPPE_SQUARE is purpose-built for exactly this problem (four
    // coplanar corners of a square) but demands its points in the fixed
    // canonical order (-l/2,+l/2), (+l/2,+l/2), (+l/2,-l/2), (-l/2,-l/2).
    // In our y-down model that is bl, br, tr, tl - so both lists are handed
    // over reversed; the recovered pose is order-independent.
    const std::vector<cv::Point3d> object_points = {kCubeModel[3], kCubeModel[2], kCubeModel[1], kCubeModel[0]};
    const std::vector<cv::Point2d> image_points = {cv::Point2d(quad[3]), cv::Point2d(quad[2]),
                                                   cv::Point2d(quad[1]), cv::Point2d(quad[0])};

    const cv::Matx33d camera = approximate_camera_matrix(frame_size);
    cv::Vec3d rvec;
    cv::Vec3d tvec;
    bool solved = false;
    try {
        solved = cv::solvePnP(object_points, image_points, camera, cv::noArray(), rvec, tvec, false,
                              cv::SOLVEPNP_IPPE_SQUARE);
    } catch (const cv::Exception&) {
        solved = false;
    }
    if (!solved) {
        // IPPE throws on inputs whose homography is near-singular; the
        // generic iterative solver tolerates more and still initialises
        // from a planar homography for four coplanar points.
        try {
            solved = cv::solvePnP(object_points, image_points, camera, cv::noArray(), rvec, tvec, false,
                                  cv::SOLVEPNP_ITERATIVE);
        } catch (const cv::Exception&) {
            solved = false;
        }
    }
    if (!solved) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    // Cheirality: every corner of the cube must sit in front of the camera,
    // otherwise projectPoints mirrors points through the principal point
    // and the wireframe folds inside out. Planar-pose ambiguity flips can
    // produce exactly this, so it is treated as a failed estimate.
    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);
    for (const cv::Point3d& corner : kCubeModel) {
        const cv::Vec3d in_camera = rotation * cv::Vec3d(corner.x, corner.y, corner.z) + tvec;
        if (in_camera[2] <= 0.0) {
            return std::unexpected(CubeError::no_valid_cube_detected);
        }
    }

    std::vector<cv::Point2d> projected;
    cv::projectPoints(std::vector<cv::Point3d>(kCubeModel.begin(), kCubeModel.end()), rvec, tvec, camera,
                      cv::noArray(), projected);

    CubePoseProjection result;
    // The pose that just passed cheirality IS the orientation - only the
    // axis convention needs reconciling (see kOpenCVToDisplayFlip).
    result.orientation = display_quaternion(rotation);
    result.orientation_valid = true;
    for (std::size_t i = 0; i < result.vertices.size(); ++i) {
        result.vertices[i] =
            cv::Point2f(static_cast<float>(projected[i].x), static_cast<float>(projected[i].y));
    }
    if (!projection_is_plausible(result.vertices, frame_size)) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    return result;
}

Result<CubePoseProjection> project_cube_from_corner(const CornerObservation& observation, cv::Size frame_size) {
    if (frame_size.width <= 0 || frame_size.height <= 0) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    const std::array<cv::Point2f, 4>& top = observation.top.quad;
    const std::array<cv::Point2f, 4>& left = observation.lower_left.quad;
    const std::array<cv::Point2f, 4>& right = observation.lower_right.quad;
    for (const std::array<cv::Point2f, 4>* quad : {&top, &left, &right}) {
        if (quad_area(*quad) < kMinQuadAreaPx2) {
            return std::unexpected(CubeError::no_valid_cube_detected);
        }
    }

    // The three visible quads pin seven distinct cube corners directly - no
    // solvePnP needed. From corner_observation.hpp's documented pose (top
    // face rhombus above, side parallelograms hanging below-left and
    // below-right, all meeting at the shared near corner) the quad-corner
    // coincidences are, in U/F/R vertex names for the first corner view
    // (identical geometry for the second):
    //
    //   URF (shared near corner): top.br = lower_left.tr = lower_right.tl
    //   UFL (top-left edge):      top.bl = lower_left.tl
    //   UBR (top-right edge):     top.tr = lower_right.tr
    //   DRF (bottom shared edge): lower_left.br = lower_right.bl
    //   UBL / DLF / DRB:          top.tl / lower_left.bl / lower_right.br
    //
    // Coincident estimates are averaged so small detector disagreement is
    // split rather than favouring one face's fit.
    const cv::Point2f v_ubl = top[0];
    const cv::Point2f v_ubr = 0.5F * (top[1] + right[1]);
    const cv::Point2f v_urf = (top[2] + left[1] + right[0]) / 3.0F;
    const cv::Point2f v_ufl = 0.5F * (top[3] + left[0]);
    const cv::Point2f v_drb = right[2];
    const cv::Point2f v_drf = 0.5F * (left[2] + right[3]);
    const cv::Point2f v_dlf = left[3];

    // The eighth corner (DLB) is fully occluded. The bottom face projects
    // to (very nearly) a parallelogram, and three of its corners - DLF,
    // DRF, DRB - are already pinned, so the fourth completes it:
    // DLB = DLF + DRB - DRF.
    const cv::Point2f v_dlb = v_dlf + v_drb - v_drf;

    // Header vertex order with the TOP face as the near face: 0-3 are the
    // top quad's tl,tr,br,bl (UBL, UBR, URF, UFL), 4-7 the bottom-face
    // corner vertically behind each (DLB, DRB, DRF, DLF).
    CubePoseProjection result;
    result.vertices = {v_ubl, v_ubr, v_urf, v_ufl, v_dlb, v_drb, v_drf, v_dlf};
    if (!projection_is_plausible(result.vertices, frame_size)) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    // Orientation (header contract): the pinned corners are image-plane
    // points only, so depth - and with it the 3D basis - must be recovered
    // through the camera model. Seven non-coplanar correspondences make a
    // well-posed generic PnP problem, solved under the SAME approximate
    // intrinsics as the face path. The model below is the unit cube in
    // OpenCV-style axes (x right, y down, z into the screen), CENTRED at
    // the origin so the recovered rotation is about the cube's centre, and
    // oriented to match the canonical display frame after the axis flip:
    // display U (+y up) is model y = -0.5, display F (+z toward viewer) is
    // model z = -0.5, display R (+x right) is model x = +0.5. Each named
    // corner is therefore, e.g., URF = (+0.5, -0.5, -0.5). The synthesized
    // DLB is deliberately excluded - it carries no independent information
    // and its parallelogram completion is exact only for a true affine
    // projection.
    static const std::vector<cv::Point3d> kCornerModel = {
        {-0.5, -0.5, 0.5},   // UBL
        {0.5, -0.5, 0.5},    // UBR
        {0.5, -0.5, -0.5},   // URF
        {-0.5, -0.5, -0.5},  // UFL
        {0.5, 0.5, 0.5},     // DRB
        {0.5, 0.5, -0.5},    // DRF
        {-0.5, 0.5, -0.5},   // DLF
    };
    const std::vector<cv::Point2d> corner_images = {cv::Point2d(v_ubl), cv::Point2d(v_ubr), cv::Point2d(v_urf),
                                                    cv::Point2d(v_ufl), cv::Point2d(v_drb), cv::Point2d(v_drf),
                                                    cv::Point2d(v_dlf)};
    const cv::Matx33d camera = approximate_camera_matrix(frame_size);
    cv::Vec3d rvec;
    cv::Vec3d tvec;
    bool solved = false;
    try {
        solved = cv::solvePnP(kCornerModel, corner_images, camera, cv::noArray(), rvec, tvec, false,
                              cv::SOLVEPNP_ITERATIVE);
    } catch (const cv::Exception&) {
        solved = false;
    }
    if (solved) {
        cv::Matx33d rotation;
        cv::Rodrigues(rvec, rotation);
        // Cheirality, as in the face path: a fit that puts corners behind
        // the camera is a mirror-ambiguous solution, not a pose.
        for (const cv::Point3d& corner : kCornerModel) {
            const cv::Vec3d in_camera = rotation * cv::Vec3d(corner.x, corner.y, corner.z) + tvec;
            if (in_camera[2] <= 0.0) {
                solved = false;
                break;
            }
        }
        if (solved) {
            // The constructed quads need not be consistent with ANY true
            // perspective projection of a cube (each detector fits its face
            // independently), so the least-squares fit can be arbitrarily
            // wrong while still converging. Gate on reprojection error
            // relative to the cube's apparent size: a genuine pose puts
            // every corner back within a modest fraction of the on-screen
            // radius; a garbage fit scatters them.
            std::vector<cv::Point2d> reprojected;
            cv::projectPoints(kCornerModel, rvec, tvec, camera, cv::noArray(), reprojected);
            cv::Point2d centroid(0.0, 0.0);
            for (const cv::Point2d& point : corner_images) {
                centroid += point;
            }
            centroid *= 1.0 / static_cast<double>(corner_images.size());
            const auto planar_distance = [](const cv::Point2d& a, const cv::Point2d& b) {
                return std::hypot(a.x - b.x, a.y - b.y);
            };
            double apparent_radius = 0.0;
            double max_error = 0.0;
            for (std::size_t i = 0; i < corner_images.size(); ++i) {
                apparent_radius = std::max(apparent_radius, planar_distance(corner_images[i], centroid));
                max_error = std::max(max_error, planar_distance(reprojected[i], corner_images[i]));
            }
            constexpr double kMaxReprojectionFraction = 0.15;
            if (std::isfinite(max_error) && apparent_radius > 0.0 &&
                max_error <= kMaxReprojectionFraction * apparent_radius) {
                result.orientation = display_quaternion(rotation);
            } else {
                solved = false;
            }
        }
    }
    // Honest fallback per the header: no robust basis means identity +
    // invalid, never a guess - the UI's idle rotation takes over.
    result.orientation_valid = solved;
    return result;
}

}  // namespace cube::vision
