#include <catch2/catch_test_macros.hpp>

#include "controllers/scan_session.hpp"

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
