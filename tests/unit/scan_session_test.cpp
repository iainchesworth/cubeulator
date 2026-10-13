#include <catch2/catch_test_macros.hpp>

#include "controllers/scan_session.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>

using cube::app::ScanSession;

TEST_CASE("ScanSession starts idle with nothing scanned or detected", "[qt][controllers]") {
    ScanSession session;

    REQUIRE_FALSE(session.running());
    REQUIRE(session.facesScanned() == 0);
    REQUIRE_FALSE(session.cubeDetected());
    REQUIRE_FALSE(session.readyToConfirm());
    REQUIRE(session.autoAdvance());
    REQUIRE(session.frameSeq() == 0);
}

TEST_CASE("ScanSession::confirmFace is a no-op without a detected reading", "[qt][controllers]") {
    ScanSession session;

    session.confirmFace();

    REQUIRE(session.facesScanned() == 0);
}

TEST_CASE("ScanSession::stop is safe to call before start", "[qt][controllers]") {
    ScanSession session;

    session.stop();

    REQUIRE_FALSE(session.running());
}

TEST_CASE("ScanSession::progressCenters starts as six empty entries", "[qt][controllers]") {
    ScanSession session;

    const auto centers = session.progressCenters();
    const auto confirmed = session.progressCentersConfirmed();

    REQUIRE(centers.size() == 6);
    REQUIRE(confirmed.size() == 6);
    for (const auto& entry : confirmed) {
        REQUIRE_FALSE(entry.toBool());
    }
}

TEST_CASE("ScanSession identity-routing state starts empty and honest", "[qt][controllers]") {
    ScanSession session;

    // The tracker has never anchored: no orientation, no identity claims,
    // no recovery in progress, no capture states to report.
    REQUIRE(session.trackingState() == QStringLiteral("lost"));
    REQUIRE(session.cubeOrientation().isEmpty());
    REQUIRE_FALSE(session.reorienting());
    REQUIRE(session.faceCaptureStates().isEmpty());
    REQUIRE(session.slotSummaries().isEmpty());
    REQUIRE(session.captureProgress() == 0.0);
}

TEST_CASE("ScanSession::retakeSlot ignores indices that name no slot", "[qt][controllers]") {
    ScanSession session;

    session.retakeSlot(0);
    session.retakeSlot(-1);
    session.retakeSlot(5);

    REQUIRE(session.facesScanned() == 0);
}

// P6.12 acceptance probe, hidden + env-gated like the camera probes: drives
// the REAL track-mode pipeline (bundled corner model, M3T, the live worker)
// against a recorded clip via CUBE_VISION_REPLAY_DIR and asserts the fusion
// store actually receives observations - the exact wiring the live-demo
// diagnosis measured as absent (track mode analysed every frame and fed
// nothing, so zero commits, unconditionally). With
// CUBE_VISION_REPLAY_EXPECT_COMPLETE=1 it additionally requires the
// completion chain (registration -> resolve -> hint -> assembly) to fire
// scanCompleted - run that variant against a clip the offline runner
// proves assembles (synth-freerot); run the plain variant against clips
// that must feed without assembling (the live-demo session's 33-epoch
// fragmentation, task P6.13).
//
//   set CUBE_VISION_REPLAY_DIR=<clip> && cube_qt_controller_tests "[.track-fusion-replay]"
TEST_CASE("track-mode replay feeds the fusion store and finalizes on six converged faces",
          "[.track-fusion-replay]") {
    const QString clip = qEnvironmentVariable("CUBE_VISION_REPLAY_DIR");
    if (clip.isEmpty()) {
        WARN("CUBE_VISION_REPLAY_DIR not set - point it at a recorded clip to run this probe");
        return;
    }
    const bool expect_complete = qEnvironmentVariable("CUBE_VISION_REPLAY_EXPECT_COMPLETE") == "1";
    // Replay fidelity with the offline runner: the runner loads the clip's
    // own calibration (load_clip_intrinsics); the live loop reads
    // CUBE_VISION_INTRINSICS, so bridge the two when the caller didn't.
    if (qEnvironmentVariable("CUBE_VISION_INTRINSICS").isEmpty() &&
        QDir(clip).exists(QStringLiteral("intrinsics.txt"))) {
        qputenv("CUBE_VISION_INTRINSICS",
                QDir(clip).absoluteFilePath(QStringLiteral("intrinsics.txt")).toLocal8Bit());
    }

    ScanSession session;
    session.setHandDetectionEnabled(false);  // ONNX hand passes only cost replay cadence here
    bool completed = false;
    QObject::connect(&session, &ScanSession::scanCompleted, [&completed] { completed = true; });

    session.start(QString());  // replay-active start: no camera, poll timer running
    REQUIRE(session.running());

    // Drive the event loop until the clip stops advancing (the replay
    // holds its final state) or completion fires; the deadline is a
    // watchdog, not the expected duration.
    QElapsedTimer total;
    total.start();
    QElapsedTimer idle;
    idle.start();
    int last_seq = -1;
    while (total.elapsed() < 240000 && !completed) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (session.frameSeq() != last_seq) {
            last_seq = session.frameSeq();
            idle.restart();
        } else if (idle.elapsed() > 3000) {
            break;  // clip exhausted and the last analysis pass drained
        }
    }
    session.stop();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    INFO("frames replayed: " << session.frameSeq());
    INFO("fusion observations: " << session.trackFusionObservations());
    INFO("facesScanned (fusion converged): " << session.facesScanned());
    CHECK(session.frameSeq() > 0);
    // THE acceptance fact: track-mode passes now FEED the fusion store.
    CHECK(session.trackFusionObservations() > 0);
    if (expect_complete) {
        CHECK(completed);
        CHECK(session.assembledCube().has_value());
        CHECK(session.facesScanned() == 6);
    }
    WARN("track-fusion replay: frames="
         << session.frameSeq() << " observations=" << session.trackFusionObservations()
         << " converged=" << session.facesScanned() << " completed=" << (completed ? "yes" : "no"));
}

TEST_CASE("ScanSession::previewMirrored defaults off and round-trips", "[qt][controllers]") {
    ScanSession session;

    // Default must be the unmirrored camera frame: the mirror conjugation
    // in cubeOrientation() is display-only compensation that QML opts into
    // by binding CameraController.previewMirrored - a session nobody bound
    // must behave exactly like the pre-mirror-aware code.
    REQUIRE_FALSE(session.previewMirrored());

    session.setPreviewMirrored(true);
    REQUIRE(session.previewMirrored());
    session.setPreviewMirrored(true);  // idempotent re-set must be safe
    REQUIRE(session.previewMirrored());
    session.setPreviewMirrored(false);
    REQUIRE_FALSE(session.previewMirrored());

    // The flag alone must not conjure an orientation: cubeOrientation
    // stays empty (tracker lost) whichever way the mirror points.
    session.setPreviewMirrored(true);
    REQUIRE(session.cubeOrientation().isEmpty());
}
