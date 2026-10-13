#include "SystemInsets.hpp"

#include "SystemChangeWatcher.hpp"

#include <QRect>
#include <QScreen>

SystemInsets::SystemInsets(QObject* parent) : QObject(parent) {
    if (SystemChangeWatcher* watcher = SystemChangeWatcher::instance(); watcher != nullptr) {
        connect(watcher, &SystemChangeWatcher::changed, this, &SystemInsets::refresh);
    }
}

qreal SystemInsets::top() const {
    return current().top;
}
qreal SystemInsets::left() const {
    return current().left;
}
qreal SystemInsets::right() const {
    return current().right;
}
qreal SystemInsets::bottom() const {
    return current().bottom;
}
bool SystemInsets::isOverridden() const {
    return m_hasOverride;
}

SystemInsets::Edges SystemInsets::readSystem() const {
    Edges edges;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    const QScreen* screen = m_window != nullptr ? m_window->screen() : nullptr;
    if (screen != nullptr) {
        const QRect full = screen->geometry();
        const QRect usable = screen->availableGeometry();
        edges.top = qMax(0, usable.top() - full.top());
        edges.left = qMax(0, usable.left() - full.left());
        edges.right = qMax(0, full.right() - usable.right());
        edges.bottom = qMax(0, full.bottom() - usable.bottom());
    }
#endif
    return edges;
}

void SystemInsets::assign(const Edges& system) {
    const Edges before = current();
    m_system = system;
    if (!(before == current())) {
        emit insetsChanged();
    }
}

void SystemInsets::watch(QWindow* window) {
    if (m_window == window) {
        return;
    }
    QObject::disconnect(m_screenConnection);
    m_window = window;
    if (window != nullptr) {
        // A rotation or a window moved to another display changes the
        // screen's geometry; there is no per-window signal in 6.8.
        m_screenConnection = connect(window, &QWindow::screenChanged, this, &SystemInsets::refresh);
    }
    refresh();
}

void SystemInsets::refresh() {
    assign(readSystem());
}

void SystemInsets::setOverride(qreal top, qreal left, qreal right, qreal bottom) {
    const Edges before = current();
    const bool hadOverride = m_hasOverride;
    m_override = Edges{qMax<qreal>(0, top), qMax<qreal>(0, left), qMax<qreal>(0, right), qMax<qreal>(0, bottom)};
    m_hasOverride = true;
    if (!(before == current()) || !hadOverride) {
        emit insetsChanged();
    }
}

void SystemInsets::clearOverride() {
    if (!m_hasOverride) {
        return;
    }
    m_hasOverride = false;
    emit insetsChanged();
}
