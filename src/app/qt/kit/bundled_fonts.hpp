#pragma once

#include <QStringList>

namespace cube::app::kit {

// Registers the fonts compiled into the app (qt/fonts/, under :/fonts) with
// Qt's font database, so that the design's type is what is shown whether or
// not the machine has Archivo or IBM Plex installed (docs/design/system.md,
// "Type"). Needs a QGuiApplication. Safe to call twice: families already
// registered by an earlier call are not added again.
//
// Returns the family names that are now available from the bundle, sorted.
QStringList installBundledFonts();

}  // namespace cube::app::kit
