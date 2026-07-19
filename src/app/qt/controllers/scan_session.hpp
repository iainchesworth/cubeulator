#pragma once

#include "interfaces/camera.hpp"

#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/model/scan_progress.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/capture_routing.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/face_observation.hpp>
#include <cube/vision/hand_presence.hpp>
#include <cube/vision/idle_gate.hpp>
#include <cube/vision/orientation_lattice.hpp>
#include <cube/vision/orientation_tracker.hpp>
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
// observation feeds a FaceAggregator and a face is captured only once its
// per-cell colours have converged across frames - cross-frame variance
// subsumes the old steadiness/hand gates, which remain exposed purely as
// advisory UI hints.
//
// Capture is FREE-FORM and IDENTITY-KEYED: no prompt sequence, no holding
// convention - the user shows faces in any order, at any rotation, and the
// OrientationTracker decides which physical face every observation belongs
// to. Six FaceAggregators are keyed by tracked body face; each pass's
// observation routes into the right one with its cells permuted to solver
// order via the resolved lattice element (orientation_lattice.hpp's
// obs_to_body tables - the same face keeps aggregating into the same
// window across in-plane re-keying, corner/face view changes and detection
// gaps). A converged window commits through CaptureRouter under the plan's
// identity policy: IDENTITY DECIDES WHERE A CAPTURE GOES, CENSUS DECIDES
// WHETHER IT MAY LAND - the only way slot content is ever replaced is the
// verified in-place update, conflicts quarantine (and escalate to a force-
// lost track, or to full scan invalidation on the two-faces-conflicting
// layer-turn signature), and a post-loss epoch re-associates against the
// store by content before anything commits ("re-orienting"). Commits are
// additionally gated on frontal-ness, per-pass rotation, motion score, no
// unknown-classed cells, and a per-face holdoff - so mid-rotation or
// occluded windows keep accumulating instead of landing - while an edge-
// clipped observation (any quad corner out of frame) never enters a
// window at all, so a converged window is fully-visible by construction. Once the sixth distinct slot fills, resolve_colors()
// clusters all 54 stickers globally and assemble_scanned_faces() remains
// the acceptance oracle exactly as before.
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
    Q_PROPERTY(bool previewMirrored READ previewMirrored WRITE setPreviewMirrored NOTIFY previewMirroredChanged)
    Q_PROPERTY(QString trackingState READ trackingState NOTIFY readingChanged)
    Q_PROPERTY(bool reorienting READ reorienting NOTIFY readingChanged)
    Q_PROPERTY(qreal overlayFreshness READ overlayFreshness NOTIFY readingChanged)
    Q_PROPERTY(qreal captureProgress READ captureProgress NOTIFY readingChanged)
    Q_PROPERTY(bool autoAdvance READ autoAdvance WRITE setAutoAdvance NOTIFY autoAdvanceChanged)
    Q_PROPERTY(bool handDetectionEnabled READ handDetectionEnabled WRITE setHandDetectionEnabled NOTIFY
                   handDetectionEnabledChanged)
    Q_PROPERTY(QVariantList progressCenters READ progressCenters NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList progressCentersConfirmed READ progressCentersConfirmed NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList progressFacelets READ progressFacelets NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList slotSummaries READ slotSummaries NOTIFY facesScannedChanged)
    Q_PROPERTY(QVariantList faceCaptureStates READ faceCaptureStates NOTIFY facesScannedChanged)

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
    // The tracked 3D orientation as [w,x,y,z] reals - the
    // OrientationTracker's per-pass output (display-frame convention, see
    // pose_estimation.hpp / orientation_lattice.hpp), temporally SMOOTHED
    // (nlerp) so the Quick3D progress cube turns with the physical cube
    // instead of jittering at the per-frame estimate noise. Because the
    // tracker COASTS on its rate estimate through detection gaps, this
    // stays populated (and keeps turning) across blips the raw pose stream
    // would drop. Empty whenever the tracker is lost or still acquiring -
    // QML treats empty as "not tracking" and idles, exactly as before.
    [[nodiscard]] QVariantList cubeOrientation() const;  // 4 reals or empty
    // Whether the on-screen video preview is horizontally mirrored - bound
    // from CameraController.previewMirrored in QML (this class deliberately
    // has no CameraController dependency; the flag crosses in QML, where
    // the two singletons already meet). When set, cubeOrientation()
    // conjugates the tracked quaternion into the mirrored display frame
    // (see the getter) so the 3D progress cube yaws the way the mirrored
    // video - and therefore the user's hands - move, instead of opposite
    // to them (map RC12).
    [[nodiscard]] bool previewMirrored() const noexcept;
    void setPreviewMirrored(bool mirrored);
    // The OrientationTracker's state as a string ("lost", "acquiring",
    // "locked", "coasting") - QML-friendly, and stable identifiers for the
    // upcoming lost/coasting UI affordances. Shadow-mode observability for
    // now: nothing but the 3D cube consumes tracker output yet.
    [[nodiscard]] QString trackingState() const;
    // True while a post-loss epoch is buffering converged captures against
    // an unresolved store gauge (see CaptureRouter) - the UI's
    // "re-orienting" chip: captures are being held, not lost, and nothing
    // needs re-showing yet.
    [[nodiscard]] bool reorienting() const;
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
    // Display-only preview: slot content is now stored in solver facelet
    // order (the obs_to_body routing tables permute cells at aggregation
    // time), and the "face" letter stays a centre-colour GUESS until
    // global colour resolution confirms the centres - the shape and
    // meaning of each entry is unchanged from the centre-keyed store.
    [[nodiscard]] QVariantList slotSummaries() const;
    // One string per FILLED slot, aligned with slotSummaries: "ok", or
    // "conflict" while the slot has an outstanding verification strike
    // (identity routed a capture there but its colours disagreed) - the
    // UI's cue that a face may need re-showing or an explicit retake.
    [[nodiscard]] QVariantList faceCaptureStates() const;

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
    // Clears ONE captured slot (fill-order index, as shown by
    // slotSummaries) so the user can re-scan that face - the sanctioned
    // way a persistently conflicted capture is ever emptied. Distinct name
    // rather than a retakeFace(int) overload: QML overload resolution on
    // slots is unreliable, and the no-argument retakeFace() contract
    // (reset the current reading, keep all slots) must not change.
    void retakeSlot(int slotIndex);

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
    void previewMirroredChanged();
    // faceIndex is the SLOT index that was filled or updated - slots carry
    // no face identity, so it only tells the UI "a capture landed", never
    // which cube face it was.
    void faceConfirmed(int faceIndex);
    void scanCompleted();
    // Six slots filled but the captures didn't resolve/assemble into a
    // buildable cube. The most-suspect slot has already been dropped (see
    // the class comment's recovery contract) - facesScanned is back to 5
    // and scanning simply continues. `reason` is cube::to_string(CubeError)
    // plus a re-show hint naming the dropped capture by its centre-colour
    // guess (still one plain QString - the payload shape is unchanged).
    void scanNeedsRetry(QString reason);
    // Census conflicts landed on two DISTINCT captured faces within the
    // layer-turn window: the cube's stickers changed mid-scan (a layer was
    // turned), so every capture predating the turn is unusable. ALL slots
    // have already been cleared - facesScanned is 0 - and scanning simply
    // continues against the cube's new state; tracking itself keeps
    // running (the turn broke the sticker premise, not the pose).
    void scanInvalidated(QString reason);

private slots:
    void poll();
    void handleAnalysisFinished();
    void handleStartFinished();

private:
    struct AnalysisResult {
        // Which estimator produced `pose`, and whether the accepted solve
        // was seed-initialised (CubePoseProjection::from_seed) - the
        // tracker's provenance signal: a corner pose constrains all three
        // axes from seven pinned points while a face pose carries the
        // planar ambiguity, and seeded acceptances measure how often
        // continuity is doing real work.
        // `edge` marks the two-face edge-view path (observe_edge +
        // project_cube_from_edge): pose-only by construction, run only
        // when neither family produced a pose, and never accompanied by a
        // capture-grade observation of its own.
        enum class PoseSource { none, face, face_seeded, corner, corner_seeded, edge, edge_seeded };

        std::optional<cube::vision::FaceObservation> face;
        std::optional<cube::vision::CornerObservation> corner;
        std::optional<cube::vision::CubePoseProjection> pose;
        PoseSource pose_source = PoseSource::none;
        // True when EVERY cell quad of the family's accepted observation
        // this pass has all four corners inside the frame (false when the
        // family produced nothing). Stricter than the centre-only
        // visibility gate the observations themselves passed: a cell whose
        // centre is in-frame but whose quad straddles the border samples
        // clamped border pixels that are perfectly stable frame to frame -
        // stable enough to converge and capture as a real sticker. Carried
        // as gate DATA for capture-time policy (a clipped observation is
        // withheld from the aggregators, never from display), and kept PER
        // FAMILY because only the pose-producing family is aggregated: the
        // other family's incidental clipping says nothing about the
        // observation that actually feeds the windows.
        bool face_full_quads = false;
        bool corner_full_quads = false;
        // The analysed frame's TimedFrame mailbox timestamp, echoed
        // through so GUI-side consumers get pose and time base from the
        // same pass without correlating queues.
        std::int64_t frame_ms = 0;
        bool is_steady = false;
        // The stability tracker's raw score for this pass (is_steady is
        // its thresholded form): the tracker inflates its coasting
        // uncertainty faster when the scene is visibly moving.
        float motion_score = 0.0F;
        // hand_present is only meaningful when hand_checked is true: ONNX
        // inference is decimated (see kHandOnnxDecimation) and skipped
        // passes must not read as "hand gone" - the GUI holds the previous
        // value across unchecked passes.
        bool hand_present = false;
        bool hand_checked = false;
        // Cool-hue presence statistic for the cube-absent idle gate (see
        // cube/vision/idle_gate.hpp), computed on EVERY pass - including
        // idle-skipped ones, since it is the signal that ends an idle
        // span. Consumed by handleAnalysisFinished to advance the
        // GUI-owned IdleGate.
        int cool_hue_pixels = 0;
    };

    // corner_armed is captured on the GUI thread when the pass is queued so
    // analyze() never reads the slot store from the worker thread.
    // frame_t_ms is the frame's TimedFrame mailbox timestamp (see
    // interfaces/camera.hpp) - the analysis pass's time base for anything
    // motion-derived, since the poll cadence says nothing about frame age.
    // pose_prior is the previous pass's accepted pose, likewise sampled on
    // the GUI thread at queue time (by value - the worker must never read
    // GUI-owned pose state) and threaded into the projectors as their
    // continuity seed AND into compute_corner_hint so the corner detector
    // follows the predicted pose instead of demanding the fixed isometric
    // presentation; nullopt runs the bit-identical unseeded, unhinted path.
    // run_hand_detector is the GUI-thread-decided decimation verdict for
    // this pass (the worker holds no pass counter of its own), decimated
    // harder while the tracker is lost (kHandOnnxDecimationLost).
    // skip_detectors is likewise decided GUI-side at queue time from the
    // IdleGate: a true verdict elides the face/corner detectors and pose
    // for this pass (cube-absent frame, tracker lost) while stability,
    // hand election, the recorder and the cool-hue statistic still run.
    [[nodiscard]] AnalysisResult analyze(const cv::Mat& frame, std::int64_t frame_t_ms, bool corner_armed,
                                         std::optional<cube::vision::PoseSeed> pose_prior,
                                         bool run_hand_detector, bool skip_detectors);

    [[nodiscard]] bool cornerScanArmed() const;
    // True when some aggregator's window has converged AND the last pass
    // satisfied the primary commit gate (locked + identity-certain +
    // accept streak) - what readyToConfirm and the auto-advance path share.
    [[nodiscard]] bool anyCaptureReady() const;
    // Walks every converged aggregator through the commit gates and routes
    // the survivors into the CaptureRouter, handling every policy outcome
    // (fill/update signals, quarantine, force-lost escalation, layer-turn
    // invalidation) and finalizing at six slots. The single capture path -
    // auto-advance and the manual Confirm button both land here.
    void tryCommitConverged();
    // The recovery half of the completion contract: erase the slot with
    // the lowest mean confidence so the user only re-shows one face.
    // Returns a short user-facing description of the dropped capture ("the
    // red-centred face"), built from its centre-colour guess BEFORE the
    // clear - the scanNeedsRetry banner appends it so the user knows WHICH
    // face to re-show, not just that one was dropped. Empty when nothing
    // was dropped.
    [[nodiscard]] QString dropMostSuspectSlot(
        const std::array<float, cube::model::face_count>& mean_confidence);
    void finalizeIfComplete();
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
    // Monotone count of QUEUED analysis passes (GUI thread only), the
    // clock behind hand-inference decimation: poll() derives this pass's
    // run_hand_detector from it and hands the verdict into the worker
    // lambda by value, keeping the worker free of its own cross-pass
    // counters. Never reset - decimation only needs the modulus, and a
    // phase discontinuity across restarts is meaningless.
    int analysis_pass_seq_ = 0;

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
    // Continuous orientation tracking over the 24-element lattice (see
    // orientation_tracker.hpp). SHADOW MODE for now: it drives
    // cubeOrientation/trackingState (display) and the TRACK log line, and
    // nothing else - capture routing and aggregation still run on centre
    // colours exactly as before, so the tracker can be validated against
    // live behaviour without being able to break a scan. GUI thread only,
    // mutated solely in handleAnalysisFinished (one on_pass per analysis
    // pass - the pass cadence is its clock).
    cube::vision::OrientationTracker tracker_;
    // Cube-absent idle gate (see cube/vision/idle_gate.hpp): GUI thread
    // only, advanced once per pass in handleAnalysisFinished from the
    // worker-computed cool-hue count; poll() samples its skip verdict at
    // queue time. Reset with the reading - a new scan starts detecting.
    cube::vision::IdleGate idle_gate_;
    // Temporally smoothed tracker quaternion backing cubeOrientation (see
    // kOrientationSmoothing in the .cpp): nlerp'd toward each pass's
    // tracked orientation while locked/coasting, dropped on lost/acquiring
    // (and with the rest of the reading) so a re-lock or a new scan never
    // eases in from a stale pose. has_smoothed_orientation_ distinguishes
    // "no sample yet" (adopt the first one outright) from an ongoing
    // blend.
    std::array<float, 4> smoothed_orientation_{1.0F, 0.0F, 0.0F, 0.0F};
    bool has_smoothed_orientation_ = false;
    // Display-only mirror flag (see previewMirrored above). GUI thread
    // only, consumed at cubeOrientation() read time - it must never touch
    // tracker or routing state, whose geometry is defined by the CAMERA's
    // (unmirrored) frame regardless of how the preview is painted.
    bool preview_mirrored_ = false;
    std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets_{};
    float overall_confidence_ = 0.0F;
    bool is_steady_ = false;
    bool hand_present_ = false;

    // Temporal fusion state (GUI thread only), keyed by tracked body face:
    // aggregators_[f] accumulates every observation the tracker attributes
    // to body face f - across face/corner views, in-plane re-keying and
    // detection gaps - with cells already permuted to solver order by the
    // resolved lattice element's obs_to_body table. The AggregatorMeta
    // sidecar carries the per-window facts the commit gates need but the
    // (unchanged) FaceAggregator doesn't track: which cell permutation the
    // window was built under (a change means the resolution flipped
    // mid-window - the window resets rather than mix permutations), when
    // it was last fed (per-identity staleness replaces the old global
    // 700 ms reset), and whether any sample came from a corner view (which
    // loosens the frontal-ness gate to the corner-flank bound). The fully-
    // in-frame-quads commit gate carries NO per-window state: clipped
    // observations are refused at add time, so every sample in a window
    // has full quads by construction - a sticky per-window flag here would
    // let one clipped pass wedge a face uncommittable for as long as it
    // stayed observed (nothing could ever clear the flag while adds kept
    // refreshing the staleness clock).
    struct AggregatorMeta {
        std::array<int, 9> window_map{};
        bool has_window = false;
        std::int64_t last_add_ms = 0;
        bool corner_fed = false;
    };
    std::array<cube::vision::FaceAggregator, cube::model::face_count> aggregators_{};
    std::array<AggregatorMeta, cube::model::face_count> metas_{};
    // The tracker epoch the aggregator keys belong to: body labels die
    // with their epoch, so a change resets every window before anything
    // mis-keyed could accumulate.
    int agg_epoch_ = -1;
    // Commit-gate context sampled from the LAST analysis pass (gate 1 and
    // the per-pass motion facts), shared by auto-advance and the manual
    // Confirm path so both apply identical policy.
    bool capture_gate_ok_ = false;
    cube::vision::Quat capture_q_{1.0F, 0.0F, 0.0F, 0.0F};
    float capture_motion_score_ = 0.0F;
    float pass_rotation_deg_ = 0.0F;
    std::int64_t last_frame_ms_ = 0;
    int consec_accepts_ = 0;
    cube::vision::Quat last_track_q_{1.0F, 0.0F, 0.0F, 0.0F};
    bool has_last_track_q_ = false;

    // The identity-keyed slot store and its full commit policy (verified
    // updates, quarantine, strikes, layer-turn detection, epoch recovery)
    // - see capture_routing.hpp. GUI thread only.
    cube::vision::CaptureRouter router_;

    // Set by finalizeIfComplete() on a successful assembly; cleared by
    // restartScan(). progress_ is populated (assembled positions) at the
    // same moment, purely for the legacy display properties.
    std::optional<cube::solver::Cube> assembled_cube_;

    // AR-overlay persistence clock (see kOverlayHoldMs): restarted on every
    // fresh detection; held display geometry clears once it expires.
    QElapsedTimer overlay_hold_timer_;
};

}  // namespace cube::app
