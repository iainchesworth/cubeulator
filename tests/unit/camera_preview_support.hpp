#pragma once

// What the tests of the screens that show the camera's picture have in common:
// a drawn side to show it, the camera put in the state they want and put back,
// and a way to find the screen's named parts.

#include "drawn_cube.hpp"
#include "qml_test_app.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

#include <opencv2/core.hpp>

#include <QQuickItem>
#include <QString>

#include <vector>

namespace cube::test {

// A side held square to the camera and off to the left of the frame, so that
// a flip moves every sticker a long way.
[[nodiscard]] inline cv::Mat frame_of_side() {
    const solver::Cube scrambled = solver::random_scramble(7);
    // Qualified: the solver has an outside_view of its own, which argument-
    // dependent lookup would otherwise offer beside this one.
    return draw_side(cube::test::outside_view(scrambled, solver::Face::F),
                     Pose{.centre = {190.0F, 170.0F}, .pitch = 46.0F});
}

// The camera in the state these tests want it - reading the side in view and
// leaving any scan alone - and put back as it was found.
class WatchingCamera {
public:
    WatchingCamera() : camera_(app_controllers().directScan) { camera_.setWatching(true); }
    ~WatchingCamera() {
        camera_.setWatching(false);
        camera_.restart();
    }
    WatchingCamera(const WatchingCamera&) = delete;
    WatchingCamera& operator=(const WatchingCamera&) = delete;

    [[nodiscard]] cube::app::DirectScan& scan() const { return camera_; }

private:
    cube::app::DirectScan& camera_;
};

inline void collect_named(QQuickItem* item, const QString& name, std::vector<QQuickItem*>& found) {
    if (item->objectName() == name) {
        found.push_back(item);
    }
    for (QQuickItem* child : item->childItems()) {
        collect_named(child, name, found);
    }
}

// Every item called `name` under `root`, through the visual tree (a
// Repeater's items are found there as well as a declared one).
[[nodiscard]] inline std::vector<QQuickItem*> items_named(QQuickItem* root, const char* name) {
    std::vector<QQuickItem*> found;
    collect_named(root, QString::fromLatin1(name), found);
    return found;
}

}  // namespace cube::test
