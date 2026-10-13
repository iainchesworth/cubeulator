#pragma once

// Pressing everything on a screen once.
//
// A screen's controls are discovered from the live item tree, not listed by a
// test, so a button added tomorrow is pressed by the sweeps below with no test
// change - and what it does is for a case of its own to assert. The sweep
// only promises that every control on show was pressed, in this layout, and
// the app was still there afterwards.

#include "app.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QSet>
#include <QString>
#include <QStringList>

namespace cube::systest {

// Controls a sweep leaves alone, by key (see App::control_key).
struct SweepOptions {
    QSet<QString> skip;
    // How many passes: a control that was disabled in one (Previous, at the
    // start of a solution) may be enabled by a press later in the next.
    int passes = 3;
};

namespace detail {

[[nodiscard]] inline bool is_toggle(QQuickItem* control) {
    // A toggle is a pointer area the app draws, not an AppButton or a cube.
    return QByteArray(control->metaObject()->className()).startsWith("QQuickMouseArea");
}

[[nodiscard]] inline bool is_enabled(QQuickItem* control) {
    const QVariant own = control->property("enabled");
    return control->isEnabled() && (!own.isValid() || own.toBool());
}

[[nodiscard]] inline QQuickItem* find_control(App& app, const QString& key) {
    for (QQuickItem* candidate : app.visible_controls()) {
        if (app.control_key(candidate) == key) {
            return candidate;
        }
    }
    return nullptr;
}

}  // namespace detail

// Presses every enabled control visible on `screen` once, and returns their
// keys. A press that leaves the screen is undone with App::go, so the next
// control can still be reached. Toggles go last and are pressed twice, so
// what they switch is put back (the idle-spin toggle disables the speed
// chips; a sweep that left it off would never reach them).
inline QStringList press_every_control(App& app, const QString& screen,
                                       const SweepOptions& options = {}) {
    QStringList pressed;
    QStringList toggles;

    const auto keys = [&]() {
        QStringList out;
        for (QQuickItem* control : app.visible_controls()) {
            const QString key = app.control_key(control);
            if (!out.contains(key) && !options.skip.contains(key)) {
                out.append(key);
            }
        }
        return out;
    };

    const auto press = [&](const QString& key) {
        QQuickItem* control = detail::find_control(app, key);
        if (control == nullptr || !detail::is_enabled(control)) {
            return false;
        }
        // A control below the bottom of the window has nowhere for a press to
        // land. layout_test.cpp lists those; the sweep leaves them alone.
        const QPointF centre =
            control->mapToScene(QPointF(control->width() / 2, control->height() / 2));
        if (!QRectF(QPointF(0, 0), QSizeF(app.window().size())).contains(centre)) {
            return false;
        }
        INFO("pressing " << key.toStdString() << " on " << screen.toStdString());
        app.click(control);
        if (app.screen() != screen) {
            app.go(screen);
            app.settle();
        }
        return true;
    };

    for (int pass = 0; pass < options.passes; ++pass) {
        bool any = false;
        for (const QString& key : keys()) {
            if (pressed.contains(key) || toggles.contains(key)) {
                continue;
            }
            QQuickItem* control = detail::find_control(app, key);
            if (control != nullptr && detail::is_toggle(control)) {
                toggles.append(key);
                continue;
            }
            if (press(key)) {
                pressed.append(key);
                any = true;
            }
        }
        if (!any) {
            break;
        }
    }

    for (const QString& key : toggles) {
        if (press(key) && press(key)) {
            pressed.append(key);
        }
    }
    return pressed;
}

}  // namespace cube::systest
