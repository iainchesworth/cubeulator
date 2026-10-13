#include "window_geometry.hpp"

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace cube::app {

namespace {

// How much of the title bar must be on a screen for a remembered position to
// be worth keeping: enough to find the window and drag it.
constexpr int kGrabWidth = 100;
constexpr int kGrabHeight = 24;
constexpr int kTitleBarHeight = 40;

// A gap in the geometry signals this long means the window has stopped
// moving or being resized, and that a maximise or full-screen change has
// had time to land: the signals do not arrive in a guaranteed order.
constexpr int kSettleMs = 250;

bool titleBarReachable(const QPoint& position, int width, const QList<QRect>& screens) {
    const QRect titleBar(position, QSize(width, kTitleBarHeight));
    return std::any_of(screens.begin(), screens.end(), [&](const QRect& screen) {
        const QRect seen = titleBar.intersected(screen);
        return seen.width() >= kGrabWidth && seen.height() >= kGrabHeight;
    });
}

// The screen a rect mostly sits on, falling back to the primary.
QRect screenFor(const QRect& rect, const QList<QRect>& screens) {
    QRect best = screens.first();
    qint64 bestArea = -1;
    for (const QRect& screen : screens) {
        const QRect overlap = rect.intersected(screen);
        const qint64 area = static_cast<qint64>(overlap.width()) * overlap.height();
        if (area > bestArea) {
            best = screen;
            bestArea = area;
        }
    }
    return best;
}

}  // namespace

WindowPlacement chooseWindowPlacement(const std::optional<WindowState>& saved,
                                      const QList<QRect>& screens, QSize preferred, QSize minimum) {
    WindowPlacement placement;

    QSize size = (saved && saved->size.isValid()) ? saved->size : preferred;
    size = size.expandedTo(minimum);

    if (screens.isEmpty()) {
        // Nothing to check against (not expected on a desktop). Open as asked.
        placement.size = size;
        placement.maximised = saved && saved->maximised;
        return placement;
    }

    const bool havePosition = saved && saved->position.has_value();
    if (havePosition && titleBarReachable(*saved->position, size.width(), screens)) {
        placement.position = saved->position;
        // Fit the screen the window will be on, but never below the minimum.
        const QRect where = screenFor(QRect(*saved->position, size), screens);
        size = size.boundedTo(where.size()).expandedTo(minimum);
    } else {
        const QRect primary = screens.first();
        size = size.boundedTo(primary.size()).expandedTo(minimum);
        if (!saved) {
            // First run: centred, rather than wherever the window system
            // happens to cascade a new window.
            placement.position = QPoint(primary.x() + (primary.width() - size.width()) / 2,
                                        primary.y() + (primary.height() - size.height()) / 2);
        }
        // A remembered position that is off every screen is dropped, and
        // the window system places the window.
    }

    placement.size = size;
    placement.maximised = saved && saved->maximised;
    return placement;
}

std::optional<WindowState> WindowGeometry::load(QSettings& settings) {
    if (!settings.contains(QStringLiteral("window/width")) ||
        !settings.contains(QStringLiteral("window/height"))) {
        return std::nullopt;
    }
    WindowState state;
    bool okWidth = false;
    bool okHeight = false;
    const int width = settings.value(QStringLiteral("window/width")).toInt(&okWidth);
    const int height = settings.value(QStringLiteral("window/height")).toInt(&okHeight);
    if (!okWidth || !okHeight || width <= 0 || height <= 0) {
        return std::nullopt;
    }
    state.size = QSize(width, height);

    if (settings.contains(QStringLiteral("window/x")) &&
        settings.contains(QStringLiteral("window/y"))) {
        bool okX = false;
        bool okY = false;
        const int x = settings.value(QStringLiteral("window/x")).toInt(&okX);
        const int y = settings.value(QStringLiteral("window/y")).toInt(&okY);
        if (okX && okY) {
            state.position = QPoint(x, y);
        }
    }
    state.maximised = settings.value(QStringLiteral("window/maximised"), false).toBool();
    return state;
}

void WindowGeometry::save(QSettings& settings, const WindowState& state) {
    settings.setValue(QStringLiteral("window/width"), state.size.width());
    settings.setValue(QStringLiteral("window/height"), state.size.height());
    if (state.position) {
        settings.setValue(QStringLiteral("window/x"), state.position->x());
        settings.setValue(QStringLiteral("window/y"), state.position->y());
    } else {
        settings.remove(QStringLiteral("window/x"));
        settings.remove(QStringLiteral("window/y"));
    }
    settings.setValue(QStringLiteral("window/maximised"), state.maximised);
}

WindowGeometry::WindowGeometry(QObject* parent) : QObject(parent) {
    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMs);
    connect(&settle_, &QTimer::timeout, this, [this] {
        capture();
        persist();
    });
}

QVariantMap WindowGeometry::initialProperties() const {
    QList<QRect> screens;
    const auto all = QGuiApplication::screens();  // primary first
    for (const QScreen* screen : all) {
        screens.append(screen->availableGeometry());
    }
    return initialProperties(screens);
}

QVariantMap WindowGeometry::initialProperties(const QList<QRect>& screens) const {
    QSettings settings;
    const WindowPlacement placement = chooseWindowPlacement(load(settings), screens);

    QVariantMap properties;
    properties.insert(QStringLiteral("width"), placement.size.width());
    properties.insert(QStringLiteral("height"), placement.size.height());
    properties.insert(QStringLiteral("minimumWidth"), kMinimumWindowSize.width());
    properties.insert(QStringLiteral("minimumHeight"), kMinimumWindowSize.height());
    if (placement.position) {
        properties.insert(QStringLiteral("x"), placement.position->x());
        properties.insert(QStringLiteral("y"), placement.position->y());
    }
    if (placement.maximised) {
        properties.insert(QStringLiteral("visibility"), QVariant::fromValue(QWindow::Maximized));
    }
    return properties;
}

void WindowGeometry::track(QWindow* window) {
    if (window == nullptr || window_ != nullptr) {
        return;
    }
    window_ = window;

#ifdef Q_OS_MACOS
    // The green button offers full screen (platforms.md: "Full screen is
    // offered"). Without the hint a Qt window can lose that offer.
    window_->setFlags(window_->flags() | Qt::WindowFullscreenButtonHint);
#endif

    QSettings settings;
    if (const auto saved = load(settings)) {
        state_ = *saved;
        haveState_ = true;
    }
    capture();

    connect(window_, &QWindow::xChanged, this, &WindowGeometry::changed);
    connect(window_, &QWindow::yChanged, this, &WindowGeometry::changed);
    connect(window_, &QWindow::widthChanged, this, &WindowGeometry::changed);
    connect(window_, &QWindow::heightChanged, this, &WindowGeometry::changed);
    connect(window_, &QWindow::visibilityChanged, this, &WindowGeometry::changed);
    connect(qGuiApp, &QGuiApplication::aboutToQuit, this, [this] {
        capture();
        persist();
    });
}

void WindowGeometry::changed() {
    settle_.start();
}

void WindowGeometry::capture() {
    if (window_ == nullptr) {
        return;
    }
    switch (window_->visibility()) {
        case QWindow::Windowed: {
            const QRect geometry = window_->geometry();
            state_.size = geometry.size();
            state_.position = geometry.topLeft();
            state_.maximised = false;
            haveState_ = true;
            break;
        }
        case QWindow::Maximized:
            // Keep the last windowed size and position: that is what the
            // window returns to when it is un-maximised.
            state_.maximised = true;
            haveState_ = true;
            break;
        default:
            // Minimised, full screen, hidden: nothing to learn.
            break;
    }
}

void WindowGeometry::persist() {
    if (!haveState_) {
        return;
    }
    QSettings settings;
    save(settings, state_);
}

}  // namespace cube::app
