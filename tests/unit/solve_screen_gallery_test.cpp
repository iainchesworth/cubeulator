// Pictures of the solve screen, for looking at rather than for checking.
//
// Hidden ("[.gallery]") and inert unless CUBE_GALLERY_DIR names a directory:
// it puts the real SolveScreen in a real window, parked off-screen, drives it
// with drawn camera frames, and saves what the window renders. This is how
// the screen can be seen in each of the camera's states without a camera or
// a scan - and without a picture of whoever is at the desk. It needs a GPU,
// so it is never part of the ordinary test run.
//
// One picture per run: grabbing a window with a 3D scene in it a second time
// brings the process down (Qt 6.8.3, Direct3D), so CUBE_GALLERY_SHOT picks
// which of the pictures below this run takes.
//
//   set CUBE_GALLERY_DIR=C:\some\dir
//   set CUBE_GALLERY_SHOT=2-matched-as-asked
//   cube_qml_tests "[.gallery]"
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "drawn_cube.hpp"
#include "qml_test_app.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>

#include <memory>
#include <optional>

using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Hold;
using cube::solver::Move;
using cube::test::wait_for;

namespace {

void pass_time(int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

}  // namespace

TEST_CASE("solve screen gallery", "[.gallery]") {
    const QString out = qEnvironmentVariable("CUBE_GALLERY_DIR");
    if (out.isEmpty()) {
        SKIP("set CUBE_GALLERY_DIR to a directory to write the pictures to");
    }
    REQUIRE(QDir().mkpath(out));
    const QString wanted = qEnvironmentVariable("CUBE_GALLERY_SHOT", "1-not-seen-yet");

    cube::app::AppControllers& app = cube::test::app_controllers();
    cube::app::DirectScan& camera = app.directScan;
    SolveController& solver = app.solve;
    const auto frame_of = [](const Cube& cube, const Hold& hold) {
        return cube::test::draw_side(cube::solver::camera_view(cube, hold),
                                     cube::test::Pose{.centre = {330.0F, 170.0F}, .pitch = 34.0F});
    };
    const auto show = [&camera](const cv::Mat& frame, int frames) {
        for (int i = 0; i < frames; ++i) {
            camera.processFrame(frame);
        }
    };

    // A cube scanned from drawings, so the camera has colours to match.
    const Cube scrambled = cube::solver::random_scramble(7);
    camera.setWatching(false);
    camera.restart();
    for (const Face face : cube::solver::kAllFaces) {
        show(cube::test::draw_side(cube::test::outside_view(scrambled, face), cube::test::Pose{}),
             static_cast<int>(cube::vision::SideScan::kMinReads) + 4);
        camera.processFrame(cube::test::room({640, 360}));
    }
    REQUIRE(camera.complete());
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));

    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setPosition(-4000, -4000);

    // Made inside the window from the start, as the app's own screens are:
    // the 3D scene is set up against the window it will be drawn in.
    QQmlComponent component(&cube::test::qml_engine());
    component.loadFromModule("Cubeulator", "SolveScreen");
    std::unique_ptr<QQuickItem> screen(
        qobject_cast<QQuickItem*>(component.beginCreate(cube::test::qml_engine().rootContext())));
    if (screen == nullptr) {
        FAIL(component.errorString().toStdString());
    }
    screen->setParentItem(window.contentItem());
    component.completeCreate();

    const auto snap = [&](const QString& name, int width = 900, int height = 640) {
        if (name != wanted) {
            return;
        }
        window.resize(width, height);
        screen->setWidth(width);
        screen->setHeight(height);
        window.show();
        // Long enough for views to settle and a nudge to get under way;
        // CUBE_GALLERY_WAIT_MS catches it at another moment.
        bool given = false;
        const int wait = qEnvironmentVariable("CUBE_GALLERY_WAIT_MS").toInt(&given);
        pass_time(given ? wait : 1700);
        const QImage image = window.grabWindow();
        REQUIRE_FALSE(image.isNull());
        REQUIRE(image.save(QDir(out).filePath(name + ".png")));
    };

    camera.setWatching(true);
    const int trust = cube::app::HeldCube::kFramesToTrust + 1;
    const int doubt = cube::app::HeldCube::kFramesToDoubt + 1;

    // Nothing in front of the camera yet.
    camera.processFrame(cube::test::room({640, 360}));
    snap("1-not-seen-yet");

    // Held as asked.
    show(frame_of(scrambled, cube::solver::kStatedHold), trust);
    snap("2-matched-as-asked");

    // Held another way: the cube on screen has turned to match.
    const Hold other{Face::R, Face::D};
    show(frame_of(scrambled, other), trust);
    snap("3-matched-another-way");

    // A side that is not on the cube as it should stand.
    const Cube strayed = scrambled.apply(Move{Face::U, 1}).apply(Move{Face::R, 1});
    show(frame_of(strayed, Hold{Face::U, Face::L}), doubt);
    snap("4-not-the-expected-side");

    // The cube picked up with the mouse and dragged half-way round, button
    // still down: real pointer events, through the window, to the screen's
    // own drag handler.
    if (wanted == "8-dragged") {
        window.resize(900, 640);
        screen->setWidth(900);
        screen->setHeight(640);
        window.show();
        pass_time(400);
        const auto mouse = [&window](QEvent::Type type, const QPointF& at, Qt::MouseButton button,
                                     Qt::MouseButtons held) {
            QMouseEvent event(type, at, window.mapToGlobal(at), button, held, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &event);
            QCoreApplication::processEvents();
        };
        mouse(QEvent::MouseButtonPress, {290.0, 340.0}, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 34; ++step) {
            mouse(QEvent::MouseMove, {290.0 + 10.0 * step, 340.0 - 3.0 * step}, Qt::NoButton,
                  Qt::LeftButton);
        }
        snap("8-dragged");
        mouse(QEvent::MouseButtonRelease, {630.0, 238.0}, Qt::LeftButton, Qt::NoButton);
    }

    // A turn of the wrong side, seen from the hold just matched.
    show(frame_of(scrambled, other), trust);
    for (const Face face : cube::solver::kAllFaces) {
        const Cube wrong = scrambled.apply(Move{face, 1});
        if (face != cube::solver::side_at(other, cube::solver::Place::Back) &&
            solver.currentMoveFace() != cube::test::letter(face) &&
            cube::solver::camera_view(wrong, other) !=
                cube::solver::camera_view(scrambled, other)) {
            camera.processFrame(cube::test::room({640, 360}));
            show(frame_of(wrong, other), trust);
            break;
        }
    }
    snap("7-wrong-turn");

    // Back to how the screen asks, and on to a move on the back.
    show(frame_of(scrambled, cube::solver::kStatedHold), trust);
    while (!solver.atEnd() && solver.currentMoveFace() != "B") {
        solver.nextMove();
    }
    show(frame_of(solver.currentCube(), cube::solver::kStatedHold), trust);
    snap("5-back-move");

    // Narrow, as on a phone held upright.
    snap("6-narrow", 420, 760);

    window.hide();
    camera.setWatching(false);
    camera.restart();
}
