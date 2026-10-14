#include "platform/web/app_lookup_web.hpp"

#include <emscripten.h>

#include <QJSValue>
#include <QList>
#include <QMetaObject>
#include <QMetaProperty>
#include <QObject>
#include <QQuickWindow>
#include <QString>
#include <QVariant>

#include <cstring>

// Settings that are in the browser's storage before the page goes away.
//
// The app keeps its preferences in QML `Settings` objects (SettingsState.qml).
// Those do not write when a property changes: they note the change and write
// half a second later, so that a slider being dragged is one write and not
// hundreds. On the desktops that is free - the last write is made as the
// object is destroyed at quit, and QSettings is synced then. A page has no
// quit. A tab that is closed or reloaded is torn down without running a
// destructor, so a theme chosen less than half a second before a reload would
// come back as the old one.
//
// Settings::sync() does not help: it syncs the QSettings underneath and leaves
// the changes the object is still holding where they are. What does write at
// once is Settings::setValue(), so this does that, for every value a Settings
// object declares, when the page tells it that it is being hidden or left
// (settings_web.js). A value that was already written is written again, the
// same; the half-second timer then fires and does so once more. Neither
// matters: QSettings on this platform is the browser's localStorage, and each
// write is one synchronous setItem().
//
// Preferences written through QSettings directly (the language, by
// LanguageManager) reach localStorage at the call and need nothing here.
//
// This file is compiled straight into the application target (see
// platform.cmake, cube_platform_deploy_qt_runtime), not into the platform
// library: nothing in the app refers to it, and an object file in a static
// library that nothing refers to is not linked.

// Provided by settings_web.js.
extern "C" {
void cube_web_settings_install();
}

namespace {

// QtCore's `Settings`, by the class name Qt gives it. (QObject::inherits() is
// the same test along the meta-object chain, which is how a QML-declared
// Settings - a subclass made at run time to carry its properties - is found.)
constexpr const char* kSettingsClass = "QQmlSettings";

// Every Settings object at or under `root`. A QML property that holds one
// (`property Settings x: Settings { ... }`) makes it a child of the object that
// declares it, which is how SettingsState's are found.
void collect(QObject* root, QList<QObject*>& found) {
    if (root == nullptr) {
        return;
    }
    if (root->inherits(kSettingsClass) && !found.contains(root)) {
        found.append(root);
    }
    for (QObject* child : root->findChildren<QObject*>()) {
        if (child->inherits(kSettingsClass) && !found.contains(child)) {
            found.append(child);
        }
    }
}

// The index of the first property a QML file declared on a Settings object.
// Settings' own (category, location) come before it and are not stored; the
// declared ones, and the aliases that stand for other objects' properties, are
// what Settings itself loads and stores, counted the same way.
int first_declared_property(const QMetaObject* meta) {
    for (const QMetaObject* m = meta; m != nullptr; m = m->superClass()) {
        if (std::strcmp(m->className(), kSettingsClass) == 0) {
            return m->propertyCount();
        }
    }
    return meta->propertyCount();
}

void write_through(QObject* settings) {
    const QMetaObject* meta = settings->metaObject();
    for (int i = first_declared_property(meta); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (!property.isReadable()) {
            continue;
        }
        QVariant value = property.read(settings);
        if (value.metaType() == QMetaType::fromType<QJSValue>()) {
            value = value.value<QJSValue>().toVariant();
        }
        QMetaObject::invokeMethod(settings, "setValue",
                                  Q_ARG(QString, QString::fromUtf8(property.name())),
                                  Q_ARG(QVariant, value));
    }
    QMetaObject::invokeMethod(settings, "sync");
}

// Registers the page's listeners when the module starts, from the main thread,
// before main().
struct Installer {
    Installer() { cube_web_settings_install(); }
};
const Installer installer;

}  // namespace

extern "C" {

// Called by the page, on the main thread - which is the Qt thread here - when
// it is hidden or being left. Before the window exists there is nothing to
// write, and nothing has changed.
EMSCRIPTEN_KEEPALIVE void cube_web_settings_flush() {
    using cube::platform::web::engine_of;
    using cube::platform::web::main_window;
    using cube::platform::web::singleton;

    QQuickWindow* window = main_window();
    if (window == nullptr) {
        return;
    }

    // The ones SettingsState declares, and any a screen declares for itself.
    QList<QObject*> found;
    collect(singleton(engine_of(window), QStringLiteral("SettingsState")), found);
    collect(window, found);

    for (QObject* settings : found) {
        write_through(settings);
    }
}

}  // extern "C"
