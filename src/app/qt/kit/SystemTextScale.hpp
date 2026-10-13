#pragma once

#include <QObject>
#include <qqmlintegration.h>

// The operating system's text-size multiplier, for Theme.textScale. Like
// SystemTheme it wraps what the platform reports in a property that QML can
// bind to and that changes while the app runs; where the platform has no
// system-wide text size it stays at 1.
//
// Settings > Appearance > Text size multiplies this by the app's own
// factor; that lives in Theme, not here. The override below is the hook for
// tests and the developer gallery: it stands in for the system's value
// until cleared.
class SystemTextScale : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qreal scale READ scale NOTIFY scaleChanged)
    Q_PROPERTY(bool overridden READ isOverridden NOTIFY scaleChanged)

public:
    explicit SystemTextScale(QObject* parent = nullptr);

    [[nodiscard]] qreal scale() const;
    [[nodiscard]] bool isOverridden() const;

    // Looks at the system again. Called when the watcher says something may
    // have changed; harmless otherwise.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setOverride(qreal scale);
    Q_INVOKABLE void clearOverride();

signals:
    void scaleChanged();

private:
    qreal m_system = 1.0;
    qreal m_override = 0.0;  // 0 = none
};
