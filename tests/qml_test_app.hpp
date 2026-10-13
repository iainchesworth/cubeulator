#pragma once

#include "controllers/app_controllers.hpp"

#include <QQmlEngine>

namespace cube::test {

// The app's controllers, made once for the whole test run and registered
// with QML as the app registers them, so a test can load one of the app's
// own screens and find every singleton it binds to. Defined in
// qml_test_main.cpp, where they live between the QGuiApplication being made
// and being torn down.
[[nodiscard]] cube::app::AppControllers& app_controllers();

// The one QML engine every test loads into. One, because a controller
// registered as a singleton instance belongs to the first engine that asks
// for it and is refused to any other - as in the app, which has one engine.
[[nodiscard]] QQmlEngine& qml_engine();

}  // namespace cube::test
