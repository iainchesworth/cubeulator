#include <catch2/catch_test_macros.hpp>

#include "desktop/window_geometry.hpp"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSettings>
#include <QSize>
#include <QTemporaryDir>

using cube::app::chooseWindowPlacement;
using cube::app::kDefaultWindowSize;
using cube::app::kMinimumWindowSize;
using cube::app::WindowGeometry;
using cube::app::WindowState;

namespace {

const QList<QRect> kOneScreen{QRect(0, 0, 1920, 1080)};
// A second monitor to the left of the primary one, as Windows numbers it.
const QList<QRect> kTwoScreens{QRect(0, 0, 1920, 1080), QRect(-1920, 0, 1920, 1080)};

WindowState state(QSize size, std::optional<QPoint> position = std::nullopt,
                  bool maximised = false) {
    WindowState s;
    s.size = size;
    s.position = position;
    s.maximised = maximised;
    return s;
}

}  // namespace

TEST_CASE("The default window is 960 x 680 with a 360 x 560 minimum", "[desktop][window]") {
    REQUIRE(kDefaultWindowSize == QSize(960, 680));
    REQUIRE(kMinimumWindowSize == QSize(360, 560));
}

TEST_CASE("First run opens at the default size, centred on the primary screen",
          "[desktop][window]") {
    const auto placement = chooseWindowPlacement(std::nullopt, kOneScreen);

    REQUIRE(placement.size == QSize(960, 680));
    REQUIRE(placement.position.has_value());
    REQUIRE(*placement.position == QPoint((1920 - 960) / 2, (1080 - 680) / 2));
    REQUIRE_FALSE(placement.maximised);
}

TEST_CASE("First run on a short screen shrinks to fit but not below the minimum",
          "[desktop][window]") {
    const QList<QRect> shortScreen{QRect(0, 0, 1280, 620)};
    const auto placement = chooseWindowPlacement(std::nullopt, shortScreen);

    REQUIRE(placement.size == QSize(960, 620));

    const QList<QRect> tiny{QRect(0, 0, 1024, 500)};
    REQUIRE(chooseWindowPlacement(std::nullopt, tiny).size.height() == 560);
}

TEST_CASE("A remembered size and position are restored", "[desktop][window]") {
    const auto placement =
        chooseWindowPlacement(state(QSize(1100, 760), QPoint(200, 120)), kOneScreen);

    REQUIRE(placement.size == QSize(1100, 760));
    REQUIRE(placement.position == QPoint(200, 120));
}

TEST_CASE("A remembered position that is off every screen is ignored", "[desktop][window]") {
    // The monitor it was on has been unplugged.
    const auto placement =
        chooseWindowPlacement(state(QSize(1100, 760), QPoint(4000, 300)), kOneScreen);

    REQUIRE(placement.size == QSize(1100, 760));    // the size is still wanted
    REQUIRE_FALSE(placement.position.has_value());  // the window system places it
}

TEST_CASE("A position on a second monitor is kept while that monitor is there",
          "[desktop][window]") {
    const auto with =
        chooseWindowPlacement(state(QSize(960, 680), QPoint(-1500, 100)), kTwoScreens);
    REQUIRE(with.position == QPoint(-1500, 100));

    const auto without =
        chooseWindowPlacement(state(QSize(960, 680), QPoint(-1500, 100)), kOneScreen);
    REQUIRE_FALSE(without.position.has_value());
}

TEST_CASE("A position is kept only if the title bar can still be grabbed", "[desktop][window]") {
    // 960 wide at x = -800: 160 px of the title bar show.
    REQUIRE(chooseWindowPlacement(state(QSize(960, 680), QPoint(-800, 100)), kOneScreen)
                .position.has_value());
    // At x = -900 only 60 px show.
    REQUIRE_FALSE(chooseWindowPlacement(state(QSize(960, 680), QPoint(-900, 100)), kOneScreen)
                      .position.has_value());
    // The title bar below the bottom edge cannot be reached either.
    REQUIRE_FALSE(chooseWindowPlacement(state(QSize(960, 680), QPoint(100, 1070)), kOneScreen)
                      .position.has_value());
}

TEST_CASE("A remembered size below the minimum is raised to it", "[desktop][window]") {
    const auto placement =
        chooseWindowPlacement(state(QSize(200, 300), QPoint(10, 10)), kOneScreen);
    REQUIRE(placement.size == QSize(360, 560));
}

TEST_CASE("A remembered size larger than the screen is reduced to it", "[desktop][window]") {
    const auto placement =
        chooseWindowPlacement(state(QSize(3000, 2000), QPoint(0, 0)), kOneScreen);
    REQUIRE(placement.size == QSize(1920, 1080));
}

TEST_CASE("The maximised state is carried through", "[desktop][window]") {
    const auto placement =
        chooseWindowPlacement(state(QSize(960, 680), QPoint(50, 50), true), kOneScreen);
    REQUIRE(placement.maximised);
    REQUIRE(placement.size == QSize(960, 680));  // the size it returns to
}

TEST_CASE("With no screens to check against the window opens as asked", "[desktop][window]") {
    const auto placement = chooseWindowPlacement(state(QSize(1000, 700), QPoint(5, 5)), {});
    REQUIRE(placement.size == QSize(1000, 700));
}

TEST_CASE("Window state survives a round trip through settings", "[desktop][window]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings settings(dir.filePath("window.ini"), QSettings::IniFormat);

    REQUIRE_FALSE(WindowGeometry::load(settings).has_value());

    WindowGeometry::save(settings, state(QSize(1024, 700), QPoint(30, 40), true));
    const auto loaded = WindowGeometry::load(settings);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size == QSize(1024, 700));
    REQUIRE(loaded->position == QPoint(30, 40));
    REQUIRE(loaded->maximised);

    // A position that is not known is removed, not left stale.
    WindowGeometry::save(settings, state(QSize(800, 600)));
    const auto without = WindowGeometry::load(settings);
    REQUIRE(without.has_value());
    REQUIRE_FALSE(without->position.has_value());
    REQUIRE_FALSE(without->maximised);
}

TEST_CASE("Unusable saved sizes are treated as nothing saved", "[desktop][window]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings settings(dir.filePath("window.ini"), QSettings::IniFormat);

    settings.setValue("window/width", 0);
    settings.setValue("window/height", 700);
    REQUIRE_FALSE(WindowGeometry::load(settings).has_value());

    settings.setValue("window/width", "wide");
    REQUIRE_FALSE(WindowGeometry::load(settings).has_value());
}
