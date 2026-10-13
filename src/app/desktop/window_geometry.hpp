#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSettings>
#include <QSize>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>

#include <optional>

namespace cube::app {

// docs/design/platforms.md, "Window": default 960 x 680, minimum 360 x 560;
// size, position and maximised state are remembered; a remembered position
// that is off every screen is ignored.
inline constexpr QSize kDefaultWindowSize{960, 680};
inline constexpr QSize kMinimumWindowSize{360, 560};

struct WindowState {
    QSize size;                      // the normal (not maximised) size
    std::optional<QPoint> position;  // top-left of that, in logical pixels
    bool maximised = false;
};

struct WindowPlacement {
    QSize size;
    std::optional<QPoint> position;  // empty: let the window system place it
    bool maximised = false;
};

// Pure: what to open with, given what was remembered (if anything) and the
// available area of each screen. `screens` is in the window system's own
// coordinates, primary first.
//
//  * No memory: preferred size, centred on the primary screen.
//  * Size is raised to `minimum` and lowered to fit the screen it lands on.
//  * A position is kept only if enough of the title bar is on some screen to
//    grab it; otherwise it is dropped and the window system places the window.
[[nodiscard]] WindowPlacement chooseWindowPlacement(const std::optional<WindowState>& saved,
                                                    const QList<QRect>& screens,
                                                    QSize preferred = kDefaultWindowSize,
                                                    QSize minimum = kMinimumWindowSize);

// Remembering and restoring one top-level window.
//
//   WindowGeometry geometry;
//   engine.setInitialProperties(geometry.initialProperties());   // before the window exists
//   engine.loadFromModule(...);
//   geometry.track(rootWindow);                                  // after
//
// The size and position are applied as initial properties, so the window
// is created where it belongs and never appears at one size and jumps to
// another.
class WindowGeometry : public QObject {
    Q_OBJECT

public:
    explicit WindowGeometry(QObject* parent = nullptr);

    // Reads what was saved (QSettings group "window") and returns the root
    // window's initial properties: width, height, minimumWidth, minimumHeight
    // and, where known, x, y and visibility.
    [[nodiscard]] QVariantMap initialProperties() const;
    [[nodiscard]] QVariantMap initialProperties(const QList<QRect>& screens) const;

    // Starts following the window and saving what it does.
    void track(QWindow* window);

    // Exposed for tests.
    [[nodiscard]] static std::optional<WindowState> load(QSettings& settings);
    static void save(QSettings& settings, const WindowState& state);

private:
    void changed();
    void capture();
    void persist();

    QWindow* window_ = nullptr;
    QTimer settle_;
    WindowState state_;
    bool haveState_ = false;
};

}  // namespace cube::app
