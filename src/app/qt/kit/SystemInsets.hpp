#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <qqmlintegration.h>

// How far the system's own furniture (status bar, notch, home indicator,
// navigation bar) intrudes on the window, in device-independent pixels, for
// Theme.insets. The edges are physical (left and right); Theme turns them
// into leading and trailing.
//
// Qt 6.8 has no public safe-area API: QWindow::safeAreaMargins arrives in 6.9
// (docs/design/platforms.md, "Safe areas"), and the 6.8 platform-window
// accessor is QPA-private. So on Android and iOS this derives the insets
// from the difference between the screen's full geometry and its available
// geometry, which is what the system reports excluding its own bars. That
// gets bars and, where the platform folds it in, the home indicator; it
// does not see a cut-out the platform does not subtract. Desktop is zero.
// Unverified on a device: this was written and unit-tested on Windows only.
//
// The override stands in for the system for tests and the developer gallery
// ("show me the notch").
class SystemInsets : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qreal top READ top NOTIFY insetsChanged)
    Q_PROPERTY(qreal left READ left NOTIFY insetsChanged)
    Q_PROPERTY(qreal right READ right NOTIFY insetsChanged)
    Q_PROPERTY(qreal bottom READ bottom NOTIFY insetsChanged)
    Q_PROPERTY(bool overridden READ isOverridden NOTIFY insetsChanged)

public:
    explicit SystemInsets(QObject* parent = nullptr);

    [[nodiscard]] qreal top() const;
    [[nodiscard]] qreal left() const;
    [[nodiscard]] qreal right() const;
    [[nodiscard]] qreal bottom() const;
    [[nodiscard]] bool isOverridden() const;

    // Follows this window's screen. Called once by KitWindowBindings.
    Q_INVOKABLE void watch(QWindow* window);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setOverride(qreal top, qreal left, qreal right, qreal bottom);
    Q_INVOKABLE void clearOverride();

signals:
    void insetsChanged();

private:
    struct Edges {
        qreal top = 0;
        qreal left = 0;
        qreal right = 0;
        qreal bottom = 0;
        bool operator==(const Edges& other) const = default;
    };

    [[nodiscard]] Edges readSystem() const;
    [[nodiscard]] const Edges& current() const { return m_hasOverride ? m_override : m_system; }
    void assign(const Edges& system);

    QPointer<QWindow> m_window;
    QMetaObject::Connection m_screenConnection;
    Edges m_system;
    Edges m_override;
    bool m_hasOverride = false;
};
