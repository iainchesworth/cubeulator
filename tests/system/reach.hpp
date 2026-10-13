#pragma once

// What the suite reached: which screens (in both layouts), which states of
// each, which controls it pressed - against an inventory of what there is.
//
// This is the stand-in for QML line coverage. gcov cannot see QML or
// JavaScript, and the tool that can (Qt Coco) is a paid product; see
// docs/testing.md. Reach answers the question line coverage would, one level
// up: is there a screen, a state or a button that no test ever visited?
//
// Nothing is self-reported by the tests. A sampler watches the running app:
// the screen and layout on show, a table of state predicates read off the
// controllers (reach_inventory.cpp), and every control the live item tree
// offers. Only a press is recorded by the input helpers in App.

#include "app.hpp"

#include <QJsonObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cube::systest {

// A screen the app routes to (AppState.screen), and where its QML lives.
struct ScreenDef {
    QString name;
    QString qml_file;
    // Set when no control on a desktop reaches it, so the suite has to put it
    // up with App::go. Reported, not waived: it still has to be visited.
    QString reached_by_ui;
};

// A state of a screen, recognised from the app's own properties.
struct StateDef {
    QString screen;
    QString name;
    std::function<bool(App&)> holds;
    // Why this state cannot be put up in this environment, if it cannot. A
    // state with a reason is listed as a known gap, not a failure.
    QString cannot_reach_because;
};

[[nodiscard]] const std::vector<ScreenDef>& screen_inventory();
[[nodiscard]] const std::vector<StateDef>& state_inventory();
// .qml files in the Cubeulator module that no route instantiates, with why.
// A screen file that is in neither list fails the reach check.
[[nodiscard]] const std::map<QString, QString>& dead_screen_files();

class Reach {
public:
    explicit Reach(App& app);

    void start();
    // A control was pressed (from App's input helpers).
    void activated(const QString& key, Layout layout);

    // The report, as lines for the log and as JSON for the CI artifact.
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QJsonObject to_json() const;
    // What was never reached; empty when the inventory is complete.
    [[nodiscard]] std::vector<QString> gaps() const;
    // What cannot be reached here and why (the waivers), for the report.
    [[nodiscard]] std::vector<QString> known_gaps() const;

private:
    void sample();

    App& app_;
    QTimer sampler_;
    std::set<std::string> screens_;     // "<screen>@<layout>"
    std::set<std::string> states_;      // "<screen>.<state>"
    std::set<std::string> discovered_;  // "<control>@<layout>"
    std::set<std::string> disabled_;    // controls seen only disabled
    std::set<std::string> activated_;   // "<control>@<layout>"
};

}  // namespace cube::systest
