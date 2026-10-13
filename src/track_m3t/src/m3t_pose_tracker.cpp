#include <cube/track_m3t/m3t_pose_tracker.hpp>

#include <m3t/body.h>
#include <m3t/camera.h>
#include <m3t/common.h>
#include <m3t/link.h>
#include <m3t/optimizer.h>
#include <m3t/region_modality.h>
#include <m3t/region_model.h>
#include <m3t/tracker.h>

#include <Eigen/Geometry>

#include <opencv2/calib3d.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace cube::track_m3t {

namespace {

// The seam speaks half-edge = 1; the CAD model is the classic 57 mm cube.
constexpr float kEdgeMetres = 0.057F;
constexpr double kScale = kEdgeMetres / 2.0;  // seam units -> metres

// The cube CAD model, written once per process (binary vertex order, CCW
// winding, metres) - same recipe the vendored smoke test uses.
std::filesystem::path ensure_cube_obj() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "cube_m3t_runtime";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "cube_57mm.obj";
    if (!std::filesystem::exists(path)) {
        std::ofstream out(path);
        const float h = kEdgeMetres / 2.0F;
        for (int i = 0; i < 8; ++i) {
            out << "v " << (((i & 1) != 0) ? h : -h) << ' ' << (((i & 2) != 0) ? h : -h) << ' '
                << (((i & 4) != 0) ? h : -h) << '\n';
        }
        out << "f 5 6 7\nf 5 7 8\nf 2 1 4\nf 2 4 3\nf 6 2 3\nf 6 3 7\nf 1 5 8\nf 1 8 4\nf 8 7 3\nf "
               "8 3 4\n"
            << "f 1 2 6\nf 1 6 5\n";
    }
    return path;
}

// In-memory colour camera: the pipeline hands frames in; no device.
class InMemColorCamera final : public m3t::ColorCamera {
public:
    InMemColorCamera(const m3t::Intrinsics& intrinsics) : ColorCamera{"pipeline_camera"} {
        intrinsics_ = intrinsics;
    }
    bool SetUp() override {
        set_up_ = true;
        return true;
    }
    void set_frame(const cv::Mat& frame) { pending_ = frame; }
    bool UpdateImage(bool /*synchronized*/) override {
        image_ = pending_;
        SaveImageIfDesired();
        return true;
    }

private:
    cv::Mat pending_;
};

m3t::Transform3fA to_m3t_pose(const cv::Vec3d& rvec, const cv::Vec3d& tvec) {
    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);
    Eigen::Matrix3f r;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r(row, col) = static_cast<float>(rotation(row, col));
        }
    }
    m3t::Transform3fA pose = m3t::Transform3fA::Identity();
    pose.linear() = r;
    pose.translation() =
        Eigen::Vector3f(static_cast<float>(tvec[0] * kScale), static_cast<float>(tvec[1] * kScale),
                        static_cast<float>(tvec[2] * kScale));
    return pose;
}

void from_m3t_pose(const m3t::Transform3fA& pose, cv::Vec3d& rvec, cv::Vec3d& tvec) {
    cv::Matx33d rotation;
    const Eigen::Matrix3f r = pose.rotation();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            rotation(row, col) = static_cast<double>(r(row, col));
        }
    }
    cv::Rodrigues(rotation, rvec);
    const Eigen::Vector3f t = pose.translation();
    tvec = cv::Vec3d(static_cast<double>(t.x()) / kScale, static_cast<double>(t.y()) / kScale,
                     static_cast<double>(t.z()) / kScale);
}

}  // namespace

struct M3tPoseTracker::Impl {
    std::shared_ptr<m3t::Body> body;
    std::shared_ptr<InMemColorCamera> camera;
    std::shared_ptr<m3t::Tracker> tracker;
    cv::Size frame_size{};
    int iteration = 0;
    bool ready = false;
    bool reinit_pending = false;

    bool build(cv::Size size, const cv::Matx33d& camera_matrix) {
        const std::filesystem::path scratch =
            std::filesystem::temp_directory_path() / "cube_m3t_runtime";
        std::filesystem::create_directories(scratch);
        const auto obj_path = ensure_cube_obj();

        body = std::make_shared<m3t::Body>("cube", obj_path, 1.0F, true, true,
                                           m3t::Transform3fA::Identity());
        const m3t::Intrinsics intrinsics{static_cast<float>(camera_matrix(0, 0)),
                                         static_cast<float>(camera_matrix(1, 1)),
                                         static_cast<float>(camera_matrix(0, 2)),
                                         static_cast<float>(camera_matrix(1, 2)),
                                         size.width,
                                         size.height};
        camera = std::make_shared<InMemColorCamera>(intrinsics);
        // Spike-reduced region-model generation (cached; use_random_seed
        // false - the determinism contract).
        auto region_model = std::make_shared<m3t::RegionModel>(
            "region_model", body, scratch / "cube_region_model.bin", 0.8F, 3, 200, 0.05F, 0.002F,
            false, 1000);
        auto region_modality =
            std::make_shared<m3t::RegionModality>("region_modality", body, camera, region_model);
        auto link = std::make_shared<m3t::Link>("link", body);
        link->AddModality(region_modality);
        auto optimizer = std::make_shared<m3t::Optimizer>("optimizer", link);
        tracker = std::make_shared<m3t::Tracker>("tracker");
        tracker->AddOptimizer(optimizer);
        frame_size = size;
        return true;
    }
};

M3tPoseTracker::M3tPoseTracker() : impl_(std::make_unique<Impl>()) {}
M3tPoseTracker::~M3tPoseTracker() = default;

bool M3tPoseTracker::init(const cv::Vec3d& rvec, const cv::Vec3d& tvec, cv::Size frame_size,
                          const cv::Matx33d& camera_matrix) {
    try {
        if (impl_->tracker == nullptr || impl_->frame_size != frame_size) {
            impl_->ready = false;
            if (!impl_->build(frame_size, camera_matrix)) {
                return false;
            }
        }
        impl_->body->set_body2world_pose(to_m3t_pose(rvec, tvec));
        if (!impl_->ready) {
            // First-time SetUp needs a frame in the camera before the
            // modality initialises; feed a placeholder-free path instead:
            // SetUp() here only wires components (GL context, region model
            // load/generate) - histograms initialise on the first
            // ExecuteStartingStep after a real frame arrives in step().
            if (!impl_->tracker->SetUp()) {
                return false;
            }
            impl_->tracker->StartTracking();
            impl_->ready = true;
        }
        // Re-init: histogram re-seed happens on the next step() via
        // ExecuteStartingStep at the freshly set pose.
        impl_->reinit_pending = true;
        return true;
    } catch (const std::exception&) {
        impl_->ready = false;
        return false;
    }
}

cube::vision::pipeline::TrackedPose M3tPoseTracker::step(const cv::Mat& bgr_frame) {
    cube::vision::pipeline::TrackedPose result;
    if (!impl_->ready || bgr_frame.empty()) {
        return result;
    }
    try {
        impl_->camera->set_frame(bgr_frame);
        const int i = impl_->iteration++;
        if (!impl_->tracker->UpdateCameras(i)) {
            return result;
        }
        if (impl_->reinit_pending) {
            if (!impl_->tracker->ExecuteStartingStep(i)) {
                return result;
            }
            impl_->reinit_pending = false;
        }
        if (!impl_->tracker->ExecuteTrackingStep(i)) {
            return result;
        }
        from_m3t_pose(impl_->body->body2world_pose(), result.rvec, result.tvec);
        result.valid = true;
        return result;
    } catch (const std::exception&) {
        return result;
    }
}

void M3tPoseTracker::reset() {
    impl_ = std::make_unique<Impl>();
}

bool M3tPoseTracker::initialized() const {
    return impl_->ready;
}

}  // namespace cube::track_m3t
