#include "reach.hpp"

#include "app_runtime.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QQuickItem>
#include <QQuickWindow>

namespace cube::systest {

// ---------------------------------------------------------------------------
// The inventory. Screens are what Main.qml routes to; states are what a person
// would tell apart on them. Each state is a predicate over properties the app
// already exposes - the sampler evaluates them, no test claims them.
// ---------------------------------------------------------------------------

const std::vector<ScreenDef>& screen_inventory() {
    static const std::vector<ScreenDef> screens = {
        {"launch", "LaunchScreen.qml", ""},
        {"permission", "PermissionScreen.qml",
         "no control leads here on a desktop: AppState.beginScan() skips it unless the platform is "
         "mobile"},
        {"scan", "DirectScanScreen.qml", ""},
        {"solve", "SolveScreen.qml", ""},
        {"settings", "SettingsScreen.qml", ""},
        {"complete", "CompleteScreen.qml", ""},
    };
    return screens;
}

const std::map<QString, QString>& dead_screen_files() {
    // None now. ScanScreen.qml, the previous scan screen, was listed here
    // until it was removed with the pipeline it showed.
    static const std::map<QString, QString> dead = {};
    return dead;
}

const std::map<QString, QString>& unpressable_controls() {
    // None now. permission.notNow was below the bottom of a phone-sized
    // window until the permission screen was rebuilt; the layout case that
    // listed it failed the day that landed, which is what it is for.
    static const std::map<QString, QString> controls = {};
    return controls;
}

const std::vector<StateDef>& state_inventory() {
    static const std::vector<StateDef> states = {
        {"launch", "idle", [](App& a) { return a.screen() == "launch"; }, ""},

        {"permission", "asking", [](App& a) { return a.screen() == "permission"; }, ""},
        {"permission", "denied",
         [](App& a) {
             QObject* permission =
                 a.runtime().engine().singletonInstance<QObject*>("Cubeulator", "CameraPermission");
             return a.screen() == "permission" && permission != nullptr &&
                    permission->property("status").toInt() == 1;
         },
         "Qt reports camera access as granted on desktop platforms, so the denied wording never "
         "shows"},

        {"scan", "opening-camera",
         [](App& a) {
             auto& scan = a.runtime().directScan();
             return a.screen() == "scan" && !scan.running() && scan.cameraError().isEmpty();
         },
         ""},
        {"scan", "camera-unavailable",
         [](App& a) {
             return a.screen() == "scan" && !a.runtime().directScan().cameraError().isEmpty();
         },
         ""},
        {"scan", "setting-up-camera",
         [](App& a) {
             auto& scan = a.runtime().directScan();
             return a.screen() == "scan" && scan.running() && scan.settingUp();
         },
         ""},
        {"scan", "looking-for-a-side",
         [](App& a) {
             auto& scan = a.runtime().directScan();
             return a.screen() == "scan" && scan.running() && !scan.settingUp() &&
                    scan.stickerPoints().isEmpty() && scan.sidesRead() == 0;
         },
         ""},
        {"scan", "side-in-view",
         [](App& a) {
             auto& scan = a.runtime().directScan();
             return a.screen() == "scan" && scan.stickerPoints().size() == 9 && !scan.reading();
         },
         ""},
        {"scan", "reading-a-side",
         [](App& a) { return a.screen() == "scan" && a.runtime().directScan().reading(); }, ""},
        {"scan", "some-sides-read",
         [](App& a) {
             auto& scan = a.runtime().directScan();
             return a.screen() == "scan" && scan.sidesRead() >= 1 && scan.sidesRead() <= 5;
         },
         ""},
        {"scan", "cube-read",
         [](App& a) { return a.screen() == "scan" && a.runtime().directScan().complete(); }, ""},
        {"scan", "sides-conflict",
         [](App& a) { return a.screen() == "scan" && a.runtime().directScan().conflicted(); },
         "needs six sides that are each read cleanly yet do not make a cube; the drawn frames are "
         "consistent, so the suite does not stage a misread"},

        {"solve", "solving",
         [](App& a) { return a.screen() == "solve" && a.runtime().solve().isSolving(); },
         "AppState only leaves the scan screen once the solution is ready, so the spinner never "
         "shows "
         "on this path"},
        {"solve", "watching-the-camera",
         [](App& a) { return a.screen() == "solve" && a.runtime().directScan().watching(); }, ""},
        {"solve", "at-start",
         [](App& a) {
             auto& solve = a.runtime().solve();
             return a.screen() == "solve" && !solve.isSolving() && solve.atStart() &&
                    solve.totalMoves() > 0;
         },
         ""},
        {"solve", "mid-solution",
         [](App& a) {
             auto& solve = a.runtime().solve();
             return a.screen() == "solve" && !solve.atStart() && !solve.atEnd();
         },
         ""},
        {"solve", "at-end",
         [](App& a) {
             auto& solve = a.runtime().solve();
             return a.screen() == "solve" && solve.atEnd() && solve.totalMoves() > 0;
         },
         ""},

        {"settings", "cameras-listed",
         [](App& a) {
             return a.screen() == "settings" && !a.runtime().camera().devices().isEmpty();
         },
         ""},
        {"settings", "no-camera-found",
         [](App& a) {
             auto& camera = a.runtime().camera();
             return a.screen() == "settings" && camera.devices().isEmpty() && !camera.busy();
         },
         ""},
        {"settings", "restart-for-backend",
         [](App& a) {
             return a.screen() == "settings" &&
                    a.find_text(qtTrId("settings.dev.restart")) != nullptr;
         },
         ""},

        {"complete", "shown", [](App& a) { return a.screen() == "complete"; }, ""},
    };
    return states;
}

// ---------------------------------------------------------------------------

Reach::Reach(App& app) : app_(app) {
    sampler_.setInterval(50);
    QObject::connect(&sampler_, &QTimer::timeout, [this]() { sample(); });
}

void Reach::start() {
    sampler_.start();
}

void Reach::activated(const QString& key, Layout layout) {
    activated_.insert((key + "@" + to_string(layout)).toStdString());
}

void Reach::sample() {
    const QString screen = app_.screen();
    const Layout layout = app_.layout();
    screens_.insert((screen + "@" + to_string(layout)).toStdString());

    for (const StateDef& state : state_inventory()) {
        if (state.screen == screen && state.holds(app_)) {
            states_.insert((state.screen + "." + state.name).toStdString());
        }
    }

    for (QQuickItem* control : app_.visible_controls()) {
        const std::string key = (app_.control_key(control) + "@" + to_string(layout)).toStdString();
        // AppButton declares its own `enabled`; the item's own says whether a
        // parent switched the whole row off.
        const QVariant enabled = control->property("enabled");
        const bool is_enabled = control->isEnabled() && (!enabled.isValid() || enabled.toBool());
        if (is_enabled) {
            discovered_.insert(key);
            disabled_.erase(key);
        } else if (discovered_.count(key) == 0) {
            disabled_.insert(key);
        }
    }
}

std::vector<QString> Reach::gaps() const {
    std::vector<QString> gaps;

    // A screen file nobody listed is a screen nobody tested.
    std::set<QString> known;
    for (const ScreenDef& screen : screen_inventory()) {
        known.insert(screen.qml_file);
    }
    for (const auto& [file, why] : dead_screen_files()) {
        known.insert(file);
    }
    for (const QString& file : QDir(":/qt/qml/Cubeulator").entryList({"*Screen.qml"})) {
        if (known.count(file) == 0) {
            gaps.push_back("screen file " + file + " is in the app but not in the reach inventory");
        }
    }

    for (const ScreenDef& screen : screen_inventory()) {
        for (const char* layout : {"narrow", "wide"}) {
            if (screens_.count((screen.name + "@" + layout).toStdString()) == 0) {
                gaps.push_back("screen " + screen.name + " never shown in the " + layout +
                               " layout");
            }
        }
    }

    for (const StateDef& state : state_inventory()) {
        if (state.cannot_reach_because.isEmpty() &&
            states_.count((state.screen + "." + state.name).toStdString()) == 0) {
            gaps.push_back("state " + state.screen + "." + state.name + " never seen");
        }
    }

    for (const std::string& control : discovered_) {
        if (activated_.count(control) == 0 &&
            unpressable_controls().count(QString::fromStdString(control)) == 0) {
            gaps.push_back("control " + QString::fromStdString(control) +
                           " was on screen but never pressed");
        }
    }
    return gaps;
}

std::vector<QString> Reach::known_gaps() const {
    std::vector<QString> out;
    for (const ScreenDef& screen : screen_inventory()) {
        if (!screen.reached_by_ui.isEmpty()) {
            out.push_back("screen " + screen.name +
                          " is put up by App::go, not by a control: " + screen.reached_by_ui);
        }
    }
    for (const auto& [file, why] : dead_screen_files()) {
        out.push_back("file " + file + " is dead: " + why);
    }
    for (const StateDef& state : state_inventory()) {
        if (!state.cannot_reach_because.isEmpty()) {
            const bool seen = states_.count((state.screen + "." + state.name).toStdString()) != 0;
            out.push_back("state " + state.screen + "." + state.name +
                          (seen ? " (seen anyway) - " : " not reachable here - ") +
                          state.cannot_reach_because);
        }
    }
    for (const auto& [control, why] : unpressable_controls()) {
        const bool pressed = activated_.count(control.toStdString()) != 0;
        out.push_back("control " + control +
                      (pressed ? " (pressed after all: remove it from the list) - "
                               : " cannot be pressed - ") +
                      why);
    }
    for (const std::string& control : disabled_) {
        out.push_back("control " + QString::fromStdString(control) + " only ever seen disabled");
    }
    return out;
}

QString Reach::summary() const {
    QString text;
    const auto line = [&text](const QString& s) { text += s + "\n"; };

    int shown = 0;
    for (const ScreenDef& screen : screen_inventory()) {
        for (const char* layout : {"narrow", "wide"}) {
            shown += static_cast<int>(screens_.count((screen.name + "@" + layout).toStdString()));
        }
    }
    const int screen_total = static_cast<int>(screen_inventory().size()) * 2;

    int state_seen = 0;
    int state_total = 0;
    for (const StateDef& state : state_inventory()) {
        if (state.cannot_reach_because.isEmpty()) {
            ++state_total;
            state_seen +=
                static_cast<int>(states_.count((state.screen + "." + state.name).toStdString()));
        }
    }

    int pressed = 0;
    for (const std::string& control : discovered_) {
        pressed += static_cast<int>(activated_.count(control));
    }

    line(QString("reach: screens %1/%2 (narrow and wide), states %3/%4, controls pressed %5/%6")
             .arg(shown)
             .arg(screen_total)
             .arg(state_seen)
             .arg(state_total)
             .arg(pressed)
             .arg(discovered_.size()));
    for (const QString& gap : gaps()) {
        line("  GAP    " + gap);
    }
    for (const QString& gap : known_gaps()) {
        line("  KNOWN  " + gap);
    }
    return text;
}

QJsonObject Reach::to_json() const {
    const auto array = [](const std::set<std::string>& set) {
        QJsonArray out;
        for (const std::string& s : set) {
            out.append(QString::fromStdString(s));
        }
        return out;
    };
    const auto strings = [](const std::vector<QString>& list) {
        QJsonArray out;
        for (const QString& s : list) {
            out.append(s);
        }
        return out;
    };
    return QJsonObject{
        {"screens", array(screens_)},
        {"states", array(states_)},
        {"controlsDiscovered", array(discovered_)},
        {"controlsActivated", array(activated_)},
        {"gaps", strings(gaps())},
        {"knownGaps", strings(known_gaps())},
        {"summary", summary()},
    };
}

}  // namespace cube::systest
