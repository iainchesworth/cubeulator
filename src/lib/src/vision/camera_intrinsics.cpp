#include <cube/vision/camera_intrinsics.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace cube::vision {

CameraIntrinsics CameraIntrinsics::scaled_to(cv::Size size) const {
    if (calibrated_size.width <= 0 || calibrated_size.height <= 0) {
        return *this;
    }
    const double scale_x = static_cast<double>(size.width) / calibrated_size.width;
    const double scale_y = static_cast<double>(size.height) / calibrated_size.height;
    CameraIntrinsics out = *this;
    out.fx = fx * scale_x;
    out.fy = fy * scale_y;
    out.cx = cx * scale_x;
    out.cy = cy * scale_y;
    out.calibrated_size = size;
    return out;
}

std::optional<std::string> write_intrinsics(const std::string& path, const CameraIntrinsics& value) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return "cannot write " + path;
    }
    // max_digits10: doubles must survive the text round-trip exactly - a
    // calibration file that quietly loses precision defeats its purpose.
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "fx " << value.fx << "\nfy " << value.fy << "\ncx " << value.cx << "\ncy " << value.cy << "\n";
    out << "distortion";
    for (const double d : value.distortion) {
        out << ' ' << d;
    }
    out << "\nsize " << value.calibrated_size.width << ' ' << value.calibrated_size.height << "\n";
    out << "rms " << value.rms_reprojection << "\n";
    return std::nullopt;
}

Result<CameraIntrinsics> read_intrinsics(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(CubeError::model_load_failed);
    }
    CameraIntrinsics value;
    std::string key;
    while (in >> key) {
        if (key == "fx") {
            in >> value.fx;
        } else if (key == "fy") {
            in >> value.fy;
        } else if (key == "cx") {
            in >> value.cx;
        } else if (key == "cy") {
            in >> value.cy;
        } else if (key == "distortion") {
            for (double& d : value.distortion) {
                in >> d;
            }
        } else if (key == "size") {
            in >> value.calibrated_size.width >> value.calibrated_size.height;
        } else if (key == "rms") {
            in >> value.rms_reprojection;
        } else {
            std::string skip;
            std::getline(in, skip);
        }
    }
    if (value.fx <= 0.0 || value.fy <= 0.0 || value.calibrated_size.width <= 0) {
        return std::unexpected(CubeError::model_load_failed);
    }
    return value;
}

Result<CameraIntrinsics> calibrate_from_views(const std::vector<cv::Mat>& bgr_views, cv::Size inner_corners,
                                              float square_size, int* views_used) {
    std::vector<std::vector<cv::Point3f>> object_points;
    std::vector<std::vector<cv::Point2f>> image_points;
    std::vector<cv::Point3f> board;
    for (int row = 0; row < inner_corners.height; ++row) {
        for (int col = 0; col < inner_corners.width; ++col) {
            board.emplace_back(static_cast<float>(col) * square_size, static_cast<float>(row) * square_size, 0.0F);
        }
    }

    cv::Size frame_size;
    for (const auto& view : bgr_views) {
        if (view.empty()) {
            continue;
        }
        frame_size = view.size();
        cv::Mat gray;
        cv::cvtColor(view, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Point2f> corners;
        if (!cv::findChessboardCorners(gray, inner_corners, corners,
                                       cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE)) {
            continue;
        }
        cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
                         cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.001));
        object_points.push_back(board);
        image_points.push_back(std::move(corners));
    }
    if (views_used != nullptr) {
        *views_used = static_cast<int>(image_points.size());
    }
    if (image_points.size() < 4) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }

    CameraIntrinsics result;
    cv::Mat camera = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat distortion = cv::Mat::zeros(5, 1, CV_64F);
    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;
    try {
        result.rms_reprojection =
            cv::calibrateCamera(object_points, image_points, frame_size, camera, distortion, rvecs, tvecs);
    } catch (const cv::Exception&) {
        return std::unexpected(CubeError::no_valid_cube_detected);
    }
    result.fx = camera.at<double>(0, 0);
    result.fy = camera.at<double>(1, 1);
    result.cx = camera.at<double>(0, 2);
    result.cy = camera.at<double>(1, 2);
    for (int i = 0; i < 5; ++i) {
        result.distortion[static_cast<std::size_t>(i)] = distortion.at<double>(i, 0);
    }
    result.calibrated_size = frame_size;
    return result;
}

}  // namespace cube::vision
