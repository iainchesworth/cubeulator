#include "app.hpp"

#include "fake_camera.hpp"
#include "reach.hpp"

#include "app_runtime.hpp"
#include "i18n/language_manager.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest/QTest>

#include <cstdio>
#include <stdexcept>

namespace cube::systest {

namespace {

std::unique_ptr<App> g_app;

[[nodiscard]] bool class_is(const QObject* object, const char* prefix) {
    return object != nullptr && QByteArray(object->metaObject()->className()).startsWith(prefix);
}

// QML types declared in a .qml file get a generated class name
// ("AppButton_QMLTYPE_12"), so the QML file's type name is a prefix.
[[nodiscard]] bool is_app_button(const QObject* object) {
    return class_is(object, "AppButton");
}
[[nodiscard]] bool is_cube_view(const QObject* object) {
    return class_is(object, "Cube3DView");
}
[[nodiscard]] bool is_mouse_area(const QObject* object) {
    return class_is(object, "QQuickMouseArea");
}

// The app marks what is pressable with the pointing-hand cursor
// (AppButton, the settings toggles). An empty MouseArea that only swallows
// input - the solving overlay - keeps the arrow and is not a control.
[[nodiscard]] bool is_pointer_area(QQuickItem* item) {
    return is_mouse_area(item) &&
           item->property("cursorShape").toInt() == static_cast<int>(Qt::PointingHandCursor) &&
           item->width() * item->height() > 0.0;
}

void walk(QQuickItem* item, const std::function<void(QQuickItem*)>& visit) {
    visit(item);
    for (QQuickItem* child : item->childItems()) {
        walk(child, visit);
    }
}

}  // namespace

App& App::launched() {
    if (!g_app) {
        g_app.reset(new App);
        g_app->boot();
    }
    return *g_app;
}

App& App::fresh() {
    App& app = launched();
    app.reset();
    return app;
}

App* App::booted() noexcept {
    return g_app.get();
}

void App::shutdown() {
    g_app.reset();
}

App::App() = default;

App::~App() {
    // The engine and the controllers go before the QGuiApplication does.
    reach_.reset();
    runtime_.reset();
}

void App::boot() {
    runtime_ = std::make_unique<cube::app::AppRuntime>();
    if (!runtime_->load()) {
        throw std::runtime_error("Main.qml produced no root object; see the QML errors above");
    }
    window_ = runtime_->window();
    if (window_ == nullptr) {
        throw std::runtime_error("the root object of Main.qml is not a window");
    }

    auto& engine = runtime_->engine();
    app_state_ = engine.singletonInstance<QObject*>("Cubeulator", "AppState");
    settings_state_ = engine.singletonInstance<QObject*>("Cubeulator", "SettingsState");
    theme_ = engine.singletonInstance<QObject*>("Cubeulator", "Theme");
    if (app_state_ == nullptr || settings_state_ == nullptr || theme_ == nullptr) {
        throw std::runtime_error("the AppState, SettingsState or Theme singleton did not load");
    }

    if (!window_->isVisible()) {
        window_->show();
    }
    if (!QTest::qWaitForWindowExposed(window_)) {
        throw std::runtime_error("the window was never exposed: no display to draw on?");
    }

    reach_ = std::make_unique<Reach>(*this);
    reach_->start();
}

void App::reset() {
    QElapsedTimer cost;
    cost.start();
    FakeCamera::instance().clear_script();
    go("launch");
    // Leaving the scan screen stops the camera; wait for it so the next test
    // does not inherit a half-open one.
    auto& scan = runtime_->directScan();
    wait_for([&scan]() { return !scan.starting() && !scan.running(); });
    FakeCamera::instance().reset();
    scan.restart();
    // A case may have unplugged the camera; the controller keeps the list it
    // was last given until it is asked again.
    runtime_->camera().refreshDevices();
    wait_for([this]() { return !runtime_->camera().busy(); });

    reset_language();
    settings_state_->setProperty("themeMode", "light");
    settings_state_->setProperty("shaderStyleIndex", 0);
    // Still, not spinning: a spinning cube is a scene that changes every
    // frame, and a software renderer spends the whole walk drawing it. The
    // cases that need it turning (render_test.cpp) switch it on themselves.
    settings_state_->setProperty("cubeAutoRotate", false);
    settings_state_->setProperty("rotateSpeedIndex", 1);
    settings_state_->setProperty("statsForNerds", false);
    // Putting the method back solves on from the cube in hand; let that land
    // here, not part-way through the next case.
    settings_state_->setProperty("solveMethod", 0);
    wait_for([this]() { return !runtime_->solve().isSolving(); }, 60'000);
    settings_state_->setProperty("graphicsBackend", "auto");
    settings_state_->setProperty("languageIndex", 0);

    resize(kDesktop);
    pump(150);
    // The camera list the launch screen's AppState asked for at start-up.
    wait_for([this]() { return !runtime_->camera().busy(); });
    // What every case pays before it starts. Printed, because on a software
    // renderer it is not small and nothing else says how much of a case's
    // time it is.
    std::fprintf(stderr, "[system]   reset: %.1fs\n", static_cast<double>(cost.elapsed()) / 1000.0);
    std::fflush(stderr);
}

// ---- the application -------------------------------------------------------

QString App::screen() const {
    return app_state_->property("screen").toString();
}

QQuickItem* App::screen_item() const {
    QQuickItem* found = nullptr;
    walk(window_->contentItem(), [&found](QQuickItem* item) {
        if (found == nullptr && class_is(item, "QQuickLoader")) {
            found = qvariant_cast<QQuickItem*>(item->property("item"));
        }
    });
    return found;
}

Layout App::layout() const {
    const int breakpoint = theme_->property("wideBreakpoint").toInt();
    return window_->width() >= breakpoint ? Layout::wide : Layout::narrow;
}

void App::go(const QString& name) {
    app_state_->setProperty("screen", name);
    pump(60);
}

// ---- finding ---------------------------------------------------------------

QList<QQuickItem*> App::find_all(const QString& object_name) const {
    QList<QQuickItem*> out;
    walk(window_->contentItem(), [&](QQuickItem* item) {
        if (item->objectName() == object_name) {
            out.append(item);
        }
    });
    return out;
}

QQuickItem* App::find(const QString& object_name) const {
    QList<QQuickItem*> shown;
    for (QQuickItem* item : find_all(object_name)) {
        if (item->isVisible()) {
            shown.append(item);
        }
    }
    INFO("looking for the visible \"" << object_name.toStdString() << "\"");
    REQUIRE(shown.size() <= 1);
    return shown.isEmpty() ? nullptr : shown.first();
}

QQuickItem* App::find_button(const QString& label) const {
    QQuickItem* found = nullptr;
    walk(window_->contentItem(), [&](QQuickItem* item) {
        if (found == nullptr && is_app_button(item) && item->isVisible() &&
            item->property("text").toString() == label) {
            found = item;
        }
    });
    return found;
}

QQuickItem* App::find_text(const QString& text) const {
    QQuickItem* found = nullptr;
    walk(window_->contentItem(), [&](QQuickItem* item) {
        if (found == nullptr && class_is(item, "QQuickText") && item->isVisible() &&
            item->property("text").toString() == text) {
            found = item;
        }
    });
    return found;
}

QList<QQuickItem*> App::visible_controls() const {
    QList<QQuickItem*> out;
    walk(window_->contentItem(), [&](QQuickItem* item) {
        if (!item->isVisible()) {
            return;
        }
        if (is_app_button(item)) {
            out.append(item);
        } else if (is_pointer_area(item) && !is_app_button(item->parentItem())) {
            out.append(item);
        } else if (is_cube_view(item) && item->property("interactive").toBool()) {
            out.append(item);
        }
    });
    return out;
}

QString App::control_key(QQuickItem* item) const {
    // The nearest name up the tree: a toggle's pointer area is anonymous but
    // the toggle that holds it is not.
    QQuickItem* const root = screen_item();
    for (QQuickItem* up = item; up != nullptr; up = up->parentItem()) {
        if (!up->objectName().isEmpty()) {
            return up->objectName();
        }
        if (up == root) {
            break;
        }
    }
    QString label = item->property("text").toString();
    if (label.isEmpty() && is_cube_view(item)) {
        label = "cube";
    }
    return screen() + ":'" + label + "'";
}

// ---- input -----------------------------------------------------------------

namespace {

// The part of a control that takes the press. A toggle's row is wider than
// the box that reacts, so the press goes to the pointer area inside it.
QQuickItem* click_target(QQuickItem* item) {
    if (is_app_button(item) || is_mouse_area(item) || is_cube_view(item)) {
        return item;
    }
    QQuickItem* inner = nullptr;
    walk(item, [&inner](QQuickItem* child) {
        if (inner == nullptr && child->isVisible() && is_pointer_area(child)) {
            inner = child;
        }
    });
    return inner != nullptr ? inner : item;
}

// A control inside a Flickable (the narrow settings list) may be scrolled out
// of the viewport; scroll it in, as a person's finger would.
void scroll_into_view(QQuickItem* item) {
    for (QQuickItem* up = item->parentItem(); up != nullptr; up = up->parentItem()) {
        if (!class_is(up, "QQuickFlickable")) {
            continue;
        }
        QQuickItem* content = qvariant_cast<QQuickItem*>(up->property("contentItem"));
        const QRectF in_content = content->mapRectFromItem(item, item->boundingRect());
        const qreal view_height = up->height();
        qreal content_y = up->property("contentY").toReal();
        if (in_content.top() < content_y) {
            content_y = in_content.top();
        } else if (in_content.bottom() > content_y + view_height) {
            content_y = in_content.bottom() - view_height;
        }
        up->setProperty("contentY", content_y);
    }
}

}  // namespace

void App::click(QQuickItem* item) {
    REQUIRE(item != nullptr);
    QQuickItem* target = click_target(item);
    scroll_into_view(target);
    pump(20);
    const QPoint at =
        target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    INFO("click on " << control_key(item).toStdString() << " at " << at.x() << "," << at.y());
    REQUIRE(QRect(QPoint(0, 0), window_->size()).contains(at));
    reach_->activated(control_key(item), layout());
    QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, at);
    pump(40);
}

void App::click(const QString& object_name) {
    QQuickItem* item = find(object_name);
    INFO("no visible control named " << object_name.toStdString());
    REQUIRE(item != nullptr);
    click(item);
}

void App::click_button(const QString& label) {
    QQuickItem* item = find_button(label);
    INFO("no visible button labelled " << label.toStdString());
    REQUIRE(item != nullptr);
    click(item);
}

void App::tap(QQuickItem* item) {
    REQUIRE(item != nullptr);
    static QPointingDevice* touch = QTest::createTouchDevice();
    QQuickItem* target = click_target(item);
    scroll_into_view(target);
    pump(20);
    const QPoint at =
        target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    INFO("tap on " << control_key(item).toStdString() << " at " << at.x() << "," << at.y());
    REQUIRE(QRect(QPoint(0, 0), window_->size()).contains(at));
    reach_->activated(control_key(item), layout());
    QTest::touchEvent(window_, touch).press(0, at, window_);
    QTest::touchEvent(window_, touch).release(0, at, window_);
    pump(40);
}

void App::tap(const QString& object_name) {
    QQuickItem* item = find(object_name);
    INFO("no visible control named " << object_name.toStdString());
    REQUIRE(item != nullptr);
    tap(item);
}

void App::drag(QQuickItem* item, QPoint by) {
    REQUIRE(item != nullptr);
    const QPoint from = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    reach_->activated(control_key(item), layout());
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, from);
    constexpr int kSteps = 8;
    for (int i = 1; i <= kSteps; ++i) {
        QTest::mouseMove(window_, from + QPoint(by.x() * i / kSteps, by.y() * i / kSteps));
        pump(16);
    }
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, from + by);
    pump(40);
}

QQuickItem* App::cube_in_view() const {
    for (QQuickItem* control : visible_controls()) {
        if (is_cube_view(control)) {
            return control;
        }
    }
    return nullptr;
}

int App::drag_cube() {
    QQuickItem* cube = cube_in_view();
    REQUIRE(cube != nullptr);
    reach_->activated(control_key(cube), layout());

    const QImage before = grab();
    const QPoint from = cube->mapToScene(QPointF(cube->width() / 2, cube->height() / 2)).toPoint();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 8; ++step) {
        QTest::mouseMove(window_, from + QPoint(12 * step, 4 * step));
        pump(16);
    }
    pump(120);
    const QImage during = grab();
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, from + QPoint(96, 32));

    // The cube's box, in device pixels.
    const qreal ratio = before.devicePixelRatio();
    const QRect box = cube->mapRectToScene(cube->boundingRect()).toAlignedRect();
    const QRect pixels = QRect(QPoint(qRound(box.x() * ratio), qRound(box.y() * ratio)),
                               QSize(qRound(box.width() * ratio), qRound(box.height() * ratio)))
                             .intersected(before.rect());
    int changed = 0;
    for (int y = pixels.top(); y < pixels.bottom(); y += 3) {
        for (int x = pixels.left(); x < pixels.right(); x += 3) {
            changed += before.pixel(x, y) != during.pixel(x, y) ? 1 : 0;
        }
    }
    return changed;
}

void App::key(Qt::Key which) {
    QTest::keyClick(window_, which);
    pump(40);
}

// ---- waiting ---------------------------------------------------------------

bool App::wait_for(const std::function<bool()>& done, int timeout_ms) {
    QElapsedTimer waited;
    waited.start();
    while (!done()) {
        if (waited.elapsed() > timeout_ms) {
            return false;
        }
        QTest::qWait(10);
    }
    return true;
}

void App::pump(int milliseconds) {
    QTest::qWait(milliseconds);
}

bool App::wait_frames(int count, int timeout_ms) {
    int swapped = 0;
    const auto connection =
        QObject::connect(window_, &QQuickWindow::frameSwapped, [&swapped]() { ++swapped; });
    window_->update();
    const bool ok = wait_for(
        [&]() {
            if (swapped < count) {
                window_->update();
            }
            return swapped >= count;
        },
        timeout_ms);
    QObject::disconnect(connection);
    return ok;
}

void App::settle() {
    // Main.qml fades each screen in over 280 ms.
    pump(350);
    wait_frames(2);
}

// ---- window ----------------------------------------------------------------

void App::resize(QSize size) {
    if (window_->size() != size) {
        window_->resize(size);
        wait_for([&]() { return window_->size() == size; }, 3000);
    }
    pump(60);
}

void App::reset_language() {
    auto* language = runtime_->engine().singletonInstance<cube::i18n::LanguageManager*>(
        "Cubeulator", "LanguageManager");
    if (language == nullptr) {
        return;
    }
    // Forget a saved choice, then switch without saving a new one: what a
    // restart would find is a device that has no opinion, and what is on
    // screen is English.
    language->useSystemLanguage();
    language->preview(QStringLiteral("en"));
    pump(30);
}

void App::set_theme(const QString& mode) {
    settings_state_->setProperty("themeMode", mode);
    pump(60);
}

QString App::graphics_api() const {
    QSGRendererInterface* renderer = window_->rendererInterface();
    if (renderer == nullptr) {
        return "none yet";
    }
    switch (renderer->graphicsApi()) {
        case QSGRendererInterface::Software:
            return "Software";
        case QSGRendererInterface::OpenVG:
            return "OpenVG";
        case QSGRendererInterface::OpenGL:
            return "OpenGL";
        case QSGRendererInterface::Direct3D11:
            return "Direct3D11";
        case QSGRendererInterface::Direct3D12:
            return "Direct3D12";
        case QSGRendererInterface::Vulkan:
            return "Vulkan";
        case QSGRendererInterface::Metal:
            return "Metal";
        case QSGRendererInterface::Null:
            return "Null";
        case QSGRendererInterface::Unknown:
            break;
    }
    return "unknown";
}

QImage App::grab() {
    wait_frames(1);
    QImage image = window_->grabWindow();
    REQUIRE_FALSE(image.isNull());
    return image;
}

QString App::screenshot_dir() {
    const QString configured = qEnvironmentVariable("CUBE_SYSTEM_TEST_OUT");
    const QString base = configured.isEmpty()
                             ? QCoreApplication::applicationDirPath() + "/system-test-output"
                             : configured;
    return base + "/screens";
}

QString App::shot(const QString& name) {
    const QString dir = screenshot_dir();
    QDir().mkpath(dir);
    const QString path = dir + "/" + name + ".png";
    const QImage image = grab();
    REQUIRE(image.save(path));
    return path;
}

}  // namespace cube::systest
