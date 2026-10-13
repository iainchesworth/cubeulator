#include "app_runtime.hpp"

#include "i18n/language_manager.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>

namespace cube::app {

AppRuntime::AppRuntime() {
    controllers_.registerWithQml();
    desktop_.registerWithQml();
    controllers_.addImageProviders(engine_);
    // The language - translators, direction, locale - before the first frame.
    // A GUI application exists by now: the controllers' timers and the engine
    // need one as well.
    cube::i18n::install(*qobject_cast<QGuiApplication*>(QCoreApplication::instance()), engine_);
}

bool AppRuntime::load() {
    engine_.loadFromModule("Cubeulator", "Main");
    return !engine_.rootObjects().isEmpty();
}

QQuickWindow* AppRuntime::window() const {
    const auto roots = engine_.rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first());
}

}  // namespace cube::app
