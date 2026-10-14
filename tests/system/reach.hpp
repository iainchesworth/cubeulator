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
// Controls that are on screen and cannot be pressed, as "<control>@<layout>"
// with why. Not hidden: they are reported, and they stop being waived the day
// they can be pressed (the report says so).
[[nodiscard]] const std::map<QString, QString>& unpressable_controls();

// Everything a run saw, as the sets a report holds. A run writes one; the
// reach gate reads one report or merges several (a walk split into shards
// visits different parts of the app in each) and judges the union.
struct Seen {
    std::set<std::string> screens;     // "<screen>@<layout>"
    std::set<std::string> states;      // "<screen>.<state>"
    std::set<std::string> discovered;  // "<control>@<layout>"
    std::set<std::string> disabled;    // controls seen only disabled
    std::set<std::string> activated;   // "<control>@<layout>"

    void merge(const Seen& other);
};

// What was never reached; empty when the inventory is complete.
[[nodiscard]] std::vector<QString> gaps_of(const Seen& seen);
// What cannot be reached here and why (the waivers), for the report.
[[nodiscard]] std::vector<QString> known_gaps_of(const Seen& seen);
// The report, as lines for the log.
[[nodiscard]] QString summary_of(const Seen& seen);
// The report, as JSON for the CI artifact and for the gate to read back.
[[nodiscard]] QJsonObject to_json(const Seen& seen);
[[nodiscard]] Seen seen_from_json(const QJsonObject& report);

class Reach {
public:
    explicit Reach(App& app);

    void start();
    // A control was pressed (from App's input helpers).
    void activated(const QString& key, Layout layout);

    [[nodiscard]] const Seen& seen() const noexcept { return seen_; }
    // The report, as lines for the log and as JSON for the CI artifact.
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QJsonObject to_json() const;
    // What the sampler has cost the app's thread. It walks the item tree on a
    // timer, which a slow renderer's cases pay for in every wait.
    [[nodiscard]] int samples() const noexcept { return samples_; }
    [[nodiscard]] double sampling_ms() const noexcept { return sampling_ms_; }

private:
    void sample();

    App& app_;
    QTimer sampler_;
    Seen seen_;
    int samples_ = 0;
    double sampling_ms_ = 0;
};

}  // namespace cube::systest
