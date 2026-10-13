#pragma once

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <opencv2/core.hpp>

#include <mutex>
#include <optional>

namespace cube::app {

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

    // One entry per side read so far, in the order first shown:
    // { colors: nine "#rrggbb" strings row-major as last shown,
    //   inView: bool }.
    // The colours are the ones measured, not names - what the camera saw.
    Q_PROPERTY(QVariantList sides READ sides NOTIFY sidesChanged)
    Q_PROPERTY(int sidesRead READ sidesRead NOTIFY sidesChanged)
    // Six sides are in but do not make a valid cube yet: one was misread, or
    // one side was captured twice under different light.
    Q_PROPERTY(bool conflicted READ conflicted NOTIFY sidesChanged)
    // The cube has been read. The six sides stay on show - the last one
    // included - until whoever listens for scanCompleted moves on.
    Q_PROPERTY(bool complete READ complete NOTIFY sidesChanged)

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
    [[nodiscard]] QVariantList sides() const;
    [[nodiscard]] int sidesRead() const noexcept { return static_cast<int>(sides_.size()); }
    [[nodiscard]] bool conflicted() const noexcept { return conflicted_; }
    [[nodiscard]] bool complete() const noexcept { return scanned_.has_value(); }
    [[nodiscard]] bool watching() const noexcept { return watching_; }

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

    // Feeds one frame through the reader. poll() calls this for every camera
    // frame; it is public so the scan logic can be driven without a camera.
    void processFrame(const cv::Mat& bgr);

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
    // Six sides have been read and make exactly one valid cube: cube().
    void scanCompleted();
    // A frame has been looked at while watching: read() is its side, or
    // empty. Emitted after frameUpdated, once per frame.
    void frameWatched();

private:
    void poll();
    void handleStartFinished();
    void openCamera(const QString& deviceId);

    QTimer poll_timer_;
    QFutureWatcher<cube::Result<void>> start_watcher_;
    bool stop_requested_while_starting_ = false;
    bool running_ = false;
    QString camera_error_;

    mutable std::mutex frame_mutex_;
    cv::Mat latest_frame_;
    int frame_seq_ = 0;
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
    std::optional<cube::vision::ScannedCube> scanned_;
    bool watching_ = false;
};

}  // namespace cube::app
