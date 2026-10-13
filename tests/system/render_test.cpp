// Does the 3D cube actually render?
//
// Qt Quick 3D needs a real render path - a GPU or a software rasteriser behind
// RHI - and quietly draws nothing without one: the window is there, the
// controls are there, the cube's box is just the page colour. So this looks at
// the pixels. It is the check that the CI environment (Xvfb with Mesa's
// software GL on Linux) is one the app can be tested in at all.

#include "app.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QQuickItem>
#include <QQuickWindow>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace {

using cube::systest::App;

[[nodiscard]] QQuickItem* find_cube(App& app) {
    for (QQuickItem* control : app.visible_controls()) {
        if (QByteArray(control->metaObject()->className()).startsWith("Cube3DView")) {
            return control;
        }
    }
    return nullptr;
}

[[nodiscard]] QRect device_box(const QImage& image, QQuickItem* item) {
    const qreal ratio = image.devicePixelRatio();
    const QRect box = item->mapRectToScene(item->boundingRect()).toAlignedRect();
    return QRect(QPoint(qRound(box.x() * ratio), qRound(box.y() * ratio)),
                 QSize(qRound(box.width() * ratio), qRound(box.height() * ratio)))
        .intersected(image.rect());
}

struct Content {
    double non_background = 0.0;  // share of the box that is not the page colour
    int saturated_hues = 0;       // 30-degree hue bands with real colour in them
    int distinct = 0;             // distinct quantised colours
};

[[nodiscard]] Content measure(const QImage& image, const QRect& box) {
    const QColor background = image.pixelColor(box.left() + 1, box.top() + 1);
    std::set<unsigned> colours;
    long differs = 0;
    long total = 0;
    long coloured = 0;
    std::vector<long> per_band(12, 0);
    for (int y = box.top(); y < box.bottom(); y += 2) {
        for (int x = box.left(); x < box.right(); x += 2) {
            const QColor pixel = image.pixelColor(x, y);
            ++total;
            const int delta = std::abs(pixel.red() - background.red()) +
                              std::abs(pixel.green() - background.green()) +
                              std::abs(pixel.blue() - background.blue());
            if (delta < 24) {
                continue;
            }
            ++differs;
            colours.insert((static_cast<unsigned>(pixel.red() / 16) << 8) |
                           (static_cast<unsigned>(pixel.green() / 16) << 4) |
                           static_cast<unsigned>(pixel.blue() / 16));
            if (pixel.hsvSaturationF() > 0.4F && pixel.valueF() > 0.25F) {
                ++coloured;
                ++per_band[static_cast<std::size_t>(pixel.hsvHue() / 30) % 12];
            }
        }
    }
    Content content;
    content.non_background =
        total > 0 ? static_cast<double>(differs) / static_cast<double>(total) : 0.0;
    content.distinct = static_cast<int>(colours.size());
    for (const long band : per_band) {
        content.saturated_hues += (coloured > 0 && band * 50 >= coloured) ? 1 : 0;
    }
    return content;
}

}  // namespace

TEST_CASE("the 3D cube on the launch screen is drawn, in colour, and moves", "[system][render]") {
    App& app = App::fresh();
    // Idle spin on (the default): the picture should change by itself.
    REQUIRE(app.settings_state().property("cubeAutoRotate").toBool());
    app.settle();

    const QString api = app.graphics_api();
    std::printf("render: scene graph API is %s, window %dx%d\n", qPrintable(api),
                app.window().width(), app.window().height());
    // Quick 3D has no software adaptation. If this is "Software" the cube
    // below cannot appear, and the platform is not one the app can be tested on.
    CHECK(api != "Software");
    CHECK(api != "Null");

    QQuickItem* cube = find_cube(app);
    REQUIRE(cube != nullptr);

    const QImage first = app.grab();
    const QRect box = device_box(first, cube);
    REQUIRE(box.width() > 100);
    const Content content = measure(first, box);
    INFO("non-background " << content.non_background << ", hues " << content.saturated_hues
                           << ", distinct colours " << content.distinct);
    // A cube fills a good part of its box and shows at least two sticker
    // colours; an empty box is one flat colour.
    CHECK(content.non_background > 0.10);
    CHECK(content.distinct > 12);
    CHECK(content.saturated_hues >= 2);

    // And it is alive: a cube auto-rotating is not the same picture later.
    app.pump(600);
    const QImage later = app.grab();
    int moved = 0;
    for (int y = box.top(); y < box.bottom(); y += 3) {
        for (int x = box.left(); x < box.right(); x += 3) {
            moved += first.pixel(x, y) != later.pixel(x, y) ? 1 : 0;
        }
    }
    CHECK(moved > 100);
}

TEST_CASE("the 3D cube is drawn in the narrow layout and in the dark theme too",
          "[system][render]") {
    App& app = App::fresh();
    app.resize(cube::systest::kPhone);
    app.set_theme("dark");
    app.settle();

    QQuickItem* cube = find_cube(app);
    REQUIRE(cube != nullptr);
    const QImage image = app.grab();
    const Content content = measure(image, device_box(image, cube));
    CHECK(content.non_background > 0.05);
    CHECK(content.distinct > 12);
}
