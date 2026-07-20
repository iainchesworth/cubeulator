#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
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

// The inverse direction (unit quaternion {w,x,y,z} -> rotation matrix),
// the textbook expansion of q v conj(q). Normalisation is the caller's
// concern; every quaternion this file round-trips is already unit.
[[nodiscard]] cv::Matx33d rotation_from_quaternion(const std::array<float, 4>& q) {
    const double w = static_cast<double>(q[0]);
    const double x = static_cast<double>(q[1]);
    const double y = static_cast<double>(q[2]);
    const double z = static_cast<double>(q[3]);
    return {1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - w * z),       2.0 * (x * z + w * y),
            2.0 * (x * y + w * z),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - w * x),
            2.0 * (x * z - w * y),       2.0 * (y * z + w * x),       1.0 - 2.0 * (x * x + y * y)};
}

// Geodesic angle (degrees) between two rotations: the angle of a * b^T,
// read off its trace. What the face path's mirror arbitration compares -
// how far IPPE's answer is from the seed AS A ROTATION, which is immune
// to the quaternion double cover and to any axis-decomposition choice.
[[nodiscard]] double rotation_angle_deg_between(const cv::Matx33d& a, const cv::Matx33d& b) {
    const cv::Matx33d relative = a * b.t();
    const double cosine = std::clamp(0.5 * (relative(0, 0) + relative(1, 1) + relative(2, 2) - 1.0), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / std::numbers::pi;
}

// Above this angular distance between IPPE's answer and the seed, the
// planar two-fold ambiguity (or an outright bad fit) is the only credible
// explanation - a hand-held cube cannot rotate a quarter turn between two
// consecutive analysis passes, and the two IPPE mirror solutions sit 2x
// the face's tilt apart, so 90 degrees only ever trips on genuinely
// discontinuous answers. Kept deliberately far above real per-pass motion
// (tens of degrees at worst) so arbitration can never override a merely
// fast-but-honest estimate.
constexpr double kMirrorArbitrationDeg = 90.0;

// Reprojection acceptance shared by every seeded/derived solve: a genuine
// pose puts every model corner back within this fraction of the
// observation's apparent on-screen radius; a garbage fit scatters them.
// The value is the corner path's long-standing 15% gate, now the single
// authority for both paths.
constexpr double kMaxReprojectionFraction = 0.15;

// Cheirality: every model corner must sit in front of the camera,
// otherwise projectPoints mirrors points through the principal point and
// the wireframe folds inside out. Planar-pose ambiguity flips can produce
// exactly this, so a failure is treated as a failed estimate, never
// patched up.
[[nodiscard]] bool passes_cheirality(const cv::Matx33d& rotation, const cv::Vec3d& tvec,
                                     const std::vector<cv::Point3d>& model_points) {
    for (const cv::Point3d& corner : model_points) {
        const cv::Vec3d in_camera = rotation * cv::Vec3d(corner.x, corner.y, corner.z) + tvec;
        if (in_camera[2] <= 0.0) {
            return false;
        }
    }
    return true;
}

// Worst single-point reprojection error as a fraction of the observed
// points' apparent radius (max distance from their centroid) - the
// scale-free residual behind kMaxReprojectionFraction and the exported
// max_reproj_fraction field. Returns +infinity for non-finite errors or a
// degenerate (zero-radius) observation so callers' <= comparisons reject
// naturally.
[[nodiscard]] double max_reprojection_fraction(const std::vector<cv::Point3d>& object_points,
                                               const std::vector<cv::Point2d>& image_points,
                                               const cv::Vec3d& rvec, const cv::Vec3d& tvec,
                                               const cv::Matx33d& camera) {
    std::vector<cv::Point2d> reprojected;
    cv::projectPoints(object_points, rvec, tvec, camera, cv::noArray(), reprojected);
    cv::Point2d centroid(0.0, 0.0);
    for (const cv::Point2d& point : image_points) {
        centroid += point;
    }
    centroid *= 1.0 / static_cast<double>(image_points.size());
    const auto planar_distance = [](const cv::Point2d& a, const cv::Point2d& b) {
        return std::hypot(a.x - b.x, a.y - b.y);
    };
    double apparent_radius = 0.0;
    double max_error = 0.0;
    for (std::size_t i = 0; i < image_points.size(); ++i) {
        apparent_radius = std::max(apparent_radius, planar_distance(image_points[i], centroid));
        max_error = std::max(max_error, planar_distance(reprojected[i], image_points[i]));
    }
    if (!std::isfinite(max_error) || apparent_radius <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return max_error / apparent_radius;
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

cv::Matx33d approximate_camera_matrix(cv::Size frame_size) {
    const double focal = static_cast<double>(frame_size.width);
    return {focal, 0.0,   0.5 * static_cast<double>(frame_size.width),
            0.0,   focal, 0.5 * static_cast<double>(frame_size.height),
            0.0,   0.0,   1.0};
}

std::array<float, 4> display_quaternion(const cv::Matx33d& opencv_rotation) {
    return quaternion_from_rotation(kOpenCVToDisplayFlip * opencv_rotation * kOpenCVToDisplayFlip);
}

cv::Matx33d opencv_rotation_from_display(const std::array<float, 4>& display_orientation) {
    // Same conjugation as display_quaternion, run the other way; F is its
    // own inverse so the sandwich is symmetric.
    return kOpenCVToDisplayFlip * rotation_from_quaternion(display_orientation) * kOpenCVToDisplayFlip;
}

namespace {

// A face's outward normal must clear the image plane by at least this
// (camera-frame -z component) before the hint calls it visible: below
// ~8.6 degrees of elevation the face projects as a sliver no lattice fit
// could hold anyway, and demanding it of ALL THREE faces is what separates
// a genuine corner presentation from a frontal/edge-on pose (whose third
// face hugs the limb). The bound also floors every visible normal's
// on-screen magnitude at ~0.24 (three orthonormal z-components square-sum
// to 1), keeping the direction normalisations below well-conditioned.
constexpr double kMinHintFacingCos = 0.15;

}  // namespace

std::optional<CornerHint> compute_corner_hint(const PoseSeed& seed) {
    cv::Matx33d rotation;
    cv::Rodrigues(seed.rvec, rotation);

    // The six outward face normals of the unit-cube model (both projectors
    // share these model AXES; only the origin differs, which a rotation-
    // only derivation never touches - see the header). Camera frame: +z
    // into the screen, so a normal faces the viewer when its rotated z
    // component is negative.
    static const std::array<cv::Vec3d, 6> kModelNormals = {{{1.0, 0.0, 0.0},
                                                            {-1.0, 0.0, 0.0},
                                                            {0.0, 1.0, 0.0},
                                                            {0.0, -1.0, 0.0},
                                                            {0.0, 0.0, 1.0},
                                                            {0.0, 0.0, -1.0}}};
    std::array<cv::Vec3d, 3> visible{};
    std::size_t visible_count = 0;
    for (const cv::Vec3d& normal : kModelNormals) {
        const cv::Vec3d in_camera = rotation * normal;
        if (in_camera[2] < -kMinHintFacingCos) {
            if (visible_count == visible.size()) {
                return std::nullopt;  // unreachable for a rotation; defensive
            }
            visible[visible_count++] = normal;
        }
    }
    if (visible_count != visible.size()) {
        return std::nullopt;
    }

    // On-screen image-coordinate direction of a model direction (weak
    // perspective, header rationale): camera x is screen-right, camera y
    // is screen-down - exactly the y-down convention the detector's sector
    // angles use.
    const auto screen_vec = [&rotation](const cv::Vec3d& direction) {
        const cv::Vec3d in_camera = rotation * direction;
        return cv::Point2f(static_cast<float>(in_camera[0]), static_cast<float>(in_camera[1]));
    };

    // Role assignment. "Top" is the visible face whose projected normal
    // points most up-screen - the same convention the fixed pose encodes
    // (its top face IS the up-screen one), kept so the hint degenerates to
    // the historical arrangement at the prompted pose. Any of the three
    // cyclic assignments would be geometrically valid (they differ by a
    // 120-degree turn about the shared corner's diagonal, a lattice
    // element the tracker's gauge resolution absorbs); picking the
    // up-screen one deterministically keeps the roles stable away from the
    // 120-degree changeover, where frame noise CAN flip the winner between
    // passes. Deliberately no hysteresis there: it would need the previous
    // pass's role state threaded through a function that is otherwise pure
    // in the seed, and a flip costs nothing in correctness - identity comes
    // from the tracker's gauge resolution, and the relabelled window_map
    // forces the per-role aggregation reset in scan_session's add_keyed
    // (the chimera defence doing its job). The price is bounded capture
    // latency near the boundary: a reset window must re-converge.
    double best_upness = -std::numeric_limits<double>::infinity();
    std::size_t top_index = 0;
    for (std::size_t i = 0; i < visible.size(); ++i) {
        const cv::Point2f dir = screen_vec(visible[i]);
        const double length = std::hypot(static_cast<double>(dir.x), static_cast<double>(dir.y));
        if (length <= 0.0) {
            return std::nullopt;  // floored away by kMinHintFacingCos; defensive
        }
        const double upness = -static_cast<double>(dir.y) / length;  // image y grows downward
        if (upness > best_upness) {
            best_upness = upness;
            top_index = i;
        }
    }
    const cv::Vec3d n_top = visible[top_index];
    cv::Vec3d n_left = visible[(top_index + 1) % 3];
    cv::Vec3d n_right = visible[(top_index + 2) % 3];
    // Chirality pins the other two roles: in the detector's model frame the
    // roles play U/F/R around the shared near corner, and U x F = R (model
    // normals (0,-1,0) x (0,0,-1) = (1,0,0)). The three visible normals are
    // mutually orthogonal +-axes, so exactly one ordering of the remaining
    // pair satisfies top x left = right; the other is the mirror
    // arrangement, which no rotation of a real cube can present.
    if (n_top.cross(n_left).dot(n_right) < 0.0) {
        std::swap(n_left, n_right);
    }

    CornerHint hint;
    const std::array<cv::Vec3d, 3> role_normals = {n_top, n_left, n_right};
    for (std::size_t role = 0; role < role_normals.size(); ++role) {
        const cv::Point2f dir = screen_vec(role_normals[role]);
        const float length = std::hypot(dir.x, dir.y);
        hint.face_dir[role] = dir / length;
    }

    // Lattice bases per role, as 3D model directions first: each face's
    // cell columns/rows run parallel to the OTHER faces' normals, and the
    // corner topology (which face corner is the shared one, where rows and
    // columns advance) fixes the signs. Derived from the fixed pose's
    // documented cell order (corner_slot_mappings' derivation): the top
    // face's columns run along the right face's normal and its rows along
    // the left face's (rows recede toward the shared corner); both hanging
    // faces' rows run down-screen away from the top face (-n_top), the
    // left one's columns along n_right (toward the shared corner) and the
    // right one's along -n_left (away from it). At the isometric pose this
    // reproduces the fixed kTopColDir/kSideRowDir/... values exactly.
    const std::array<std::pair<cv::Vec3d, cv::Vec3d>, 3> role_bases = {{
        {n_right, n_left},          // top: col, row
        {n_right, -1.0 * n_top},    // lower_left
        {-1.0 * n_left, -1.0 * n_top},  // lower_right
    }};
    for (std::size_t role = 0; role < role_bases.size(); ++role) {
        const cv::Point2f col = screen_vec(role_bases[role].first);
        const cv::Point2f row = screen_vec(role_bases[role].second);
        const float col_length = std::hypot(col.x, col.y);
        const float row_length = std::hypot(row.x, row.y);
        const float shorter = std::min(col_length, row_length);
        if (shorter <= 0.0F) {
            return std::nullopt;  // same floor argument as face_dir; defensive
        }
        // Shorter-axis normalisation (CornerHint contract): the sector
        // pitch estimate is a nearest-neighbour median, i.e. the spacing
        // along the MORE foreshortened axis, so scaling that axis to unit
        // makes pitch * dir land both steps of an oblique face correctly.
        hint.col_dir[role] = col / shorter;
        hint.row_dir[role] = row / shorter;
    }
    return hint;
}

std::optional<EdgeHint> compute_edge_hint(const PoseSeed& seed) {
    cv::Matx33d rotation;
    cv::Rodrigues(seed.rvec, rotation);

    // Same visibility floor and model normals as compute_corner_hint; an
    // edge presentation is EXACTLY two faces clearing it (three is a
    // corner, one is frontal - each has its own hint/detector).
    static const std::array<cv::Vec3d, 6> kModelNormals = {{{1.0, 0.0, 0.0},
                                                            {-1.0, 0.0, 0.0},
                                                            {0.0, 1.0, 0.0},
                                                            {0.0, -1.0, 0.0},
                                                            {0.0, 0.0, 1.0},
                                                            {0.0, 0.0, -1.0}}};
    std::array<cv::Vec3d, 2> visible{};
    std::size_t visible_count = 0;
    for (const cv::Vec3d& normal : kModelNormals) {
        const cv::Vec3d in_camera = rotation * normal;
        if (in_camera[2] < -kMinHintFacingCos) {
            if (visible_count == visible.size()) {
                return std::nullopt;  // three or more: a corner presentation
            }
            visible[visible_count++] = normal;
        }
    }
    if (visible_count != visible.size()) {
        return std::nullopt;
    }

    const auto screen_vec = [&rotation](const cv::Vec3d& direction) {
        const cv::Vec3d in_camera = rotation * direction;
        return cv::Point2f(static_cast<float>(in_camera[0]), static_cast<float>(in_camera[1]));
    };

    // Role assignment by screen side: left plays F, right plays R. Always
    // chirality-consistent for a real cube (n_left x n_right = the shared
    // edge's UP direction, exactly F x R = U), so no mirror case exists -
    // see edge_observation.hpp's convention note.
    cv::Vec3d n_left = visible[0];
    cv::Vec3d n_right = visible[1];
    if (screen_vec(n_left).x > screen_vec(n_right).x) {
        std::swap(n_left, n_right);
    }

    // Rows run down-screen along the shared edge: edge-up is
    // n_left x n_right, so the row direction is its negation. A shared
    // edge closer to horizontal than vertical is a top/bottom edge
    // presentation this detector does not model - refuse the hint rather
    // than fit grids against a basis the split search can't honour.
    const cv::Vec3d up3 = n_left.cross(n_right);
    const cv::Point2f row = screen_vec(-1.0 * up3);
    const float row_length = std::hypot(row.x, row.y);
    if (row_length <= 0.0F || std::abs(row.y) <= std::abs(row.x) || row.y <= 0.0F) {
        return std::nullopt;
    }

    // Columns mirror the corner topology: the left (F-role) face's columns
    // advance toward the shared edge - along the right face's normal - and
    // the right (R-role) face's columns advance away from it, along the
    // left face's negated normal (model F->B).
    const std::array<cv::Vec3d, 2> col3 = {n_right, -1.0 * n_left};
    EdgeHint hint;
    for (std::size_t role = 0; role < 2; ++role) {
        const cv::Point2f col = screen_vec(col3[role]);
        const float col_length = std::hypot(col.x, col.y);
        const float shorter = std::min(col_length, row_length);
        if (shorter <= 0.0F) {
            return std::nullopt;  // floored away by kMinHintFacingCos; defensive
        }
        // Shorter-axis normalisation, same contract as CornerHint: the
        // sector pitch estimate is the spacing along the more
        // foreshortened axis, so that axis must be unit.
        hint.col_dir[role] = col / shorter;
        hint.row_dir[role] = row / shorter;
    }
    return hint;
}

Result<CubePoseProjection> project_cube_from_face(const FaceObservation& observation, cv::Size frame_size,
                                                  const PoseSeed* seed) {
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
    // The full cube model's corners for cheirality: the FITTED points are
    // only the four near-face corners, but a mirror flip typically pushes
    // the REAR face behind the camera first, so all eight must be checked.
    static const std::vector<cv::Point3d> kAllCorners(kCubeModel.begin(), kCubeModel.end());
    cv::Vec3d rvec;
    cv::Vec3d tvec;
    bool solved = false;
    bool from_seed = false;
    try {
        solved = cv::solvePnP(object_points, image_points, camera, cv::noArray(), rvec, tvec, false,
                              cv::SOLVEPNP_IPPE_SQUARE);
    } catch (const cv::Exception&) {
        solved = false;
    }
    if (!solved) {
        // IPPE throws on inputs whose homography is near-singular; the
        // generic iterative solver tolerates more and still initialises
        // from a planar homography for four coplanar points - or, when a
        // seed is available, from the previous pass's pose, which keeps
        // the optimiser in the basin the cube actually occupied.
        try {
            if (seed != nullptr) {
                rvec = seed->rvec;
                tvec = seed->tvec;
            }
            solved = cv::solvePnP(object_points, image_points, camera, cv::noArray(), rvec, tvec,
                                  seed != nullptr, cv::SOLVEPNP_ITERATIVE);
            from_seed = solved && seed != nullptr;
        } catch (const cv::Exception&) {
            solved = false;
        }
    }
    if (!solved) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);
    bool cheirality_ok = passes_cheirality(rotation, tvec, kAllCorners);

    // Mirror arbitration (header contract): the planar square's two-fold
    // ambiguity lets IPPE hand back the reflected pose, and no single
    // frame can tell the twins apart - but history can. When the answer
    // sits impossibly far from the seed (or already failed cheirality,
    // the flip's other signature), a seed-initialised iterative solve is
    // tried; it wins ONLY by passing the same cheirality gate plus the
    // reprojection bar, i.e. by genuinely explaining the observed quad
    // from the temporally consistent side.
    if (seed != nullptr && !from_seed) {
        cv::Matx33d seed_rotation;
        cv::Rodrigues(seed->rvec, seed_rotation);
        const bool discontinuous =
            !cheirality_ok || rotation_angle_deg_between(rotation, seed_rotation) > kMirrorArbitrationDeg;
        if (discontinuous) {
            cv::Vec3d seeded_rvec = seed->rvec;
            cv::Vec3d seeded_tvec = seed->tvec;
            bool seeded_solved = false;
            try {
                seeded_solved = cv::solvePnP(object_points, image_points, camera, cv::noArray(), seeded_rvec,
                                             seeded_tvec, true, cv::SOLVEPNP_ITERATIVE);
            } catch (const cv::Exception&) {
                seeded_solved = false;
            }
            if (seeded_solved) {
                cv::Matx33d seeded_rotation;
                cv::Rodrigues(seeded_rvec, seeded_rotation);
                if (passes_cheirality(seeded_rotation, seeded_tvec, kAllCorners) &&
                    max_reprojection_fraction(object_points, image_points, seeded_rvec, seeded_tvec, camera) <=
                        kMaxReprojectionFraction) {
                    rvec = seeded_rvec;
                    tvec = seeded_tvec;
                    rotation = seeded_rotation;
                    cheirality_ok = true;
                    from_seed = true;
                }
            }
        }
    }
    if (!cheirality_ok) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    std::vector<cv::Point2d> projected;
    cv::projectPoints(kAllCorners, rvec, tvec, camera, cv::noArray(), projected);

    CubePoseProjection result;
    // The pose that just passed cheirality IS the orientation - only the
    // axis convention needs reconciling (see kOpenCVToDisplayFlip).
    result.orientation = display_quaternion(rotation);
    result.orientation_valid = true;
    result.rvec = rvec;
    result.tvec = tvec;
    result.from_seed = from_seed;
    result.max_reproj_fraction = max_reprojection_fraction(object_points, image_points, rvec, tvec, camera);
    for (std::size_t i = 0; i < result.vertices.size(); ++i) {
        result.vertices[i] =
            cv::Point2f(static_cast<float>(projected[i].x), static_cast<float>(projected[i].y));
    }
    if (!projection_is_plausible(result.vertices, frame_size)) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    return result;
}

Result<CubePoseProjection> project_cube_from_corner(const CornerObservation& observation, cv::Size frame_size,
                                                    const PoseSeed* seed) {
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
    // solvePnP needed. The coincidences below are identities on QUAD-CORNER
    // LABELS, not on screen positions: each view's tl/tr/br/bl follow from
    // its cell bases (quad corner = anchor -+ 1.5 col_step -+ 1.5 row_step),
    // and the corner topology fixes where each face's bases point relative
    // to the shared vertex - the top face's +col/+row advance toward it,
    // the hanging faces' rows advance away from the top face, the left
    // one's columns toward the shared vertex and the right one's away. That
    // convention is what observe_corner's bases encode on BOTH paths (the
    // fixed isometric directions, or a CornerHint's pose-predicted ones -
    // compute_corner_hint derives them from this same topology), so the
    // identities hold for any corner presentation, however rotated on
    // screen, in U/F/R vertex names of the view's model frame:
    //
    //   URF (shared near corner): top.br = lower_left.tr = lower_right.tl
    //   UFL (top-left edge):      top.bl = lower_left.tl
    //   UBR (top-right edge):     top.tr = lower_right.tr
    //   DRF (bottom shared edge): lower_left.br = lower_right.bl
    //   UBL / DLF / DRB:          top.tl / lower_left.bl / lower_right.br
    //
    // Coincident estimates are averaged so small detector disagreement is
    // split rather than favouring one face's fit. A labelling that does NOT
    // satisfy the identities (a mis-hinted or degenerate fit) constructs a
    // self-inconsistent vertex set, which the reprojection gate on the pose
    // solve below rejects rather than exporting.
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

    // One gated fit attempt: solve (optionally initialised from a seed),
    // then hold the answer to cheirality (a fit that puts corners behind
    // the camera is a mirror-ambiguous solution, not a pose) and to the
    // reprojection bar - the constructed quads need not be consistent with
    // ANY true perspective projection of a cube (each detector fits its
    // face independently), so the least-squares fit can be arbitrarily
    // wrong while still converging; a genuine pose puts every corner back
    // within a modest fraction of the on-screen radius, a garbage fit
    // scatters them.
    struct AcceptedFit {
        cv::Vec3d rvec;
        cv::Vec3d tvec;
        cv::Matx33d rotation;
        double reproj_fraction = 0.0;
    };
    const auto attempt_fit = [&](const PoseSeed* init) -> std::optional<AcceptedFit> {
        cv::Vec3d rvec;
        cv::Vec3d tvec;
        if (init != nullptr) {
            rvec = init->rvec;
            tvec = init->tvec;
        }
        bool solved = false;
        try {
            solved = cv::solvePnP(kCornerModel, corner_images, camera, cv::noArray(), rvec, tvec,
                                  init != nullptr, cv::SOLVEPNP_ITERATIVE);
        } catch (const cv::Exception&) {
            solved = false;
        }
        if (!solved) {
            return std::nullopt;
        }
        cv::Matx33d rotation;
        cv::Rodrigues(rvec, rotation);
        if (!passes_cheirality(rotation, tvec, kCornerModel)) {
            return std::nullopt;
        }
        const double fraction = max_reprojection_fraction(kCornerModel, corner_images, rvec, tvec, camera);
        if (fraction > kMaxReprojectionFraction) {
            return std::nullopt;
        }
        return AcceptedFit{rvec, tvec, rotation, fraction};
    };

    // Seed first (temporal continuity steers the seven-point solve out of
    // the cold start's occasional wrong local minimum), cold start second
    // - so a stale or garbage seed costs one extra bounded solve and
    // nothing else, and the null-seed path is exactly the historical one.
    std::optional<AcceptedFit> fit;
    bool from_seed = false;
    if (seed != nullptr) {
        fit = attempt_fit(seed);
        from_seed = fit.has_value();
    }
    if (!fit.has_value()) {
        fit = attempt_fit(nullptr);
    }

    if (fit.has_value()) {
        result.orientation = display_quaternion(fit->rotation);
        result.rvec = fit->rvec;
        result.tvec = fit->tvec;
        result.max_reproj_fraction = fit->reproj_fraction;
        result.from_seed = from_seed;
    }
    // Honest fallback per the header: no robust basis means identity +
    // invalid, never a guess - the UI's idle rotation takes over.
    result.orientation_valid = fit.has_value();
    return result;
}

Result<CubePoseProjection> project_cube_from_edge(const EdgeObservation& observation, cv::Size frame_size,
                                                  const PoseSeed* seed) {
    if (frame_size.width <= 0 || frame_size.height <= 0) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    if (quad_area(observation.left_quad) < kMinQuadAreaPx2 || quad_area(observation.right_quad) < kMinQuadAreaPx2) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    // Centred unit-cube model in OpenCV-style axes (x right, y down, z into
    // the screen), oriented like the corner path's kCornerModel: display U
    // is y = -0.5, display F is z = -0.5, display R is x = +0.5. The left
    // quad plays F (tl,tr,br,bl = UFL,URF,DRF,DLF), the right plays R
    // (URF,UBR,DRB,DRF); the shared URF/DRF vertices are averaged so small
    // detector disagreement splits instead of favouring one face's fit.
    static const std::vector<cv::Point3d> kEdgeModel = {
        {-0.5, -0.5, -0.5},  // UFL
        {0.5, -0.5, -0.5},   // URF (shared, top)
        {0.5, -0.5, 0.5},    // UBR
        {-0.5, 0.5, -0.5},   // DLF
        {0.5, 0.5, -0.5},    // DRF (shared, bottom)
        {0.5, 0.5, 0.5},     // DRB
    };
    const std::array<cv::Point2f, 4>& left = observation.left_quad;
    const std::array<cv::Point2f, 4>& right = observation.right_quad;
    const cv::Point2f v_urf = 0.5F * (left[1] + right[0]);
    const cv::Point2f v_drf = 0.5F * (left[2] + right[3]);
    const std::vector<cv::Point2d> image_points = {cv::Point2d(left[0]), cv::Point2d(v_urf),
                                                   cv::Point2d(right[1]), cv::Point2d(left[3]),
                                                   cv::Point2d(v_drf),   cv::Point2d(right[2])};

    const cv::Matx33d camera = approximate_camera_matrix(frame_size);

    struct AcceptedFit {
        cv::Vec3d rvec;
        cv::Vec3d tvec;
        cv::Matx33d rotation;
        double reproj_fraction = 0.0;
    };
    const auto attempt_fit = [&](const cv::Vec3d* init_rvec, const cv::Vec3d* init_tvec) -> std::optional<AcceptedFit> {
        cv::Vec3d rvec;
        cv::Vec3d tvec;
        const bool use_guess = init_rvec != nullptr;
        if (use_guess) {
            rvec = *init_rvec;
            tvec = *init_tvec;
        }
        bool solved = false;
        try {
            solved = cv::solvePnP(kEdgeModel, image_points, camera, cv::noArray(), rvec, tvec, use_guess,
                                  cv::SOLVEPNP_ITERATIVE);
        } catch (const cv::Exception&) {
            solved = false;
        }
        if (!solved) {
            return std::nullopt;
        }
        cv::Matx33d rotation;
        cv::Rodrigues(rvec, rotation);
        if (!passes_cheirality(rotation, tvec, kEdgeModel)) {
            return std::nullopt;
        }
        const double fraction = max_reprojection_fraction(kEdgeModel, image_points, rvec, tvec, camera);
        if (fraction > kMaxReprojectionFraction) {
            return std::nullopt;
        }
        return AcceptedFit{rvec, tvec, rotation, fraction};
    };

    // Canonical initial guess: the symmetric edge pose every genuine edge
    // detection is near BY CONSTRUCTION (the detector only accepts
    // left/right column grids meeting at a near-vertical edge) - display
    // yaw of -45 degrees about +y, i.e. F leaning screen-left and R
    // screen-right toward the viewer, a cube's depth out. Derived through
    // opencv_rotation_from_display so the guess can never drift from the
    // axis-reconciliation convention.
    static const cv::Vec3d kCanonicalTvec(0.0, 0.0, 3.0);
    const cv::Vec3d canonical_rvec = [] {
        constexpr float kHalfYawRad = -22.5F * 3.14159265358979F / 180.0F;
        const std::array<float, 4> q{std::cos(kHalfYawRad), 0.0F, std::sin(kHalfYawRad), 0.0F};
        cv::Vec3d rvec;
        cv::Rodrigues(opencv_rotation_from_display(q), rvec);
        return rvec;
    }();

    std::optional<AcceptedFit> fit;
    bool from_seed = false;
    if (seed != nullptr) {
        fit = attempt_fit(&seed->rvec, &seed->tvec);
        from_seed = fit.has_value();
    }
    if (!fit.has_value()) {
        fit = attempt_fit(&canonical_rvec, &kCanonicalTvec);
    }
    if (!fit.has_value()) {
        fit = attempt_fit(nullptr, nullptr);
    }
    if (!fit.has_value()) {
        // No independent vertex construction exists for this path (the six
        // pinned points ARE the observation), so an unexplainable pose is
        // an outright failure - never an invalid-orientation projection.
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    // Full-cube vertices for the wireframe, header order with the LEFT
    // (F-role) face as the near face: 0-3 its tl,tr,br,bl, 4-7 each
    // vertex's rear counterpart.
    static const std::vector<cv::Point3d> kEdgeWireframe = {
        {-0.5, -0.5, -0.5},  // UFL
        {0.5, -0.5, -0.5},   // URF
        {0.5, 0.5, -0.5},    // DRF
        {-0.5, 0.5, -0.5},   // DLF
        {-0.5, -0.5, 0.5},   // UBL
        {0.5, -0.5, 0.5},    // UBR
        {0.5, 0.5, 0.5},     // DRB
        {-0.5, 0.5, 0.5},    // DLB
    };
    std::vector<cv::Point2d> projected;
    cv::projectPoints(kEdgeWireframe, fit->rvec, fit->tvec, camera, cv::noArray(), projected);

    CubePoseProjection result;
    for (std::size_t i = 0; i < result.vertices.size(); ++i) {
        result.vertices[i] = cv::Point2f(static_cast<float>(projected[i].x), static_cast<float>(projected[i].y));
    }
    if (!projection_is_plausible(result.vertices, frame_size)) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    result.orientation = display_quaternion(fit->rotation);
    result.orientation_valid = true;
    result.rvec = fit->rvec;
    result.tvec = fit->tvec;
    result.max_reproj_fraction = fit->reproj_fraction;
    result.from_seed = from_seed;
    return result;
}

}  // namespace cube::vision
