#pragma once

#include <QStringList>

namespace cube::app::kit {

// Registers the fonts compiled into the app under :/fonts with Qt's font
// database, so that they are there for any text that asks for them by family
// or falls back to them for a script nothing else covers. Needs a
// QGuiApplication. Safe to call twice: families already registered by an
// earlier call are not added again.
//
// What is under :/fonts is the build's own: a platform whose own fonts are not
// enough adds some through its fragment (the browser build has none, so it
// bundles its Japanese and Chinese; src/app/platform/web/fonts). Nothing under
// :/fonts is not an error: most builds rely on the system's.
//
// Returns the family names that are now available from the bundle, sorted.
QStringList installBundledFonts();

}  // namespace cube::app::kit
