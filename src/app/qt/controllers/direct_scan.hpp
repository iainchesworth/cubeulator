#pragma once

#include "interfaces/camera.hpp"

#include <cube/diagnostics/json.hpp>
#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/recording_v2.hpp>

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <opencv2/core.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace cube::app {

class Diagnostics;

// The scan screen's controller: camera frames in, the cube out.
//
// Each frame goes through cube::vision::read_side and into a SideScan; the
// screen shows where the stickers were found, a chip for every side read so
// far, and - when six sides make one valid cube - scanCompleted. There is no
// pose tracker, no detector model and no scan mode to choose: what is on the
// screen is exactly what the reader has (see docs/recovery-plan.md).
//
// Once the cube is read the camera has a second job. The solve screen keeps
// it running and asks only to be told what side is in view (watch()):
// HeldCube matches that against the cube as it should now stand, to draw the
// cube the way it is held. Nothing is added to the scan while watching, and
// the exposure and white balance the scan fixed stay fixed, so a sticker
// still measures what it measured then.
//
// Everything here runs on the GUI thread. A frame's read is a few
// milliseconds, well inside the poll interval, so there is no worker and no
// state to hand between threads; only latestFrame() is called from elsewhere
// (the preview image provider).
class DirectScan : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool starting READ starting NOTIFY runningChanged)
    // Why the camera is not running, in words for the user. Empty when it is.
    Q_PROPERTY(QString cameraError READ cameraError NOTIFY runningChanged)

    // Bumped per frame so the preview Image re-requests its source.
    Q_PROPERTY(int frameSeq READ frameSeq NOTIFY frameUpdated)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameUpdated)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameUpdated)

    // The nine sticker centres found in the current frame, in frame pixels,
    // row-major as the camera sees them. Empty when no side is in view.
    Q_PROPERTY(QVariantList stickerPoints READ stickerPoints NOTIFY frameUpdated)
    // True when the side in view is being held still enough to count.
    Q_PROPERTY(bool reading READ reading NOTIFY frameUpdated)
    // 0..1 while a side that has not been captured yet is being held still:
    // how far it is towards counting. 0 otherwise.
    Q_PROPERTY(qreal captureProgress READ captureProgress NOTIFY frameUpdated)
    // True from camera start until its exposure and white balance are held.
    // Nothing is read meanwhile: a colour sampled while the balance is still
    // moving would not match the same sticker a second later.
    Q_PROPERTY(bool settingUp READ settingUp NOTIFY frameUpdated)
    // What the camera's exposure and white balance came to, as the lock
    // reported it - the record the diagnostics channel's `camera` event carries
    // as `lock`, in the words a screen or a test can use:
    //   reported      the camera has said (false from camera start until then)
    //   supported     the platform claims a lock on this camera
    //                 (photometry_control_supported(): only where one was
    //                 read back as holding)
    //   exposure, whiteBalance
    //                 "applied", "rejected", "unsupported" or "not_requested";
    //                 only "applied" is a control that was held
    //   summary       PhotometryLockReport::summary(), the one sentence that
    //                 never claims a control it did not verify. English and
    //                 for a developer: a screen that wants to tell the person
    //                 builds its own text from the two outcomes (docs/i18n.md)
    // exposure, whiteBalance and summary are absent until reported.
    Q_PROPERTY(QVariantMap lock READ lock NOTIFY frameUpdated)

    // One entry per side read so far, in the order first shown:
    // { colors: nine "#rrggbb" strings row-major as last shown,
    //   inView: bool }.
    // The colours are the ones measured, not names - what the camera saw.
    Q_PROPERTY(QVariantList sides READ sides NOTIFY sidesChanged)
    Q_PROPERTY(int sidesRead READ sidesRead NOTIFY sidesChanged)
    // Six sides are in but do not make a valid cube yet: one was misread, or
    // one side was captured twice under different light.
    Q_PROPERTY(bool conflicted READ conflicted NOTIFY sidesChanged)
    // Conflicted, and the reason is that the six sides fit together two ways
    // rather than none: showing each side again cannot help, but turning the
    // cube so that two sides show at once can.
    Q_PROPERTY(bool ambiguous READ ambiguous NOTIFY sidesChanged)
    // Where this scan's frames are being written, or empty. Empty unless a
    // developer has asked for recording (see setRecordingRoot): the app
    // stores nothing otherwise. The screen shows it whenever it is set, so a
    // recording is never silent.
    Q_PROPERTY(QString recordingRoot READ recordingRoot NOTIFY recordingRootChanged)
    // The cube has been read. The six sides stay on show - the last one
    // included - until whoever listens for scanCompleted moves on.
    Q_PROPERTY(bool complete READ complete NOTIFY sidesChanged)
    // While the scan is ambiguous: the three colours - "white", "yellow",
    // "green", "blue", "orange", "red", in no particular order - of the sides
    // that meet at the corner whose view would tell the cubes the six sides
    // make apart. Showing it, with two or three of its sides in one view, is
    // what settles the scan; the screen says it. Empty when the scan is not
    // ambiguous or no corner would help.
    Q_PROPERTY(QStringList settlingCorner READ settlingCorner NOTIFY sidesChanged)

    // The camera is being kept for the solve screen: sides are looked for in
    // every frame and reported (frameWatched), and none is added to the scan.
    Q_PROPERTY(bool watching READ watching NOTIFY runningChanged)

public:
    explicit DirectScan(QObject* parent = nullptr);
    ~DirectScan() override;

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] bool starting() const { return start_watcher_.isRunning(); }
    [[nodiscard]] QString cameraError() const { return camera_error_; }
    [[nodiscard]] int frameSeq() const noexcept { return frame_seq_; }
    [[nodiscard]] int frameWidth() const noexcept { return frame_width_; }
    [[nodiscard]] int frameHeight() const noexcept { return frame_height_; }
    [[nodiscard]] QVariantList stickerPoints() const;
    [[nodiscard]] bool reading() const noexcept { return reading_; }
    [[nodiscard]] qreal captureProgress() const noexcept { return capture_progress_; }
    [[nodiscard]] bool settingUp() const noexcept { return awaiting_lock_; }
    [[nodiscard]] QVariantMap lock() const;
    [[nodiscard]] QVariantList sides() const;
    [[nodiscard]] int sidesRead() const noexcept { return static_cast<int>(sides_.size()); }
    [[nodiscard]] bool conflicted() const noexcept { return conflicted_; }
    [[nodiscard]] bool ambiguous() const noexcept { return ambiguous_; }
    [[nodiscard]] bool complete() const noexcept { return scanned_.has_value(); }
    [[nodiscard]] QStringList settlingCorner() const { return settling_corner_; }
    [[nodiscard]] bool watching() const noexcept { return watching_; }
    [[nodiscard]] QString recordingRoot() const { return recording_root_; }

    // Developer recording, for building the corpus of real scans. Off by
    // default; turned on only by the CUBE_SCAN_RECORD_DIR environment
    // variable (read at construction) or by this call - there is no switch
    // for it in the app. While it is on, every scan writes its frames, at
    // the size the reader works at, to a new folder under `root` in the
    // format tools/scan replays. An empty root turns it off. Only a scan is
    // ever recorded: the solve screen's camera (watching) writes nothing.
    void setRecordingRoot(const QString& root);

    // The cube, once scanCompleted has fired for the current scan.
    [[nodiscard]] std::optional<cube::solver::Cube> cube() const {
        return scanned_.transform([](const cube::vision::ScannedCube& s) { return s.cube; });
    }
    // What each colour measured in that scan; later frames from the same
    // camera session are matched against these.
    [[nodiscard]] std::optional<cube::vision::SessionColours> colours() const {
        return scanned_.transform([](const cube::vision::ScannedCube& s) { return s.colours; });
    }

    // The side in the frame just processed, if there was one square to the
    // camera. While watching this is every frame's read, held still or not.
    [[nodiscard]] const std::optional<cube::vision::SideRead>& read() const noexcept {
        return read_;
    }

    // Turns watching on or off without touching the camera. watch() is what
    // the screens call; this is the part of it that needs no device.
    void setWatching(bool watching);

    // The most recent camera frame (BGR). Safe from any thread.
    [[nodiscard]] cv::Mat latestFrame() const;

    // The developer diagnostics channel (docs/diagnostics.md), or none. While
    // it is on, each new evaluation of the scan, and what the camera did with
    // the photometry lock, are written to it; while it is off nothing here
    // does any more than test a pointer and a bool.
    void setDiagnostics(Diagnostics* diagnostics) noexcept { diagnostics_ = diagnostics; }
    // Everything this controller knows, for a snapshot record: the scan as it
    // stands (SideScan::diagnose), the cube it made, the camera and its lock.
    [[nodiscard]] cube::diagnostics::Json diagnosticState() const;

    // Feeds one frame through the reader. poll() calls this for every camera
    // frame; it is public so the scan logic can be driven without a camera.
    // `t_ms` is when the frame was captured, kept with a recorded frame.
    void processFrame(const cv::Mat& bgr, std::int64_t t_ms = 0);

public slots:
    // Opens the camera and begins a fresh scan. `deviceId` is a
    // CameraController device identifier.
    void start(const QString& deviceId);
    // Keeps the camera for the solve screen: watching on, the scan and the
    // cube it produced left alone. A camera already running stays as it is,
    // lock and all; one that was closed (coming back from settings) is
    // opened again.
    void watch(const QString& deviceId);
    void stop();
    // Forgets every side read so far and scans again, camera still running.
    void restart();

signals:
    void runningChanged();
    void frameUpdated();
    void sidesChanged();
    void recordingRootChanged();
    // Six sides have been read and make exactly one valid cube: cube().
    void scanCompleted();
    // A frame has been looked at while watching: read() is its side, or
    // empty. Emitted after frameUpdated, once per frame.
    void frameWatched();

private:
    void poll();
    void handleStartFinished();
    void record(const cv::Mat& bgr, std::int64_t t_ms);
    void finishRecording();
    void openCamera(const QString& deviceId);
    // Evaluates the scan as scanned() does, and - the channel being on - writes
    // what the evaluation did, once for each distinct outcome.
    [[nodiscard]] cube::Result<cube::vision::ScannedCube> evaluateScan(std::string_view trigger);
    // One `camera` record: `event`, the device, the frame size, the lock.
    void noteCamera(std::string_view event) const;
    [[nodiscard]] bool diagnosing() const noexcept;

    QTimer poll_timer_;
    QFutureWatcher<cube::Result<void>> start_watcher_;
    bool stop_requested_while_starting_ = false;
    bool running_ = false;
    QString camera_error_;

    mutable std::mutex frame_mutex_;
    cv::Mat latest_frame_;
    int frame_seq_ = 0;
    // When the frame before was captured; the scan's rule that a side must have
    // held still is scaled by how long ago that was.
    std::optional<std::int64_t> previous_frame_ms_;
    int frame_width_ = 0;
    int frame_height_ = 0;

    // Set while a photometry lock has been asked for and not yet reported.
    bool awaiting_lock_ = false;
    int frames_awaiting_lock_ = 0;

    cube::vision::SideScan scan_;
    std::optional<cube::vision::SideRead> read_;
    bool reading_ = false;
    qreal capture_progress_ = 0.0;
    std::vector<cube::vision::SideScan::Side> sides_;
    bool conflicted_ = false;
    bool ambiguous_ = false;
    QStringList settling_corner_;
    std::optional<cube::vision::ScannedCube> scanned_;
    bool watching_ = false;

    QString recording_root_;
    cube::vision::recording::AsyncRecorder recorder_;
    // A scan whose folder could not be created is not retried frame by frame.
    bool recording_refused_ = false;

    Diagnostics* diagnostics_ = nullptr;
    // The device the camera was last opened on, and what its lock last
    // reported, kept so that a snapshot can say them whenever it is asked.
    QString device_id_;
    std::optional<cube::platform::camera::PhotometryLockReport> lock_report_;
    // What the last written `scan` record said, and how many evaluations
    // since agreed with it: a scan that has not changed is not written again.
    std::string last_scan_digest_;
    int scans_unchanged_ = 0;
};

}  // namespace cube::app
