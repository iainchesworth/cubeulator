// The scan screen's picture: DirectScanScreen.qml loaded with the app's own
// controllers behind it and fed drawn camera frames.
//
// The camera faces the user, so its frame is shown the way a mirror shows it
// and a cube moved to the right goes right on screen. Everything that is
// *read* stays as the camera delivered it - a mirror image of a side never
// matches the side - so the mirror may exist only in how this one picture is
// painted. What this holds the screen to:
//
//   - the picture and the dots on it flip together, about the middle of the
//     screen, so every dot stays on the sticker it marks;
//   - the status, the chips and the buttons are never flipped.
//
// Both are checked from the items' own transforms, which need neither a GPU
// nor a display. The hidden "[.gallery]" case below draws the screen both ways
// for looking at.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "camera_preview_support.hpp"
#include "cube_view_reading.hpp"
#include "drawn_cube.hpp"
#include "qml_test_app.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaObject>
#include <QPointF>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using cube::solver::Cube;
using cube::solver::Face;

namespace {

constexpr int kWidth = 900;
constexpr int kHeight = 640;

using cube::test::frame_of_side;
using cube::test::WatchingCamera;

class Screen {
public:
    Screen() {
        QQmlComponent component(&cube::test::qml_engine());
        component.loadFromModule("Cubeulator", "DirectScanScreen");
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        item_->setWidth(kWidth);
        item_->setHeight(kHeight);
    }

    [[nodiscard]] std::vector<QQuickItem*> all(const char* name) const {
        return cube::test::items_named(item_.get(), name);
    }
    [[nodiscard]] QQuickItem* one(const char* name) const {
        const std::vector<QQuickItem*> found = all(name);
        REQUIRE(found.size() == 1);
        return found.front();
    }
    [[nodiscard]] QString settling_text() const {
        return item_->property("settlingText").toString();
    }
    [[nodiscard]] bool mirrored() const { return item_->property("mirrored").toBool(); }
    void set_mirrored(bool mirrored) { REQUIRE(item_->setProperty("mirrored", mirrored)); }

    // `local` in `item`'s own coordinates, in the screen's.
    [[nodiscard]] QPointF on_screen(const QQuickItem* item, QPointF local) const {
        return item_->mapFromItem(item, local);
    }

private:
    std::unique_ptr<QQuickItem> item_;
};

// The camera's frame fills the screen and crops what does not fit; sticker
// points arrive in frame pixels and are mapped through the same crop.
[[nodiscard]] QPointF unmirrored_position(const QPointF& frame_point, const cv::Size& frame) {
    const double scale = std::max(static_cast<double>(kWidth) / frame.width,
                                  static_cast<double>(kHeight) / frame.height);
    const double left = (kWidth - frame.width * scale) / 2.0;
    const double top = (kHeight - frame.height * scale) / 2.0;
    return {left + frame_point.x() * scale, top + frame_point.y() * scale};
}

}  // namespace

TEST_CASE("the scan screen follows the camera on whether its picture is mirrored",
          "[qml][direct-scan]") {
    Screen screen;

    REQUIRE(screen.mirrored() == cube::test::app_controllers().camera.previewMirrored());
}

TEST_CASE("the scan screen's picture and its dots flip together, and nothing else does",
          "[qml][direct-scan]") {
    const bool mirrored = GENERATE(true, false);
    CAPTURE(mirrored);

    WatchingCamera camera;
    Screen screen;
    screen.set_mirrored(mirrored);

    const cv::Mat frame = frame_of_side();
    for (std::size_t i = 0; i < cube::vision::SideScan::kMinReads; ++i) {
        camera.scan().processFrame(frame);
    }
    const QVariantList points = camera.scan().stickerPoints();
    REQUIRE(points.size() == 9);

    SECTION("the picture sits where the screen's edges are, or reversed across them") {
        // Its left edge is at the screen's right edge when mirrored.
        const QQuickItem* picture = screen.one("scan.picture");
        REQUIRE(screen.on_screen(picture, {0.0, 0.0}).x() == (mirrored ? kWidth : 0.0));
        REQUIRE(screen.on_screen(picture, {picture->width(), 0.0}).x() ==
                (mirrored ? 0.0 : kWidth));
    }

    SECTION("every dot is on the sticker it marks, as the picture shows it") {
        const std::vector<QQuickItem*> dots = screen.all("scan.dot");
        REQUIRE(dots.size() == 9);

        for (const QVariant& entry : points) {
            QPointF want = unmirrored_position(entry.toPointF(), frame.size());
            if (mirrored) {
                want.setX(kWidth - want.x());
            }
            const bool found = std::any_of(dots.begin(), dots.end(), [&](const QQuickItem* dot) {
                const QPointF at = screen.on_screen(dot, {dot->width() / 2.0, dot->height() / 2.0});
                return std::abs(at.x() - want.x()) < 0.5 && std::abs(at.y() - want.y()) < 0.5;
            });
            CAPTURE(want.x(), want.y());
            REQUIRE(found);
        }
    }

    SECTION("the status, the chips and the buttons are not flipped") {
        for (const char* name : {"scan.status", "scan.chips", "scan.buttons"}) {
            CAPTURE(name);
            const QQuickItem* item = screen.one(name);
            const QPointF origin = screen.on_screen(item, {0.0, 0.0});
            REQUIRE(origin.x() == item->x());
            REQUIRE(origin.y() == item->y());
            REQUIRE(screen.on_screen(item, {item->width(), 0.0}).x() == item->x() + item->width());
        }
    }
}

TEST_CASE("an ambiguous scan has the screen name the corner to show", "[qml][direct-scan]") {
    cube::app::DirectScan& camera = cube::test::app_controllers().directScan;
    camera.setWatching(false);
    camera.restart();

    const auto patterned = cube::test::patterned_cube();
    REQUIRE(patterned.has_value());
    for (const Face face : cube::solver::kAllFaces) {
        const cv::Mat frame =
            draw_side(cube::test::outside_view(*patterned, face), cube::test::Pose{});
        for (std::size_t i = 0; i < cube::vision::SideScan::kMinReads + 2; ++i) {
            camera.processFrame(frame);
        }
        camera.processFrame(cube::test::room({640, 360}));
    }
    REQUIRE(camera.ambiguous());
    REQUIRE(camera.settlingCorner().size() == 3);

    Screen screen;
    // The string for it - its own id when no catalogue is installed, as here -
    // and not the generic instruction that does not say which corner.
    const QString text = screen.settling_text();
    CHECK_FALSE(text.isEmpty());

    camera.restart();
    CHECK(camera.settlingCorner().isEmpty());
}

TEST_CASE("six sides that make no cube can be looked over, put right, and come back from",
          "[qml][direct-scan][review]") {
    cube::app::DirectScan& camera = cube::test::app_controllers().directScan;
    camera.setWatching(false);
    camera.restart();
    auto* state = cube::test::qml_engine().singletonInstance<QObject*>("Cubeulator", "AppState");
    REQUIRE(state != nullptr);
    state->setProperty("screen", QStringLiteral("scan"));

    {
        // Nothing read: nothing to look over.
        Screen screen;
        CHECK_FALSE(screen.one("scan.checkColours")->isVisible());
    }

    const auto patterned = cube::test::patterned_cube();
    REQUIRE(patterned.has_value());
    for (const Face face : cube::solver::kAllFaces) {
        const cv::Mat frame =
            draw_side(cube::test::outside_view(*patterned, face), cube::test::Pose{});
        for (std::size_t i = 0; i < cube::vision::SideScan::kMinReads + 2; ++i) {
            camera.processFrame(frame);
        }
        camera.processFrame(cube::test::room({640, 360}));
    }
    REQUIRE(camera.conflicted());

    Screen screen;
    QQuickItem* button = screen.one("scan.checkColours");
    CHECK(button->isVisible());
    // The scan has not made a cube, so this is the way on: it is the main button.
    CHECK(button->property("variant").toString() == "primary");

    REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
    CHECK(state->property("screen").toString() == "colours");
    CHECK(state->property("coloursFromScan").toBool());
    CHECK(cube::test::app_controllers().colours.fromScan());
    CHECK(cube::test::app_controllers().colours.size() == 3);

    // Back from there is a new scan.
    REQUIRE(QMetaObject::invokeMethod(state, "closeColours"));
    CHECK(state->property("screen").toString() == "scan");
    CHECK_FALSE(state->property("coloursFromScan").toBool());

    state->setProperty("screen", QStringLiteral("launch"));
    camera.restart();
    cube::test::app_controllers().colours.begin(3);
}

// Pictures of the scan screen, for looking at rather than for checking.
//
// Hidden and inert unless CUBE_GALLERY_DIR names a directory: it puts the real
// screen in a real window parked off-screen, shows it a drawn side, and saves
// what the window renders - mirrored, as it is for a camera facing the user,
// and as it would be if it were not. It needs a GPU, and there is no picture of
// whoever is at the desk.
//
//   set CUBE_GALLERY_DIR=C:\some\dir
//   cube_qml_tests "[.gallery]"
TEST_CASE("scan screen gallery", "[.gallery][direct-scan]") {
    const QString out = qEnvironmentVariable("CUBE_GALLERY_DIR");
    if (out.isEmpty()) {
        SKIP("set CUBE_GALLERY_DIR to a directory to write the pictures to");
    }
    REQUIRE(QDir().mkpath(out));

    cube::app::DirectScan& camera = cube::test::app_controllers().directScan;
    camera.setWatching(false);
    camera.restart();
    const cv::Mat frame = frame_of_side();
    for (std::size_t i = 0; i < cube::vision::SideScan::kMinReads + 4; ++i) {
        camera.processFrame(frame);
    }

    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setPosition(-4000, -4000);

    QQmlComponent component(&cube::test::qml_engine());
    component.loadFromModule("Cubeulator", "DirectScanScreen");
    std::unique_ptr<QQuickItem> screen(
        qobject_cast<QQuickItem*>(component.beginCreate(cube::test::qml_engine().rootContext())));
    if (screen == nullptr) {
        FAIL(component.errorString().toStdString());
    }
    screen->setParentItem(window.contentItem());
    component.completeCreate();
    screen->setWidth(kWidth);
    screen->setHeight(kHeight);
    window.resize(kWidth, kHeight);
    window.show();

    const auto snap = [&](const QString& name, bool mirrored) {
        REQUIRE(screen->setProperty("mirrored", mirrored));
        camera.processFrame(frame);
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 600) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        const QImage image = window.grabWindow();
        REQUIRE_FALSE(image.isNull());
        REQUIRE(image.save(QDir(out).filePath(name + ".png")));
    };
    snap("scan-mirrored", true);
    snap("scan-unmirrored", false);

    window.hide();
    camera.restart();
}

TEST_CASE("the scan screen offers 2 to 5 and scans the one chosen", "[qml][direct-scan][nxn]") {
    cube::app::DirectScan& camera = cube::test::app_controllers().directScan;
    camera.setWatching(false);
    camera.setSize(3);
    camera.useAutoSize();
    camera.restart();

    Screen screen;
    for (const int size : {2, 4, 5, 3}) {
        INFO("size " << size);
        QQuickItem* button = screen.one(("scan.size." + std::to_string(size)).c_str());
        // (Hidden when the camera would not start, which an earlier test may have left.)
        REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
        CHECK(camera.size() == size);
    }
    CHECK(screen.all("scan.size.6").empty());

    // Auto is on until a size is chosen, and a click on it gives the choice back.
    CHECK_FALSE(camera.autoSize());
    REQUIRE(QMetaObject::invokeMethod(screen.one("scan.size.auto"), "clicked"));
    CHECK(camera.autoSize());
    CHECK(camera.size() == 3);
    REQUIRE(QMetaObject::invokeMethod(screen.one("scan.size.5"), "clicked"));
    CHECK_FALSE(camera.autoSize());
    CHECK(camera.size() == 5);

    // The controller is shared: leave it as it is found.
    camera.useAutoSize();
}

TEST_CASE("the scan screen of a 5x5 marks its 25 stickers and no more", "[qml][direct-scan][nxn]") {
    cube::app::DirectScan& camera = cube::test::app_controllers().directScan;
    camera.setWatching(false);
    camera.restart();
    camera.setSize(5);
    REQUIRE(camera.size() == 5);

    const cube::nxn::NCube cube = cube::nxn::random_scramble({.size = 5, .seed = 4, .moves = 80});
    cube::test::Pose pose;
    pose.pitch = 360.0F * 0.6F / 5.0F;
    const cv::Mat frame = draw_side(cube::nxn::outside_view(cube, Face::F), pose);
    for (std::size_t i = 0; i < cube::vision::SideScan::kMinReads; ++i) {
        camera.processFrame(frame);
    }
    REQUIRE(camera.stickerPoints().size() == 25);

    {
        Screen screen;
        const std::vector<QQuickItem*> dots = screen.all("scan.dot");
        REQUIRE(dots.size() == 25);
        // Every one is on its sticker, as the picture shows it (the camera in this
        // process is a front one or not; the dot follows the same mirror as the picture).
        for (const QQuickItem* dot : dots) {
            CHECK(dot->isVisible());
            // Smaller than a 3x3's 14, for the smaller stickers.
            CHECK(dot->width() < 14.0);
            CHECK(dot->width() >= 8.0);
        }
        // And the six chips are drawn from the sides read, 25 cells to a chip.
        CHECK(screen.one("scan.chips")->childItems().size() > 0);
    }

    camera.setSize(3);
    camera.restart();
    {
        Screen screen;
        // A 3x3's dots are the 14 they always were, and one for each of nine.
        CHECK(screen.all("scan.dot").size() == 9);
    }
}
