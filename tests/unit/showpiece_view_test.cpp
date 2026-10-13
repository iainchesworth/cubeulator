// The showpiece cube - the one on the launch screen and the settings preview.
//
// It is meant to be seen from above: the top, the front and the right side in
// view, and still so however long it has been spinning. Tipped the other way
// it shows the underside, which is the dark side of the lighting and reads as
// a rendering fault; and a tilt that sits inside the idle spin comes round to
// the underside once in every turn.
//
// Cube3DView.qml and the two screens are loaded for real and read back the
// way cube_view_test.cpp does: from the directions the sides face in the
// scene, where a side faces the viewer when its z is positive.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"
#include "qml_test_app.hpp"

#include <cube/solver/cube.hpp>

#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>

#include <algorithm>
#include <memory>
#include <vector>

using cube::solver::Face;
using cube::test::side_direction;
using cube::test::wait_for;

namespace {

[[nodiscard]] std::unique_ptr<QQuickItem> load(const char* type) {
    QQmlComponent component(&cube::test::qml_engine());
    component.loadFromModule("Cubeulator", type);
    if (component.isError()) {
        FAIL(component.errorString().toStdString());
    }
    std::unique_ptr<QQuickItem> item(qobject_cast<QQuickItem*>(component.create()));
    if (item == nullptr) {
        FAIL(component.errorString().toStdString());
    }
    return item;
}

// Every Cube3DView under `screen`: the items that can say which way their
// sides face.
[[nodiscard]] std::vector<QQuickItem*> cubes_in(QQuickItem& screen) {
    std::vector<QQuickItem*> cubes;
    for (QQuickItem* item : screen.findChildren<QQuickItem*>()) {
        if (item->metaObject()->indexOfMethod("sideDirections()") >= 0) {
            cubes.push_back(item);
        }
    }
    return cubes;
}

// Lets the cube finish settling into its viewpoint before it is read.
void settle(QQuickItem* cube) {
    REQUIRE(wait_for([&]() { return !cube->property("viewMoving").toBool(); }));
}

void check_top_front_and_right_in_view(QQuickItem* cube) {
    settle(cube);
    CHECK(side_direction(cube, Face::U).z > 0.3);
    CHECK(side_direction(cube, Face::F).z > 0.3);
    CHECK(side_direction(cube, Face::R).z > 0.3);
    CHECK(side_direction(cube, Face::D).z < -0.3);
    CHECK(side_direction(cube, Face::B).z < -0.3);
    CHECK(side_direction(cube, Face::L).z < -0.3);
}

// Spins the cube fast through more than half a turn and holds it to showing
// the top the whole way round.
void check_top_stays_in_view_while_spinning(QQuickItem* cube) {
    settle(cube);
    REQUIRE(cube->setProperty("autoRotate", true));
    REQUIRE(cube->setProperty("autoRotateSpeed", 20.0));

    double lowest_top = 1.0;
    double front_left = 1.0;
    double front_right = -1.0;
    const bool turned = wait_for(
        [&]() {
            lowest_top = std::min(lowest_top, side_direction(cube, Face::U).z);
            const double across = side_direction(cube, Face::F).x;
            front_left = std::min(front_left, across);
            front_right = std::max(front_right, across);
            // The front has swung from one side of the view to the other.
            return front_right - front_left > 1.9;
        },
        10'000);
    // A check that never saw the cube move would prove nothing.
    REQUIRE(turned);
    CHECK(lowest_top > 0.3);
}

}  // namespace

TEST_CASE("a cube left at its default tilt shows the top, the front and the right side",
          "[qml][showpiece]") {
    const auto cube = load("Cube3DView");
    cube->setProperty("autoRotate", false);
    check_top_front_and_right_in_view(cube.get());
}

TEST_CASE("the launch screen's cube shows the top, the front and the right side",
          "[qml][showpiece]") {
    const auto screen = load("LaunchScreen");
    const auto cubes = cubes_in(*screen);
    // One for the narrow layout and one for the wide.
    REQUIRE(cubes.size() == 2);
    for (QQuickItem* cube : cubes) {
        check_top_front_and_right_in_view(cube);
    }
}

TEST_CASE("the launch screen's cube keeps the top in view as it spins", "[qml][showpiece]") {
    const auto screen = load("LaunchScreen");
    const auto cubes = cubes_in(*screen);
    REQUIRE(cubes.size() == 2);
    for (QQuickItem* cube : cubes) {
        check_top_stays_in_view_while_spinning(cube);
    }
}

TEST_CASE("the settings preview's cube shows the top, the front and the right side",
          "[qml][showpiece]") {
    const auto screen = load("SettingsScreen");
    const auto cubes = cubes_in(*screen);
    REQUIRE_FALSE(cubes.empty());
    for (QQuickItem* cube : cubes) {
        check_top_front_and_right_in_view(cube);
        check_top_stays_in_view_while_spinning(cube);
    }
}
