#pragma once

// The application, booted for a test: the real Main.qml, the real controllers
// (cube::app::AppRuntime, the class main() uses), a QQuickWindow that is
// really shown and really rendered. The only thing that is not the shipped
// program is what is underneath the platform camera (fake_camera.hpp).
//
// Tests drive it the way a person does - mouse, touch and key events sent to
// the window, controls found by objectName - and read back what a person
// would see: which screen is up, what is on it, what a grab of the window
// looks like.

#include <QImage>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QVariant>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

class QObject;
class QQuickItem;
class QQuickWindow;

namespace cube::app {
class AppRuntime;
}

namespace cube::systest {

class Reach;

// Which of the two layouts a screen is in (Theme.wideBreakpoint, in QML).
enum class Layout { narrow, wide };

[[nodiscard]] inline const char* to_string(Layout layout) noexcept {
    return layout == Layout::wide ? "wide" : "narrow";
}

// A window size on each side of the breakpoint: a phone held upright, and a
// desktop window the shape the app opens at.
inline constexpr QSize kPhone{390, 844};
inline constexpr QSize kDesktop{900, 640};

class App {
public:
    // Boots the application on first use (one per process: the controllers
    // are registered as QML singletons, which is process-global). Every call
    // puts it back at a known baseline - launch screen, light theme, desktop
    // size, camera idle and plugged in - so each test starts from the same
    // place however the last one ended.
    static App& fresh();
    // Boots the application and leaves it exactly as start-up made it: no
    // reset. For what only a launch can show (what was read from the store).
    static App& launched();
    // The booted instance, or nullptr if nothing has used it yet.
    static App* booted() noexcept;
    // Tears the application down. main() calls it before the QGuiApplication
    // goes, which the engine and controllers must not outlive.
    static void shutdown();

    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ---- the application ---------------------------------------------------
    [[nodiscard]] cube::app::AppRuntime& runtime() noexcept { return *runtime_; }
    [[nodiscard]] QQuickWindow& window() noexcept { return *window_; }
    [[nodiscard]] QObject& app_state() noexcept { return *app_state_; }
    [[nodiscard]] QObject& settings_state() noexcept { return *settings_state_; }
    [[nodiscard]] QObject& theme() noexcept { return *theme_; }
    [[nodiscard]] Reach& reach() noexcept { return *reach_; }

    // AppState.screen: launch | permission | scan | solve | settings | complete.
    [[nodiscard]] QString screen() const;
    // The screen's own root item, as the Loader in Main.qml holds it.
    [[nodiscard]] QQuickItem* screen_item() const;
    // Which layout the screen on show chose for itself.
    [[nodiscard]] Layout layout() const;
    // Sets AppState.screen directly. For screens no control on a desktop
    // reaches (permission, complete), and only for those.
    void go(const QString& name);

    // ---- finding -----------------------------------------------------------
    // Every item with this objectName, on show or not. Each screen builds a
    // narrow and a wide layout side by side, so a name can match twice.
    [[nodiscard]] QList<QQuickItem*> find_all(const QString& object_name) const;
    // The one that is on show, or nullptr. Fails the test if two are.
    [[nodiscard]] QQuickItem* find(const QString& object_name) const;
    // A visible AppButton by its label, for screens whose controls have no
    // objectName yet (see tests/system/README in docs/testing.md).
    [[nodiscard]] QQuickItem* find_button(const QString& label) const;
    // A visible Text showing exactly this string.
    [[nodiscard]] QQuickItem* find_text(const QString& text) const;
    // Every control a person could press on the screen now, as a test would
    // discover them: AppButtons, pointer-cursor MouseAreas (the toggles) and
    // the draggable cube.
    [[nodiscard]] QList<QQuickItem*> visible_controls() const;
    // The name a control is known by in reach reports.
    [[nodiscard]] QString control_key(QQuickItem* item) const;

    // ---- input -------------------------------------------------------------
    void click(QQuickItem* item);  // a mouse click on its centre
    void click(const QString& object_name);
    void click_button(const QString& label);
    void tap(QQuickItem* item);  // a finger: touch press and release
    void tap(const QString& object_name);
    void drag(QQuickItem* item, QPoint by);  // press, move, release
    void key(Qt::Key which);                 // to the window's focus item

    // ---- waiting -----------------------------------------------------------
    // Runs the event loop until `done` holds. False if `timeout_ms` passed.
    bool wait_for(const std::function<bool()>& done, int timeout_ms = 15000);
    // Lets animations and queued signals play out.
    void pump(int milliseconds);
    // Waits for `count` frames to be swapped to the display.
    bool wait_frames(int count = 2, int timeout_ms = 5000);
    // Everything the screen on show animates in has landed.
    void settle();

    // ---- window ------------------------------------------------------------
    void resize(QSize size);
    void set_theme(const QString& mode);  // "system" | "light" | "dark"
    // The scene graph's graphics API ("OpenGL", "Direct3D11", "Software"...).
    [[nodiscard]] QString graphics_api() const;
    // The window as rendered, in device pixels.
    [[nodiscard]] QImage grab();
    // Writes grab() to <screenshot dir>/<name>.png and returns the path.
    QString shot(const QString& name);
    [[nodiscard]] static QString screenshot_dir();

private:
    App();
    void boot();
    void reset();

    std::unique_ptr<cube::app::AppRuntime> runtime_;
    QQuickWindow* window_ = nullptr;
    QObject* app_state_ = nullptr;
    QObject* settings_state_ = nullptr;
    QObject* theme_ = nullptr;
    std::unique_ptr<Reach> reach_;
};

}  // namespace cube::systest
