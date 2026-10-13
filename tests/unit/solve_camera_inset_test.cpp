// The solve screen's camera preview: SolveScreen.qml loaded with the app's own
// controllers behind it and fed a drawn camera frame.
//
// As on the scan screen, the picture is shown the way a mirror shows it, and
// only the picture: what is read is the camera's own frame, because a mirror
// image of a side never matches the side. The preview also closes in on the
// cube and follows it (solve_screen_test.cpp holds it to that), working in the
// camera's own frame pixels, so the mirror has to come after all of it. What
// this holds the preview to:
//
//   - the picture and the dots on it flip together, about the middle of the
//     preview, so every dot stays on the sticker it marks wherever the view
//     has closed in to;
//   - the frame drawn over it and the flash are never flipped;
//   - the preview reads its own pixels in its own coordinates, flipped or not.
//
// Checked from the items' own transforms, which need neither a GPU nor a
// display.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "camera_preview_support.hpp"
#include "cube_view_reading.hpp"
#include "drawn_cube.hpp"
#include "qml_test_app.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

#include <QPointF>
#include <QQmlComponent>
#include <QQuickItem>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using cube::test::items_named;
using cube::test::wait_for;

namespace {

class SolveScreenItem {
public:
    SolveScreenItem() {
        QQmlComponent component(&cube::test::qml_engine());
        component.loadFromModule("Cubeulator", "SolveScreen");
        if (component.isError()) {
            FAIL(component.errorString().toStdString());
        }
        item_.reset(qobject_cast<QQuickItem*>(component.create()));
        if (item_ == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        item_->setWidth(900);
        item_->setHeight(640);
    }

    [[nodiscard]] QQuickItem* one(const char* name) const {
        const std::vector<QQuickItem*> found = items_named(item_.get(), name);
        REQUIRE(found.size() == 1);
        return found.front();
    }
    [[nodiscard]] std::vector<QQuickItem*> all(const char* name) const {
        return items_named(item_.get(), name);
    }

    // `local` in `item`'s own coordinates, in the screen's.
    [[nodiscard]] QPointF on_screen(const QQuickItem* item, QPointF local) const {
        return item_->mapFromItem(item, local);
    }

private:
    std::unique_ptr<QQuickItem> item_;
};

// A cube to be solving, so the screen has what it needs to say its words.
void be_solving_something() {
    SolveController& solver = cube::test::app_controllers().solve;
    solver.solveCube(cube::solver::random_scramble(7));
    REQUIRE(wait_for([&]() { return !solver.isSolving(); }, 60'000));
}

}  // namespace

TEST_CASE("the solve screen's camera preview follows the camera on whether it is mirrored",
          "[qml][solve-screen][camera-preview]") {
    be_solving_something();
    const SolveScreenItem screen;
    const QQuickItem* preview = screen.one("solve.camera");

    REQUIRE(preview->property("mirrored").toBool() ==
            cube::test::app_controllers().camera.previewMirrored());
}

TEST_CASE(
    "the solve screen's camera preview flips the picture and its dots together, and nothing else",
    "[qml][solve-screen][camera-preview]") {
    const bool mirrored = GENERATE(true, false);
    CAPTURE(mirrored);

    be_solving_something();
    cube::test::WatchingCamera camera;
    const SolveScreenItem screen;
    QQuickItem* preview = screen.one("solve.camera");
    REQUIRE(preview->setProperty("mirrored", mirrored));

    const cv::Mat frame = cube::test::frame_of_side();
    for (std::size_t i = 0; i < cube::vision::SideScan::kMinReads; ++i) {
        camera.scan().processFrame(frame);
    }
    const QVariantList points = camera.scan().stickerPoints();
    REQUIRE(points.size() == 9);

    // Where a pixel of the camera's picture lands in the preview, read the way
    // the preview's own tests read it: in its own coordinates, unflipped.
    // Nothing is waited for between this and the geometry checked below, so
    // the view is wherever it is, closed in or not.
    const double width = preview->width();
    const double picture_x = preview->property("pictureX").toDouble();
    const double picture_y = preview->property("pictureY").toDouble();
    const double scale = preview->property("pictureScale").toDouble();
    REQUIRE(scale > 0.0);
    const auto lands = [&](const QPointF& pixel) {
        const double x = picture_x + pixel.x() * scale;
        return QPointF(mirrored ? width - x : x, picture_y + pixel.y() * scale);
    };

    SECTION("the picture is where its own pixels say, or reversed across the preview") {
        const QQuickItem* image = screen.one("solve.image");
        // Its left edge, in the preview.
        REQUIRE(std::abs(preview->mapFromItem(image, QPointF(0.0, 0.0)).x() -
                         (mirrored ? width - picture_x : picture_x)) < 0.01);
        REQUIRE(std::abs(preview->mapFromItem(image, QPointF(0.0, 0.0)).y() - picture_y) < 0.01);
    }

    SECTION("every dot is on the sticker it marks, as the picture shows it") {
        const std::vector<QQuickItem*> dots = screen.all("solve.dot");
        REQUIRE(dots.size() == 9);

        for (const QVariant& entry : points) {
            const QPointF want = lands(entry.toPointF());
            const bool found = std::any_of(dots.begin(), dots.end(), [&](const QQuickItem* dot) {
                const QPointF at =
                    preview->mapFromItem(dot, QPointF(dot->width() / 2.0, dot->height() / 2.0));
                return std::abs(at.x() - want.x()) < 0.5 && std::abs(at.y() - want.y()) < 0.5;
            });
            CAPTURE(want.x(), want.y());
            REQUIRE(found);
        }
    }

    SECTION("the preview and its words are not reversed on the screen") {
        // A flip anywhere above the picture would turn the words round too.
        for (const char* name : {"solve.camera", "solve.words", "solve.frame", "solve.flash"}) {
            CAPTURE(name);
            const QQuickItem* item = screen.one(name);
            REQUIRE(item->width() > 0.0);
            REQUIRE(screen.on_screen(item, QPointF(0.0, 0.0)).x() <
                    screen.on_screen(item, QPointF(item->width(), 0.0)).x());
        }
    }

    SECTION("the frame and the flash are not flipped") {
        for (const char* name : {"solve.frame", "solve.flash"}) {
            CAPTURE(name);
            const QQuickItem* item = screen.one(name);
            REQUIRE(std::abs(preview->mapFromItem(item, QPointF(0.0, 0.0)).x()) < 0.01);
            REQUIRE(std::abs(preview->mapFromItem(item, QPointF(item->width(), 0.0)).x() -
                             preview->width()) < 0.01);
        }
    }
}
