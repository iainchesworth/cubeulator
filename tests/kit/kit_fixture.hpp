#pragma once

// Helpers the kit's tests share: making a kit item, putting it in a real
// window so it can have keyboard focus, reading it the way a screen reader
// does, and the contrast arithmetic the design's gate is written in
// (docs/design/tools/check_tokens.py).

#include <QAccessible>
#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

namespace cube::test {

// The one QML engine of the run, made in kit_test_main.cpp.
[[nodiscard]] QQmlEngine& engine();

// The folder this run keeps its QSettings in, so a test that points them
// elsewhere can put them back.
[[nodiscard]] QString settingsRoot();

// A QML singleton of the "Cubeulator" module: "Theme", "SettingsState",
// "Navigator", "SystemTextScale"...
[[nodiscard]] QObject* singleton(const char* name);

// Sets a property of a singleton and leaves the engine to settle.
void set(const char* singletonName, const char* property, const QVariant& value);
[[nodiscard]] QVariant get(const char* singletonName, const char* property);

// Puts every setting and override the kit reads back to its default, so one
// test does not leave the next in dark mode at text scale 2.
void resetKitState();

// Calls a method of a QML object (a singleton's function, an item's, a C++
// Q_INVOKABLE) with these arguments and answers what it returns. The call is
// made by a small piece of QML, so arguments are converted the way QML itself
// converts them and fewer arguments than a function declares are padded with
// undefined.
QVariant invoke(QObject* object, const char* method, const QVariantList& arguments = {});

// Drops what the fixture holds on to. Called once, before the engine goes.
void releaseFixtures();

// A kit type from the "Cubeulator" module, made with these properties.
[[nodiscard]] std::unique_ptr<QQuickItem> create(const QString& type, const QVariantMap& properties = {});
// A QML snippet that imports "Cubeulator", for things that are a few kit
// parts together.
[[nodiscard]] std::unique_ptr<QQuickItem> createFromSource(const QByteArray& qml);

// An item in a window of its own, shown, active, with the item given the
// window's size. Keyboard and mouse input go through the window as they do in
// the app.
class Hosted {
public:
    explicit Hosted(std::unique_ptr<QQuickItem> item, QSize size = QSize(480, 320));
    ~Hosted();
    Hosted(const Hosted&) = delete;
    Hosted& operator=(const Hosted&) = delete;

    [[nodiscard]] QQuickItem* item() const { return item_.get(); }
    [[nodiscard]] QQuickWindow* window() { return &window_; }

    // Another item in the same window, alongside the first. The window keeps it
    // alive; it is released with the window.
    QQuickItem* adopt(std::unique_ptr<QQuickItem> item);

    // Lets the window lay out and polish what changed since: positioners
    // settle on the frame after a property changes, as they do in the app.
    void settle();

    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void click(QQuickItem* target);
    // Moves focus the way Tab does; returns the item that now has it.
    QQuickItem* tab(bool backwards = false);

private:
    QQuickWindow window_;
    std::unique_ptr<QQuickItem> item_;
    std::vector<std::unique_ptr<QQuickItem>> others_;
};

// What a screen reader is told about an item.
struct Accessibility {
    bool present = false;
    QString name;
    QString description;
    QAccessible::Role role = QAccessible::NoRole;
    QAccessible::State state;
    QStringList actions;
};
[[nodiscard]] Accessibility accessibilityOf(QObject* item);

// The first item under `root` (or `root` itself) with this objectName,
// looking through the visual tree.
[[nodiscard]] QQuickItem* find(QQuickItem* root, const QString& objectName);
// `root` and every item beneath it, in the visual tree.
[[nodiscard]] QList<QQuickItem*> allItems(QQuickItem* root);

// Whether QML's LayoutMirroring is in effect for this item (an attached
// property, so it cannot be read with QObject::property).
[[nodiscard]] bool layoutMirrored(QQuickItem* item);

// One contrast pairing a component says it draws: what it is, the two
// colours, and the ratio the design's gate asks of it.
struct ContrastPair {
    QString name;
    QColor foreground;
    QColor background;
    double target = 0;
};
// A kit component's `contrastPairs`, empty if it declares none.
[[nodiscard]] QList<ContrastPair> contrastPairsOf(QObject* component);

// Waits, processing events, until `done` is true or the time is up.
bool waitFor(const std::function<bool()>& done, int timeoutMs = 2000);

// ---- Contrast ---------------------------------------------------------------
[[nodiscard]] double luminance(const QColor& colour);
[[nodiscard]] double contrast(const QColor& a, const QColor& b);
// `top` laid over `bottom` at `top`'s own alpha; the result is opaque.
[[nodiscard]] QColor over(const QColor& top, const QColor& bottom);
[[nodiscard]] QColor colour(const QVariant& v);

}  // namespace cube::test
