#include "controllers/live_track.hpp"

#include "controllers/bundled_models.hpp"

#include <cube/track_m3t/m3t_pose_tracker.hpp>
#include <cube/vision/camera_intrinsics.hpp>
#include <cube/vision/corner_detector.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/pipeline/detect_track_loop.hpp>
#include <cube/vision/pipeline/face_projection.hpp>

#include <opencv2/calib3d.hpp>

#include <QByteArray>
#include <QString>
#include <QtLogging>

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

namespace cube::app::live_track {

namespace {

// Set, for good, when the corner model cannot load: the pipeline then runs
// the legacy cascade instead. Worker-side, single-in-flight.
bool track_mode_degraded = false;

}  // namespace

bool track_mode() {
    static const bool legacy_requested =
        qEnvironmentVariable("CUBE_VISION_PIPELINE") == QStringLiteral("legacy");
    return !legacy_requested && !track_mode_degraded;
}

// Worker-side statics: single-in-flight analysis means no concurrent passes,
// the same story as ScanSession's recorder. The detector model comes from
// the embedded resource (bundled_models), the tracker from cube::track_m3t;
// a load failure warns once and degrades the app to the legacy pipeline.
Sample step(const cv::Mat& analysis_frame, float scale_back) {
    static cube::vision::pipeline::DetectTrackLoop loop;
    static std::unique_ptr<cube::vision::OnnxCornerDetector> detector;
    static std::unique_ptr<cube::track_m3t::M3tPoseTracker> tracker;
    static bool init_attempted = false;
    if (!init_attempted) {
        init_attempted = true;
        const QByteArray bytes = cube::app::cube_corner_pose_model_bytes();
        if (!bytes.isEmpty()) {
            if (auto created = cube::vision::OnnxCornerDetector::create_from_memory(
                    bytes.constData(), static_cast<std::size_t>(bytes.size()));
                created.has_value()) {
                detector = std::move(*created);
                tracker = std::make_unique<cube::track_m3t::M3tPoseTracker>();
                loop.attach(detector.get(), tracker.get());
                // Detector every 3rd pass while locked: the M3T step is
                // ~1-2 ms, the ONNX detector tens of ms - this is what
                // buys live tracking cadence (the offline runner keeps
                // every-frame detection for its metrics).
                loop.set_live_detect_interval(3);
                // Measured camera intrinsics (P2.9): point at the file
                // the calibration tool wrote and every camera-matrix
                // consumer in the loop leaves the f=width approximation.
                if (const QString intrinsics_path = qEnvironmentVariable("CUBE_VISION_INTRINSICS");
                    !intrinsics_path.isEmpty()) {
                    if (auto intrinsics =
                            cube::vision::read_intrinsics(intrinsics_path.toStdString());
                        intrinsics.has_value()) {
                        loop.set_camera_intrinsics(*intrinsics);
                        qInfo("ScanSession: calibrated intrinsics loaded (fx %.1f fy %.1f from %s)",
                              intrinsics->fx, intrinsics->fy, qUtf8Printable(intrinsics_path));
                    } else {
                        qWarning("ScanSession: CUBE_VISION_INTRINSICS set but unreadable: %s",
                                 qUtf8Printable(intrinsics_path));
                    }
                }
            }
        }
        if (!loop.attached()) {
            qWarning(
                "ScanSession: corner model could not be loaded - degrading to the legacy pipeline "
                "(CUBE_VISION_PIPELINE=legacy behaviour)");
            track_mode_degraded = true;
        }
    }
    if (!loop.attached()) {
        return {};
    }

    const auto dt = loop.process(analysis_frame);
    Sample sample;
    sample.health_frac = dt.health_frac;
    sample.reinits = dt.reinits;
    if (!dt.track_live || !dt.step_valid) {
        return sample;
    }
    sample.locked = true;
    sample.gauge_epoch = dt.gauge_epoch;
    sample.tvec = dt.tvec;
    cube::vision::CubePoseProjection pose;
    for (std::size_t i = 0; i < 8; ++i) {
        pose.vertices[i] = dt.projected[i] * scale_back;  // analysis -> full-frame px
    }
    cv::Matx33d rotation;
    cv::Rodrigues(dt.rvec, rotation);
    pose.orientation = cube::vision::display_quaternion(rotation);
    pose.orientation_valid = true;
    // Seed-compatible-ish extrinsics: the tracked pose is over the
    // Singmaster half-edge-1 model; kCubeModel's scale differs, so a
    // downstream seeded solve degrades gracefully (documented behaviour)
    // rather than being biased by a silently-wrong scale.
    pose.rvec = dt.rvec;
    pose.tvec = dt.tvec * 0.5;
    pose.max_reproj_fraction = dt.health_frac >= 0.0 ? dt.health_frac : 0.0;
    pose.from_seed = false;
    sample.pose = pose;

    // P6.12: the runner's stage D-F track-geometry composition, run live -
    // this is the wiring the live-demo diagnosis measured as absent (track
    // mode returned here with pose only, and no observation ever reached
    // any capture surface). Per capture-grade derived face (geometric
    // frontality AND the loop's tracker-quality read grade, the P6.2
    // gate): rectify at analysis scale, re-fit the internal cell grid
    // where the rectification holds (P4 sub-pixel polish, clamped
    // never-re-seat), then sample each sticker quad with the exonerated
    // quad-masked Lab-median estimator. Purity rides along; 0 stays "not
    // measured" and fails closed at the feed's centre bar. Mirrors
    // legacy_pipeline.cpp's track_geometry branch, which the offline
    // runner replays - the two must produce the same reads or the corpus
    // stops being evidence about the app.
    for (const auto& derived : dt.faces) {
        if (!(derived.frontality >= cube::vision::fusion::kTrackCaptureFrontality &&
              dt.read_grade)) {
            continue;
        }
        cv::Mat homography;
        const cv::Mat rectified =
            cube::vision::pipeline::rectify_face(analysis_frame, derived, 96, &homography);
        std::array<std::array<cv::Point2f, 4>, 9> cells = derived.cells;
        if (!rectified.empty() && !homography.empty()) {
            const auto grid = cube::vision::pipeline::refine_cell_grid(rectified);
            if (grid.valid) {
                cells = cube::vision::pipeline::cells_from_grid(grid, homography);
            }
        }
        cube::vision::fusion::TrackedFaceRead read;
        read.face = derived.face;
        for (std::size_t cell = 0; cell < read.lab.size(); ++cell) {
            const auto stats = cube::vision::sample_cell_patch(analysis_frame, cells[cell]);
            if (stats.quad_px > 0) {
                read.lab[cell] = stats.lab_median;
                read.purity[cell] = stats.purity;
            }
        }
        sample.faces.push_back(std::move(read));
    }
    return sample;
}

}  // namespace cube::app::live_track
