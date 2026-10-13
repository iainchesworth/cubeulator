#pragma once

#include "controllers/app_controllers.hpp"
#include "i18n/language_manager.hpp"

#include <QQmlEngine>

namespace cube::i18n_test {

// The one engine every screen test loads into (a controller registered as a
// singleton instance belongs to the first engine that asks for it), with the
// app's own controllers and the language manager behind it, made in
// screen_test_main.cpp as the app makes them.
[[nodiscard]] QQmlEngine& engine();
[[nodiscard]] cube::app::AppControllers& controllers();
[[nodiscard]] cube::i18n::LanguageManager& language();

}  // namespace cube::i18n_test
