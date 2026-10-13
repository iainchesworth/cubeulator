// The reach gate: after the rest of the suite has walked the app, was there a
// screen, a state or a control it never visited?
//
// It is listed last in tests/system/CMakeLists.txt, so Catch2's declaration
// order runs it after everything else in the same process. Run on its own it
// has nothing to judge and says so.

#include "app.hpp"
#include "reach.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <cstdio>

TEST_CASE("reach: every screen, state and control in the inventory was visited", "[system][reach]") {
    cube::systest::App* app = cube::systest::App::booted();
    if (app == nullptr || app->reach().to_json().value("screens").toArray().size() < 4) {
        SKIP("the reach gate judges a whole run; run cube_system_tests without a filter");
    }

    const auto gaps = app->reach().gaps();
    std::fputs(qPrintable(app->reach().summary()), stdout);
    for (const QString& gap : gaps) {
        UNSCOPED_INFO(gap.toStdString());
    }
    // New screens, new states and new buttons arrive as a failure here until a
    // test visits them; that is the point. A state the environment cannot
    // show goes in reach.cpp with its reason, not in a skipped assertion.
    CHECK(gaps.empty());
}
