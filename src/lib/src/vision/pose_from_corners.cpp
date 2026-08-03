#include <cube/vision/pose_from_corners.hpp>

#include <cube/vision/pose_estimation.hpp>

#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <vector>

namespace cube::vision {

Result<CornerPnpResult> estimate_pose_from_corners(const CornerDetectionResult& detection, cv::Size frame_size,
                                                   float min_confidence) {
    std::vector<cv::Point3f> model_points;
    std::vector<cv::Point2f> image_points;
    for (int id = 0; id < 8; ++id) {
        const auto& corner = detection.corners[static_cast<std::size_t>(id)];
        if (corner.confidence < min_confidence) {
            continue;
        }
        model_points.push_back(kSingmasterCornerModel[static_cast<std::size_t>(id)]);
        image_points.push_back(corner.px);
    }
    if (model_points.size() < 4) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    const cv::Matx33d camera = approximate_camera_matrix(frame_size);
    CornerPnpResult result;
    // SQPnP: globally optimal for arbitrary (non-coplanar) configurations
    // down to 4 points - exactly this shape of problem. A Levenberg-
    // Marquardt polish costs microseconds and tightens the reprojection.
    // The try/catch is load-bearing: on degenerate input (e.g. collapsed
    // points from a garbage detection) OpenCV ASSERTS inside sqpnp rather
    // than returning false, and a detector hiccup must degrade to
    // "no pose", never to an exception through the pipeline.
    try {
        if (!cv::solvePnP(model_points, image_points, camera, cv::noArray(), result.rvec, result.tvec,
                          /*useExtrinsicGuess=*/false, cv::SOLVEPNP_SQPNP)) {
            return std::unexpected(CubeError::no_valid_cube_detected);
        }
        cv::solvePnPRefineLM(model_points, image_points, camera, cv::noArray(), result.rvec, result.tvec);
    } catch (const cv::Exception&) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    // Cheirality: every used corner must sit in front of the camera.
    cv::Matx33d rotation;
    cv::Rodrigues(result.rvec, rotation);
    for (const auto& point : model_points) {
        const cv::Matx31d camera_point =
            rotation * cv::Matx31d(point.x, point.y, point.z) + cv::Matx31d(result.tvec[0], result.tvec[1], result.tvec[2]);
        if (camera_point(2, 0) <= 0.0) {
            return std::unexpected(CubeError::no_valid_cube_detected);
        }
    }

    std::vector<cv::Point2f> reprojected;
    cv::projectPoints(model_points, result.rvec, result.tvec, camera, cv::noArray(), reprojected);
    double total = 0.0;
    for (std::size_t i = 0; i < reprojected.size(); ++i) {
        const double err = cv::norm(reprojected[i] - image_points[i]);
        total += err;
        result.max_reproj_px = std::max(result.max_reproj_px, err);
    }
    result.mean_reproj_px = total / static_cast<double>(reprojected.size());
    result.corners_used = static_cast<int>(model_points.size());
    result.orientation = display_quaternion(rotation);
    return result;
}

const std::vector<std::array<int, 8>>& singmaster_rotation_permutations() {
    static const std::vector<std::array<int, 8>> perms = [] {
        const auto perm_of = [](const cv::Matx33f& rotation) {
            std::array<int, 8> perm{};
            for (int i = 0; i < 8; ++i) {
                const cv::Point3f p = kSingmasterCornerModel[static_cast<std::size_t>(i)];
                const cv::Matx31f rotated = rotation * cv::Matx31f(p.x, p.y, p.z);
                for (int j = 0; j < 8; ++j) {
                    const cv::Point3f q = kSingmasterCornerModel[static_cast<std::size_t>(j)];
                    if (cv::norm(cv::Point3f(rotated(0), rotated(1), rotated(2)) - q) < 0.1F) {
                        perm[static_cast<std::size_t>(i)] = j;
                        break;
                    }
                }
            }
            return perm;
        };
        // Closure over two quarter-turn generators reaches all 24 proper
        // rotations of the cube.
        const cv::Matx33f rot_y(0, 0, 1, 0, 1, 0, -1, 0, 0);
        const cv::Matx33f rot_z(0, -1, 0, 1, 0, 0, 0, 0, 1);
        std::vector<cv::Matx33f> rotations{cv::Matx33f::eye()};
        for (std::size_t i = 0; i < rotations.size(); ++i) {
            for (const auto& generator : {rot_y, rot_z}) {
                const cv::Matx33f candidate = generator * rotations[i];
                const bool known = std::any_of(rotations.begin(), rotations.end(), [&](const cv::Matx33f& r) {
                    return cv::norm(r - candidate) < 0.1;
                });
                if (!known) {
                    rotations.push_back(candidate);
                }
            }
        }
        std::vector<std::array<int, 8>> out;
        out.reserve(rotations.size());
        for (const auto& rotation : rotations) {
            out.push_back(perm_of(rotation));
        }
        return out;
    }();
    return perms;
}

}  // namespace cube::vision
