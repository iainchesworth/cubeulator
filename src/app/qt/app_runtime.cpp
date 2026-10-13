#include "app_runtime.hpp"

#include <QQuickWindow>

namespace cube::app {

AppRuntime::AppRuntime() {
    controllers_.registerWithQml();
    controllers_.addImageProviders(engine_);
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
