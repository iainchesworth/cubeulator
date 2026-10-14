// What the cube's light probe costs the first frame of a screen with a cube.
//
// Qt Quick 3D prefilters a scene's light probe on the first frame of every
// View3D unless it is given one that is prefiltered already, which the cube's
// is now (docs/performance.md, "The light probe"). On a GPU that is not seen;
// on a software renderer it was seconds, and this is the measurement of it: the
// launch screen is brought up from the permission screen, with the probe and
// without (App::Scene3D), and the time to the fifth swapped frame is printed
// for each. Not part of the walk: it does nothing unless CUBE_PROBE_COST is set.
//
//   CUBE_PROBE_COST=1 cube_system_tests "[probe-cost]"
//
// Which renderer it measures is the platform's to pick, as for the app. For a
// software one on a desktop session, the Mesa that Qt bundles for Windows, the
// renderer the Linux CI draws with:
//
//   QT_OPENGL=software QSG_RHI_BACKEND=opengl
//
// CUBE_PROBE_COST_REPS is how many times each is brought up (3). The first of
// a process pays for what the others find compiled.

#include "app.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QQuickWindow>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

using cube::systest::App;

TEST_CASE("what the light probe costs the first frame of the launch cube", "[.][probe-cost]") {
    if (!qEnvironmentVariableIsSet("CUBE_PROBE_COST")) {
        SUCCEED("CUBE_PROBE_COST is not set: nothing measured");
        return;
    }
    const int repetitions = qEnvironmentVariableIsSet("CUBE_PROBE_COST_REPS")
                                ? qEnvironmentVariableIntValue("CUBE_PROBE_COST_REPS")
                                : 3;
    // Without the probe first: a view made while the probe was being left out
    // stays without it, and going to the full scene leaves the screens that
    // have a cube first (App::reset).
    for (const auto scene : {App::Scene3D::cheap, App::Scene3D::full}) {
        for (int rep = 0; rep < repetitions; ++rep) {
            App& app = App::fresh(App::Start::elsewhere, scene);
            std::vector<double> swaps;
            QElapsedTimer timer;
            timer.start();
            const auto connection = QObject::connect(
                &app.window(), &QQuickWindow::frameSwapped,
                [&]() { swaps.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e9); });

            app.go("launch");
            // The launch screen fades in over 280 ms and an idle window draws
            // nothing, so frames are asked for; five swapped after the cube
            // exists are past the first one, whatever it cost.
            const bool done = app.wait_for(
                [&]() {
                    if (swaps.size() < 5) {
                        app.window().update();
                    }
                    return swaps.size() >= 5;
                },
                600000);
            const double wall = static_cast<double>(timer.nsecsElapsed()) / 1.0e9;
            QObject::disconnect(connection);

            double worst_gap = 0.0;
            for (std::size_t i = 1; i < swaps.size(); ++i) {
                worst_gap = std::max(worst_gap, swaps[i] - swaps[i - 1]);
            }
            std::printf(
                "probe-cost: api=%s probe=%s rep=%d first_swap=%.3fs fifth_swap=%.3fs "
                "worst_gap=%.3fs wall=%.3fs done=%d\n",
                qPrintable(app.graphics_api()), scene == App::Scene3D::full ? "on " : "off", rep,
                swaps.empty() ? -1.0 : swaps.front(), swaps.size() >= 5 ? swaps[4] : -1.0,
                worst_gap, wall, done ? 1 : 0);
            std::fflush(stdout);
            CHECK(done);
        }
    }
}
