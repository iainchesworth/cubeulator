#pragma once

#include "interfaces/camera.hpp"

#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/model/scan_progress.hpp>
#include <cube/vision/hand_presence.hpp>
#include <cube/vision/stability.hpp>

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <opencv2/core.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace cube::app {

// Owns the live capture->vision-analysis pipeline for the scan screen:
// per-frame color/coverage/shake/hand-presence signals, per-face
// confirm/retake, and the color-scheme short-circuit prediction for the
// progress-cube preview.
//
// Per-frame analysis (grid detection, color classification, shake, and
// especially ONNX hand-presence inference) runs off the GUI thread for the
// same reason camera enumeration does - see CameraController - via
// QtConcurrent, gated so at most one analysis pass is ever in flight; a
// poll() that finds one still running just updates the live image and
// skips kicking off another; this also keeps stability_/hand_detector_
// access sequential without an explicit lock.
class ScanSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool starting READ starting NOTIFY startingChanged)
    Q_PROPERTY(int frameSeq READ frameSeq NOTIFY frameUpdated)
    Q_PROPERTY(int facesScanned READ facesScanned NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList cellColors READ cellColors NOTIFY readingChanged)
    Q_PROPERTY(QVariantList cellConfidence READ cellConfidence NOTIFY readingChanged)
    Q_PROPERTY(qreal overallConfidence READ overallConfidence NOTIFY readingChanged)
    Q_PROPERTY(bool isSteady READ isSteady NOTIFY readingChanged)
    Q_PROPERTY(bool handPresent READ handPresent NOTIFY readingChanged)
    Q_PROPERTY(bool cubeDetected READ cubeDetected NOTIFY readingChanged)
    Q_PROPERTY(bool readyToConfirm READ readyToConfirm NOTIFY readingChanged)
    Q_PROPERTY(bool autoAdvance READ autoAdvance WRITE setAutoAdvance NOTIFY autoAdvanceChanged)
    Q_PROPERTY(bool handDetectionEnabled READ handDetectionEnabled WRITE setHandDetectionEnabled NOTIFY
                   handDetectionEnabledChanged)
    Q_PROPERTY(QVariantList progressCenters READ progressCenters NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList progressCentersConfirmed READ progressCentersConfirmed NOTIFY facesScannedChanged)

public:
    explicit ScanSession(QObject* parent = nullptr);

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool starting() const noexcept;
    [[nodiscard]] int frameSeq() const noexcept;
    [[nodiscard]] int facesScanned() const;
    [[nodiscard]] QVariantList cellColors() const;
    [[nodiscard]] QVariantList cellConfidence() const;
    [[nodiscard]] qreal overallConfidence() const noexcept;
    [[nodiscard]] bool isSteady() const noexcept;
    [[nodiscard]] bool handPresent() const noexcept;
    [[nodiscard]] bool cubeDetected() const noexcept;
    [[nodiscard]] bool readyToConfirm() const noexcept;
    [[nodiscard]] bool autoAdvance() const noexcept;
    void setAutoAdvance(bool enabled);
    [[nodiscard]] bool handDetectionEnabled() const noexcept;
    void setHandDetectionEnabled(bool enabled);
    // 6-entry lists, one per Face (U,R,F,D,L,B): the confirmed or
    // color-scheme-predicted center color as a CSS color string (empty
    // when neither is known), and whether that entry is confirmed
    // (full-fidelity) rather than merely predicted.
    [[nodiscard]] QVariantList progressCenters() const;
    [[nodiscard]] QVariantList progressCentersConfirmed() const;

    // The most recently delivered frame (BGR), or an empty Mat if none has
    // arrived yet. Used by FrameImageProvider, not exposed to QML directly.
    [[nodiscard]] cv::Mat latestFrame() const;

public slots:
    void start(const QString& deviceId);
    void stop();
    void confirmFace();
    void retakeFace();

signals:
    void runningChanged();
    void startingChanged();
    void frameUpdated();
    void facesScannedChanged();
    void readingChanged();
    void autoAdvanceChanged();
    void handDetectionEnabledChanged();
    void faceConfirmed(int faceIndex);
    void scanCompleted();

private slots:
    void poll();
    void handleAnalysisFinished();
    void handleStartFinished();

private:
    struct AnalysisResult {
        bool cube_detected = false;
        std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets{};
        float overall_confidence = 0.0F;
        bool is_steady = false;
        bool hand_present = false;
    };

    [[nodiscard]] AnalysisResult analyze(const cv::Mat& frame);
    void resetReading();

    QTimer poll_timer_;
    mutable std::mutex frame_mutex_;
    cv::Mat latest_frame_;
    bool running_ = false;
    // stop() while start_watcher_ is still in flight can't safely call
    // cube::platform::camera::stop() concurrently with the start() still
    // running on the thread pool - deferred until handleStartFinished()
    // instead, so the device never gets opened-then-immediately-abandoned
    // on a screen the user already navigated away from.
    bool stop_requested_while_starting_ = false;
    int frame_seq_ = 0;
    bool auto_advance_ = true;
    // Read from the analysis background thread (see analyze()), written
    // from the GUI thread via setHandDetectionEnabled() - atomic rather
    // than mutex-guarded since it's a single independent flag, not part of
    // a larger critical section.
    std::atomic<bool> hand_detection_enabled_{true};

    cube::vision::StabilityTracker stability_;
    std::unique_ptr<cube::vision::HandPresenceDetector> hand_detector_;
    cube::model::ScanProgress progress_;
    QFutureWatcher<AnalysisResult> analysis_watcher_;

    // Opening the real capture device (cube::platform::camera::start()) is a
    // genuine multi-second hardware operation on Windows (MSMF device open),
    // same class of problem as CameraController's enumeration - it must
    // never run synchronously on the GUI thread, or the whole window reports
    // "Not Responding" for that whole time. Runs via QtConcurrent instead;
    // running_ only flips true once start_watcher_ actually finishes.
    QFutureWatcher<Result<void>> start_watcher_;

    // Latest per-frame reading, updated once per completed analysis pass.
    bool cube_detected_ = false;
    std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets_{};
    float overall_confidence_ = 0.0F;
    bool is_steady_ = false;
    bool hand_present_ = false;
};

}  // namespace cube::app
