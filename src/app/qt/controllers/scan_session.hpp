#pragma once

#include "interfaces/camera.hpp"

#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/model/scan_progress.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/hand_presence.hpp>
#include <cube/vision/pose_estimation.hpp>
#include <cube/vision/stability.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <opencv2/core.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace cube::app {

// Owns the live capture->vision-analysis pipeline for the scan screen:
// per-frame face/corner observations with AR-overlay geometry, temporal
// colour fusion with auto-capture into centre-keyed slots, and the
// end-of-scan global colour resolution + cube assembly handoff.
//
// Per-frame analysis (face/corner observation, shake, and especially ONNX
// hand-presence inference) runs off the GUI thread for the same reason
// camera enumeration does - see CameraController - via QtConcurrent, gated
// so at most one analysis pass is ever in flight; a poll() that finds one
// still running just updates the live image and skips kicking off another.
// ALL aggregator/progress mutation happens on the GUI thread in
// handleAnalysisFinished(), which keeps that state sequential without an
// explicit lock; analyze() itself only touches stability_/hand_detector_,
// serialized by the same single-in-flight gate.
//
// Capture is driven by temporal fusion, not per-frame gating: each
// observation feeds a FaceAggregator (one for the single-face view, three
// for the corner view) and a face is captured only once its per-cell
// colours have converged across frames - cross-frame variance subsumes the
// old steadiness/hand gates, which remain exposed purely as advisory UI
// hints. After any capture a re-arm guard swallows observations until the
// cube has visibly left the frame (several consecutive no-detection polls)
// or a short timeout passes, so the same face can't be captured twice
// while the user is still rotating to the next one.
//
// Capture is FREE-FORM: no prompt sequence and no holding convention.
// The user shows faces in any order, at any rotation. Each converged
// capture is keyed into one of up to six slots by its centre colour: a
// capture whose centre Lab reading matches an existing slot's centre
// updates that slot in place (the user re-showed a face), otherwise it
// fills a new slot. A corner capture slots its three visible faces
// independently the same way, so corner views are usable at ANY point in
// the scan, not just at fixed regrip counts. Which cube face a slot is,
// and how it is rotated, is never guessed during capture - once the sixth
// distinct slot fills, resolve_colors() clusters all 54 stickers globally
// and cube::solver::assemble_scanned_faces() searches per-face rotations
// for the arrangement that forms a physically buildable cube.
//
// Completion / recovery contract: on assembly SUCCESS the assembled Cube
// is exposed via assembledCube(), the legacy ScanProgress is rewritten
// with each resolved face at its ASSEMBLED position (display/back-compat
// only - SolveController consumes assembledCube() directly), and
// scanCompleted() fires. On FAILURE (unresolvable colours or no solvable
// assembly) scanNeedsRetry(reason) fires and ONLY the most-suspect slot -
// the one with the lowest mean resolved (or, failing that, provisional)
// colour confidence - is discarded: facesScanned drops from 6 to 5 and
// scanning continues seamlessly, so the user re-shows one face instead of
// restarting the whole scan.
//
// Per-frame facelets_ (cellColors/cellConfidence) stay PROVISIONAL -
// classify_lab() against the fixed reference palette, for live UI only.
//
// The 3D wireframe (cubeVertexPoints, from cube::vision::project_cube_*)
// is display-only: the projected pose makes the overlay read as a tracked
// object in space; face identity comes from centre colours + assembly,
// never from the pose.
class ScanSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool starting READ starting NOTIFY startingChanged)
    Q_PROPERTY(int frameSeq READ frameSeq NOTIFY frameUpdated)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameUpdated)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameUpdated)
    Q_PROPERTY(int facesScanned READ facesScanned NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList cellColors READ cellColors NOTIFY readingChanged)
    Q_PROPERTY(QVariantList cellConfidence READ cellConfidence NOTIFY readingChanged)
    Q_PROPERTY(qreal overallConfidence READ overallConfidence NOTIFY readingChanged)
    Q_PROPERTY(bool isSteady READ isSteady NOTIFY readingChanged)
    Q_PROPERTY(bool handPresent READ handPresent NOTIFY readingChanged)
    Q_PROPERTY(bool cubeDetected READ cubeDetected NOTIFY readingChanged)
    Q_PROPERTY(bool readyToConfirm READ readyToConfirm NOTIFY readingChanged)
    Q_PROPERTY(QVariantList faceQuadPoints READ faceQuadPoints NOTIFY readingChanged)
    Q_PROPERTY(QVariantList cellQuadPoints READ cellQuadPoints NOTIFY readingChanged)
    Q_PROPERTY(bool cornerDetected READ cornerDetected NOTIFY readingChanged)
    Q_PROPERTY(QVariantList cornerQuadPoints READ cornerQuadPoints NOTIFY readingChanged)
    Q_PROPERTY(QVariantList cornerCellColors READ cornerCellColors NOTIFY readingChanged)
    Q_PROPERTY(QVariantList cubeVertexPoints READ cubeVertexPoints NOTIFY readingChanged)
    Q_PROPERTY(QVariantList cubeOrientation READ cubeOrientation NOTIFY readingChanged)
    Q_PROPERTY(qreal overlayFreshness READ overlayFreshness NOTIFY readingChanged)
    Q_PROPERTY(qreal captureProgress READ captureProgress NOTIFY readingChanged)
    Q_PROPERTY(bool autoAdvance READ autoAdvance WRITE setAutoAdvance NOTIFY autoAdvanceChanged)
    Q_PROPERTY(bool handDetectionEnabled READ handDetectionEnabled WRITE setHandDetectionEnabled NOTIFY
                   handDetectionEnabledChanged)
    Q_PROPERTY(QVariantList progressCenters READ progressCenters NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList progressCentersConfirmed READ progressCentersConfirmed NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList progressFacelets READ progressFacelets NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList slotSummaries READ slotSummaries NOTIFY facesScannedChanged)

public:
    explicit ScanSession(QObject* parent = nullptr);
    // QtConcurrent::run futures are uncancelable and ~QFutureWatcher does
    // NOT wait for them - without an explicit wait here, closing the app
    // mid-scan would destroy stability_/hand_detector_ while the analysis
    // worker is still dereferencing them (a use-after-free on exit).
    ~ScanSession() override;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool starting() const noexcept;
    [[nodiscard]] int frameSeq() const noexcept;
    [[nodiscard]] int frameWidth() const noexcept;
    [[nodiscard]] int frameHeight() const noexcept;
    [[nodiscard]] int facesScanned() const;
    [[nodiscard]] QVariantList cellColors() const;
    [[nodiscard]] QVariantList cellConfidence() const;
    [[nodiscard]] qreal overallConfidence() const noexcept;
    [[nodiscard]] bool isSteady() const noexcept;
    [[nodiscard]] bool handPresent() const noexcept;
    [[nodiscard]] bool cubeDetected() const noexcept;
    [[nodiscard]] bool readyToConfirm() const noexcept;
    // AR overlay geometry, full-frame pixel coordinates, flat
    // [x0,y0,x1,y1,...] with quad corners ordered tl,tr,br,bl. Empty lists
    // when nothing was detected on the latest reading.
    [[nodiscard]] QVariantList faceQuadPoints() const;   // 8 reals
    [[nodiscard]] QVariantList cellQuadPoints() const;   // 9*8 reals, row-major cells
    [[nodiscard]] bool cornerDetected() const noexcept;
    [[nodiscard]] QVariantList cornerQuadPoints() const;  // 3*8 reals: top, lower_left, lower_right
    // 27 CSS colour strings (provisional classify_lab), slot-major in the
    // same slot order as cornerQuadPoints; empty when no corner.
    [[nodiscard]] QVariantList cornerCellColors() const;
    // 3D wireframe pose, full-frame pixel coordinates: flat [x0,y0,...] for
    // the 8 cube vertices in CubePoseProjection order (near face tl,tr,br,
    // bl, then each vertex's rear counterpart); empty when no pose could be
    // estimated this reading. Display-only - see the class comment.
    [[nodiscard]] QVariantList cubeVertexPoints() const;  // 16 reals
    // The current (or persistence-held) pose's 3D orientation as [w,x,y,z]
    // reals - the CubePoseProjection quaternion (canonical camera-facing
    // model frame -> observed pose, y-up display axes; see
    // pose_estimation.hpp), temporally SMOOTHED (nlerp) so the Quick3D
    // progress cube turns with the physical cube instead of jittering at
    // the per-frame estimate noise. Empty when there is no pose this
    // reading or its orientation_valid is false (corner-path fallback) -
    // QML treats empty as "not tracking" and idles.
    [[nodiscard]] QVariantList cubeOrientation() const;  // 4 reals or empty
    // 1.0 while the overlay geometry is from a fresh detection, decaying
    // toward 0 over the hold window after detection drops - QML fades the
    // AR overlay by this instead of strobing it at the per-frame detection
    // rate.
    [[nodiscard]] qreal overlayFreshness() const;
    // 0..1 convergence toward auto-capture: the guided aggregator's
    // convergence, or the mean of the three corner aggregators while a
    // corner is in view.
    [[nodiscard]] qreal captureProgress() const;
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
    // 54 CSS colour strings, face-major in cube::model::Face order
    // (U,R,F,D,L,B) x 9 row-major facelets. Every cell of an unconfirmed
    // face is an empty string. This is what the progress cube paints for
    // confirmed faces - the actual captured stickers, not a centre-colour
    // flood fill that misreads as "the whole face scanned as one colour".
    [[nodiscard]] QVariantList progressFacelets() const;
    // One QVariantMap per FILLED capture slot, in slot (fill) order:
    //   "face":   "U"/"D"/"L"/"R"/"F"/"B" - the slot's canonical cube
    //             position GUESSED from its centre colour alone, via the
    //             same mapping the assembler anchors on (White -> D,
    //             Yellow -> U, Green -> F, Blue -> B, Orange -> L,
    //             Red -> R); "?" when the centre classifies as unknown.
    //   "colors": 9 CSS colour strings (provisional classify_lab), the
    //             slot's stickers row-major as the camera saw them.
    // Display-only preview: the facelet ROTATION within each face is
    // uncorrected until final assembly (the camera saw the face at an
    // arbitrary quarter-turn; assemble_scanned_faces() fixes rotations
    // mathematically at six slots), and the "face" letter is likewise
    // provisional until global colour resolution confirms the centres.
    [[nodiscard]] QVariantList slotSummaries() const;

    // The most recently delivered frame (BGR), or an empty Mat if none has
    // arrived yet. Used by FrameImageProvider, not exposed to QML directly.
    [[nodiscard]] cv::Mat latestFrame() const;

    // The current (possibly complete) scan state - not a Q_PROPERTY since
    // ScanProgress isn't a QObject/Q_GADGET (cube::model stays Qt-free).
    // Only populated once a scan completes successfully (free-form slots
    // have no face identity until assembly). Used by SolveController's
    // legacy fallback path, not exposed to QML directly.
    [[nodiscard]] const cube::model::ScanProgress& progress() const noexcept;

    // The cube produced by assemble_scanned_faces() for the last completed
    // scan, already validated as physically buildable; nullopt until a scan
    // completes (and again after restartScan()). Plain getter, not a
    // Q_PROPERTY - only SolveController consumes it, and cube::solver::Cube
    // can't cross into QML anyway.
    [[nodiscard]] std::optional<cube::solver::Cube> assembledCube() const;

public slots:
    void start(const QString& deviceId);
    void stop();
    void confirmFace();
    void retakeFace();

    // Discards every capture slot (and any assembled cube) and starts the
    // scan over, without touching camera running/starting state - start()'s
    // own early-return guard means it can't be reused for this. Called by
    // SolveController if it ever rejects a completed scan.
    void restartScan();

signals:
    void runningChanged();
    void startingChanged();
    void frameUpdated();
    void facesScannedChanged();
    void readingChanged();
    void autoAdvanceChanged();
    void handDetectionEnabledChanged();
    // faceIndex is the SLOT index that was filled or updated - slots carry
    // no face identity, so it only tells the UI "a capture landed", never
    // which cube face it was.
    void faceConfirmed(int faceIndex);
    void scanCompleted();
    // Six slots filled but the captures didn't resolve/assemble into a
    // buildable cube. The most-suspect slot has already been dropped (see
    // the class comment's recovery contract) - facesScanned is back to 5
    // and scanning simply continues. `reason` is cube::to_string(CubeError).
    void scanNeedsRetry(QString reason);

private slots:
    void poll();
    void handleAnalysisFinished();
    void handleStartFinished();

private:
    struct AnalysisResult {
        std::optional<cube::vision::FaceObservation> face;
        std::optional<cube::vision::CornerObservation> corner;
        std::optional<cube::vision::CubePoseProjection> pose;
        bool is_steady = false;
        bool hand_present = false;
    };

    // corner_armed is captured on the GUI thread when the pass is queued so
    // analyze() never reads the slot store from the worker thread.
    // frame_t_ms is the frame's TimedFrame mailbox timestamp (see
    // interfaces/camera.hpp) - the analysis pass's time base for anything
    // motion-derived, since the poll cadence says nothing about frame age.
    [[nodiscard]] AnalysisResult analyze(const cv::Mat& frame, std::int64_t frame_t_ms, bool corner_armed);

    [[nodiscard]] bool cornerScanArmed() const;
    [[nodiscard]] bool faceCaptureReady() const;
    [[nodiscard]] bool cornerCaptureReady() const;
    void captureGuidedFace();
    void captureCornerFaces();
    // Routes one converged capture into the slot store by centre colour:
    // update the matching slot, else fill a new one, else (six filled, no
    // tight match) update the nearest slot within a looser radius, else
    // drop the capture. Emits faceConfirmed/facesScannedChanged when a
    // slot changed.
    void slotCapture(const cube::vision::CapturedFace& captured);
    // The recovery half of the completion contract: erase the slot with
    // the lowest mean confidence so the user only re-shows one face.
    void dropMostSuspectSlot(const std::array<float, cube::model::face_count>& mean_confidence);
    void finalizeIfComplete();
    void armRearmGuard();
    void resetAggregators();
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
    int frame_width_ = 0;
    int frame_height_ = 0;
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
    // Strict-alternation analysis gate: set when a pass is queued in
    // poll(), cleared FIRST THING in handleAnalysisFinished() - not
    // analysis_watcher_.isRunning(), which reports false in the window
    // between the future finishing and the queued finished() signal
    // actually running the handler. Gating on isRunning() would let poll()
    // queue a second pass inside that window; its setFuture() would then
    // make the pending handler consume the NEW pass's result (blocking the
    // GUI thread on .result() until it finishes), and any state sampled at
    // queue time would no longer correspond to the result being handled.
    // The flag guarantees queue -> handle -> queue -> handle alternation,
    // which per-pass sampled priors (this feature's whole data flow) depend
    // on. The flag is NOT sufficient alone: stop()/start() force-clear it
    // so a wedged pass can't lock analysis out of the next session, and
    // that pass may still be executing when the next session's poll timer
    // starts - so poll() gates on (flag || isRunning()), each half covering
    // the other's blind spot, preserving single-in-flight across restarts.
    // The timer feeds poll()'s watchdog log for a pass that never
    // delivered; the logged flag keeps that log at one line per episode
    // instead of one per poll.
    bool analysis_inflight_ = false;
    QElapsedTimer analysis_inflight_timer_;
    bool analysis_overrun_logged_ = false;

    // Opening the real capture device (cube::platform::camera::start()) is a
    // genuine multi-second hardware operation on Windows (MSMF device open),
    // same class of problem as CameraController's enumeration - it must
    // never run synchronously on the GUI thread, or the whole window reports
    // "Not Responding" for that whole time. Runs via QtConcurrent instead;
    // running_ only flips true once start_watcher_ actually finishes.
    QFutureWatcher<Result<void>> start_watcher_;

    // Latest per-frame reading, updated once per completed analysis pass.
    // facelets_/overall_confidence_ are derived from face_observation_ via
    // provisional classify_lab so the pre-existing cellColors/cellConfidence
    // properties keep their meaning.
    std::optional<cube::vision::FaceObservation> face_observation_;
    std::optional<cube::vision::CornerObservation> corner_observation_;
    std::optional<cube::vision::CubePoseProjection> cube_pose_;
    // Temporally smoothed pose quaternion backing cubeOrientation (see
    // kOrientationSmoothing in the .cpp): nlerp'd toward each fresh valid
    // orientation, held across detection blips alongside cube_pose_, and
    // reset with the rest of the reading so a new scan never eases in from
    // a stale pose. has_smoothed_orientation_ distinguishes "no sample yet"
    // (adopt the first one outright) from an ongoing blend.
    std::array<float, 4> smoothed_orientation_{1.0F, 0.0F, 0.0F, 0.0F};
    bool has_smoothed_orientation_ = false;
    std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets_{};
    float overall_confidence_ = 0.0F;
    bool is_steady_ = false;
    bool hand_present_ = false;

    // Temporal fusion state (GUI thread only). face_aggregator_ drives the
    // one-face-at-a-time view; corner_aggregators_ (slot order top,
    // lower_left, lower_right) drive the three-faces-at-once corner view.
    cube::vision::FaceAggregator face_aggregator_;
    std::array<cube::vision::FaceAggregator, 3> corner_aggregators_{};

    // The centre-keyed slot store (see the class comment): up to six
    // captured faces in ARBITRARY order with NO face identity - slot order
    // is simply fill order, which is fine because resolve_colors() and
    // assemble_scanned_faces() are both order-agnostic. facesScanned() is
    // this vector's size.
    std::vector<cube::vision::CapturedFace> slots_;
    // Set by finalizeIfComplete() on a successful assembly; cleared by
    // restartScan(). progress_ is populated (assembled positions) at the
    // same moment, purely for the legacy display properties.
    std::optional<cube::solver::Cube> assembled_cube_;

    // Post-capture re-arm guard: while active, observations are dropped
    // instead of aggregated, until either enough consecutive analysis
    // passes see no cube at all (the user moved it away to rotate) or a
    // timeout elapses (they re-aimed faster than detection dropped out).
    bool rearm_guard_active_ = false;
    int rearm_quiet_polls_ = 0;
    QElapsedTimer rearm_timer_;

    // Aggregator staleness (see kAggregatorStaleMs in the .cpp): a long gap
    // between observations resets the corresponding aggregator so sporadic
    // detections spread over minutes can never fake convergence.
    QElapsedTimer face_add_gap_timer_;
    QElapsedTimer corner_add_gap_timer_;

    // AR-overlay persistence clock (see kOverlayHoldMs): restarted on every
    // fresh detection; held display geometry clears once it expires.
    QElapsedTimer overlay_hold_timer_;
};

}  // namespace cube::app
