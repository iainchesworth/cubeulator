#include "kit_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlProperty>
#include <QTest>
#include <QThread>

#include <cmath>

namespace cube::test {

QObject* singleton(const char* name) {
    QObject* object = engine().singletonInstance<QObject*>("Cubeulator", QString::fromLatin1(name));
    if (object == nullptr) {
        FAIL("no QML singleton " << name << " in the Cubeulator module");
    }
    return object;
}

void set(const char* singletonName, const char* property, const QVariant& value) {
    QObject* object = singleton(singletonName);
    if (!object->setProperty(property, value)) {
        FAIL(singletonName << " has no settable property " << property);
    }
}

QVariant get(const char* singletonName, const char* property) {
    return singleton(singletonName)->property(property);
}

void resetKitState() {
    set("SettingsState", "themeMode", "light");
    set("SettingsState", "language", "en");
    set("SettingsState", "marks", true);
    set("SettingsState", "textSize", "system");
    set("SettingsState", "motion", "full");
    invoke(singleton("SystemTextScale"), "clearOverride");
    invoke(singleton("SystemMotion"), "clearOverride");
    invoke(singleton("SystemInsets"), "clearOverride");
    set("Theme", "windowWidth", 900);
    set("Theme", "windowHeight", 640);
    set("Navigator", "overlays", QVariantList());
    set("Navigator", "guards", QVariantList());
    invoke(singleton("Navigator"), "reset", {"home"});
    QCoreApplication::processEvents();
}

namespace {
std::unique_ptr<QObject>& callerStorage() {
    static std::unique_ptr<QObject> caller;
    return caller;
}
std::unique_ptr<QObject>& probeStorage() {
    static std::unique_ptr<QObject> probe;
    return probe;
}
}  // namespace

QVariant invoke(QObject* object, const char* method, const QVariantList& arguments) {
    auto& caller = callerStorage();
    if (caller == nullptr) {
        QQmlComponent component(&engine());
        component.setData(
            "import QtQuick\n"
            "QtObject { function call(target, method, args) { return target[method].apply(target, args) } }",
            QUrl(QStringLiteral("kit-test-caller.qml")));
        caller.reset(component.create());
        if (caller == nullptr) {
            FAIL(component.errorString().toStdString());
        }
    }
    QVariant result;
    const bool called = QMetaObject::invokeMethod(
        caller.get(), "call", Qt::DirectConnection, Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, QVariant::fromValue<QObject*>(object)), Q_ARG(QVariant, QString::fromLatin1(method)),
        Q_ARG(QVariant, QVariant(arguments)));
    if (!called) {
        FAIL("could not call " << method);
    }
    return result;
}

void releaseFixtures() {
    callerStorage().reset();
    probeStorage().reset();
}

std::unique_ptr<QQuickItem> create(const QString& type, const QVariantMap& properties) {
    QQmlComponent component(&engine());
    component.loadFromModule("Cubeulator", type);
    if (component.isError()) {
        FAIL(component.errorString().toStdString());
    }
    std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
    if (object == nullptr) {
        FAIL(component.errorString().toStdString());
    }
    auto* item = qobject_cast<QQuickItem*>(object.get());
    if (item == nullptr) {
        FAIL(type.toStdString() << " is not an item");
    }
    object.release();
    return std::unique_ptr<QQuickItem>(item);
}

std::unique_ptr<QQuickItem> createFromSource(const QByteArray& qml) {
    QQmlComponent component(&engine());
    component.setData(qml, QUrl(QStringLiteral("kit-test.qml")));
    if (component.isError()) {
        FAIL(component.errorString().toStdString());
    }
    std::unique_ptr<QObject> object(component.create());
    if (object == nullptr) {
        FAIL(component.errorString().toStdString());
    }
    auto* item = qobject_cast<QQuickItem*>(object.get());
    if (item == nullptr) {
        FAIL("the snippet's root is not an item");
    }
    object.release();
    return std::unique_ptr<QQuickItem>(item);
}

Hosted::Hosted(std::unique_ptr<QQuickItem> item, QSize size) : item_(std::move(item)) {
    window_.resize(size);
    item_->setParentItem(window_.contentItem());
    // A control sized by its content keeps its own size; one that fills is
    // told the window's.
    window_.show();
    window_.requestActivate();
    REQUIRE(QTest::qWaitForWindowExposed(&window_));
    // Some platforms never report a window active; the keyboard still reaches
    // it once it is exposed, so this is a courtesy wait, not a requirement.
    static_cast<void>(QTest::qWaitForWindowActive(&window_, 1000));
}

Hosted::~Hosted() {
    // The items go first: they belong to the engine, not the window.
    others_.clear();
    item_.reset();
}

QQuickItem* Hosted::adopt(std::unique_ptr<QQuickItem> item) {
    item->setParentItem(window_.contentItem());
    others_.push_back(std::move(item));
    return others_.back().get();
}

void Hosted::settle() {
    window_.update();
    QTest::qWait(60);
    QCoreApplication::processEvents();
}

void Hosted::key(Qt::Key key, Qt::KeyboardModifiers modifiers) {
    QTest::keyClick(&window_, key, modifiers);
    QCoreApplication::processEvents();
}

void Hosted::click(QQuickItem* target) {
    const QPointF centre = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    QTest::mouseClick(&window_, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    QCoreApplication::processEvents();
}

QQuickItem* Hosted::tab(bool backwards) {
    key(backwards ? Qt::Key_Backtab : Qt::Key_Tab, backwards ? Qt::ShiftModifier : Qt::NoModifier);
    return window_.activeFocusItem();
}

Accessibility accessibilityOf(QObject* item) {
    Accessibility result;
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item);
    if (iface == nullptr) {
        return result;
    }
    result.present = true;
    result.name = iface->text(QAccessible::Name);
    result.description = iface->text(QAccessible::Description);
    result.role = iface->role();
    result.state = iface->state();
    if (QAccessibleActionInterface* actions = iface->actionInterface(); actions != nullptr) {
        result.actions = actions->actionNames();
    }
    return result;
}

bool layoutMirrored(QQuickItem* item) {
    auto& probe = probeStorage();
    if (probe == nullptr) {
        QQmlComponent component(&engine());
        component.setData(
            "import QtQuick\n"
            "QtObject { property var target: null\n"
            "  property bool mirrored: target ? target.LayoutMirroring.enabled : false }",
            QUrl(QStringLiteral("kit-test-probe.qml")));
        probe.reset(component.create());
        if (probe == nullptr) {
            FAIL(component.errorString().toStdString());
        }
    }
    probe->setProperty("target", QVariant());
    probe->setProperty("target", QVariant::fromValue<QObject*>(item));
    return probe->property("mirrored").toBool();
}

namespace {
QColor asColour(const QVariant& v) {
    if (v.metaType() == QMetaType::fromType<QColor>()) {
        return v.value<QColor>();
    }
    return QColor(v.toString());
}
}  // namespace

QList<ContrastPair> contrastPairsOf(QObject* component) {
    QList<ContrastPair> pairs;
    QQmlProperty property(component, QStringLiteral("contrastPairs"), &engine());
    if (!property.isValid()) {
        return pairs;
    }
    const QVariantList list = property.read().toList();
    for (const QVariant& entry : list) {
        const QVariantMap map = entry.toMap();
        pairs.push_back(ContrastPair{map.value("name").toString(), asColour(map.value("fg")),
                                     asColour(map.value("bg")), map.value("target").toDouble()});
    }
    return pairs;
}

QQuickItem* find(QQuickItem* root, const QString& objectName) {
    if (root->objectName() == objectName) {
        return root;
    }
    // The visual tree, not QObject children: items a Repeater makes have no
    // QObject parent to find them by.
    for (QQuickItem* childItem : root->childItems()) {
        if (QQuickItem* found = find(childItem, objectName); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

QList<QQuickItem*> allItems(QQuickItem* root) {
    QList<QQuickItem*> items;
    items.push_back(root);
    for (QQuickItem* childItem : root->childItems()) {
        items.append(allItems(childItem));
    }
    return items;
}

bool waitFor(const std::function<bool()>& done, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return true;
}

double luminance(const QColor& c) {
    const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
}

double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor over(const QColor& top, const QColor& bottom) {
    const double alpha = top.alphaF();
    return QColor::fromRgbF(alpha * top.redF() + (1 - alpha) * bottom.redF(),
                            alpha * top.greenF() + (1 - alpha) * bottom.greenF(),
                            alpha * top.blueF() + (1 - alpha) * bottom.blueF());
}

QColor colour(const QVariant& v) {
    return v.value<QColor>();
}

}  // namespace cube::test
