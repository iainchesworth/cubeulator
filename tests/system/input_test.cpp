// The same app, driven by other kinds of input: a finger, then a keyboard.

#include "app.hpp"
#include "fake_camera.hpp"
#include "reach.hpp"

#include "app_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/QTest>

using cube::systest::App;
using cube::systest::FakeCamera;

TEST_CASE("touch drives the launch, scan and settings screens like the mouse does",
          "[system][input][touch]") {
    App& app = App::fresh();
    app.resize(cube::systest::kPhone);

    app.tap("launch.settings");
    REQUIRE(app.wait_for([&]() { return app.screen() == "settings"; }));
    app.settle();
    app.tap("settings.theme.2");
    REQUIRE(app.wait_for([&]() { return app.theme().property("effectiveDark").toBool(); }));
    app.tap("settings.theme.1");
    REQUIRE(app.wait_for([&]() { return !app.theme().property("effectiveDark").toBool(); }));
    app.tap("settings.back");
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));

    app.tap("launch.startScan");
    REQUIRE(app.wait_for([&]() { return app.screen() == "scan"; }));
    REQUIRE(app.wait_for([&]() { return app.runtime().directScan().running(); }));
    QQuickItem* cancel = app.find_button("Cancel");
    REQUIRE(cancel != nullptr);
    app.tap(cancel);
    REQUIRE(app.wait_for([&]() { return app.screen() == "launch"; }));
    REQUIRE(app.wait_for([&]() { return !FakeCamera::instance().capturing(); }));
}

TEST_CASE("the 3D cube turns under a drag", "[system][input][cube]") {
    App& app = App::fresh();
    // Idle spin off, so any turn seen is the drag's.
    app.settings_state().setProperty("cubeAutoRotate", false);
    app.settle();

    QQuickItem* cube = nullptr;
    for (QQuickItem* control : app.visible_controls()) {
        if (QByteArray(control->metaObject()->className()).startsWith("Cube3DView")) {
            cube = control;
        }
    }
    REQUIRE(cube != nullptr);
    app.reach().activated(app.control_key(cube), app.layout());

    // The view eases back to its presented orientation once let go, so the
    // picture that proves the turn is the one taken while the button is down.
    const QImage before = app.grab();
    const QPoint from = cube->mapToScene(QPointF(cube->width() / 2, cube->height() / 2)).toPoint();
    QTest::mousePress(&app.window(), Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 8; ++step) {
        QTest::mouseMove(&app.window(), from + QPoint(12 * step, 4 * step));
        app.pump(16);
    }
    app.pump(120);
    const QImage during = app.grab();
    QTest::mouseRelease(&app.window(), Qt::LeftButton, Qt::NoModifier, from + QPoint(96, 32));

    // The cube's box, in device pixels.
    const qreal ratio = before.devicePixelRatio();
    const QRect box = cube->mapRectToScene(cube->boundingRect()).toAlignedRect();
    const QRect pixels(QPoint(qRound(box.x() * ratio), qRound(box.y() * ratio)),
                       QSize(qRound(box.width() * ratio), qRound(box.height() * ratio)));
    int changed = 0;
    for (int y = pixels.top(); y < pixels.bottom(); y += 3) {
        for (int x = pixels.left(); x < pixels.right(); x += 3) {
            changed += before.pixel(x, y) != during.pixel(x, y) ? 1 : 0;
        }
    }
    // A drag that turned nothing leaves the same picture.
    CHECK(changed > 200);
}
