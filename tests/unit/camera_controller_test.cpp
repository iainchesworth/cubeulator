#include <catch2/catch_test_macros.hpp>

#include "controllers/camera_controller.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <functional>

using cube::app::CameraController;

namespace {

// enumerate_devices() runs on Qt's thread pool (see camera_controller.hpp),
// so tests need to pump the event loop until it finishes rather than
// reading results immediately after calling refreshDevices().
bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 30000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return predicate();
}

}  // namespace

TEST_CASE("CameraController starts idle with no devices", "[qt][controllers]") {
    CameraController controller;

    REQUIRE_FALSE(controller.busy());
    REQUIRE(controller.devices().isEmpty());
    REQUIRE(controller.selectedDeviceId().isEmpty());
}

TEST_CASE("CameraController::refreshDevices runs off the GUI thread and reports a result",
          "[qt][controllers]") {
    // No real camera is guaranteed on a CI runner; this only checks the
    // enumerate_devices()->QVariantList marshaling path doesn't throw and
    // leaves lastError empty, matching the platform contract that
    // enumeration always succeeds (possibly with an empty list).
    CameraController controller;

    controller.refreshDevices();
    REQUIRE(controller.busy());

    REQUIRE(wait_until([&] { return !controller.busy(); }));
    REQUIRE(controller.lastError().isEmpty());
}

TEST_CASE("CameraController::selectDevice updates the id and skips duplicate signals",
          "[qt][controllers]") {
    CameraController controller;
    int change_count = 0;
    QObject::connect(&controller, &CameraController::selectedDeviceIdChanged,
                     [&]() { ++change_count; });

    controller.selectDevice("cam-1");
    controller.selectDevice("cam-1");
    controller.selectDevice("cam-2");

    REQUIRE(controller.selectedDeviceId() == QString("cam-2"));
    REQUIRE(change_count == 2);
}

TEST_CASE("The preview is mirrored for every camera but a rear one", "[qt][controllers]") {
    using cube::platform::camera::Facing;

    // A front camera looks at the user, and a desktop webcam reports unknown
    // while doing the same: both are shown as a mirror shows them. A rear
    // camera shows the scene, and mirroring it would flip the cube's layout.
    REQUIRE(CameraController::previewMirroredFor(Facing::front));
    REQUIRE(CameraController::previewMirroredFor(Facing::unknown));
    REQUIRE_FALSE(CameraController::previewMirroredFor(Facing::rear));
}

TEST_CASE("CameraController mirrors the preview until a camera says otherwise", "[qt][controllers]") {
    // Nothing selected yet, which is how the scan screen first appears on a
    // desktop with a webcam: the default is the mirror.
    CameraController controller;

    REQUIRE(controller.previewMirrored());
}
