#pragma once

#include <QGuiApplication>
#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QWindow>

// How platform code in this directory reaches the running app: its window, the
// engine that loaded Main.qml and the QML singletons (the "Cubeulator" module's
// AppState, SettingsState, ...).
//
// Nothing in the app hands these to the platform layer - main() owns the
// runtime - so they are found the way a person's eyes find them: from the
// window that is on screen. The test driver (driver_web.cpp) and the settings
// flush (settings_web.cpp) both start here. Everything is called on the Qt
// thread, which on this platform is the browser's main thread.
namespace cube::platform::web {

// The Qt Quick window that is showing the app, or null before there is one.
inline QQuickWindow* main_window() {
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (auto* quick = qobject_cast<QQuickWindow*>(window)) {
            return quick;
        }
    }
    return nullptr;
}

namespace detail {

// The engine that loaded the app. The window and the items Qt Quick Controls
// make for it are made from C++ and have none; the app's own items, made from
// its QML, do.
inline QQmlEngine* engine_of_item(const QQuickItem* item) {
    if (QQmlEngine* engine = qmlEngine(item)) {
        return engine;
    }
    for (const QQuickItem* child : item->childItems()) {
        if (QQmlEngine* engine = engine_of_item(child)) {
            return engine;
        }
    }
    return nullptr;
}

}  // namespace detail

inline QQmlEngine* engine_of(QQuickWindow* window) {
    if (window == nullptr) {
        return nullptr;
    }
    if (QQmlEngine* engine = qmlEngine(window)) {
        return engine;
    }
    return detail::engine_of_item(window->contentItem());
}

// A singleton of the "Cubeulator" module by name, or null.
inline QObject* singleton(QQmlEngine* engine, const QString& name) {
    if (engine == nullptr) {
        return nullptr;
    }
    return engine->singletonInstance<QObject*>("Cubeulator", name.toLatin1().constData());
}

}  // namespace cube::platform::web
