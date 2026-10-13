#pragma once

#include <QObject>
#include <qqmlintegration.h>

// Whether the operating system has been asked to cut back animation, for
// Theme.reducedMotion. Same shape as SystemTextScale: a property QML binds
// to, refreshed when the system may have changed, with an override that
// stands in for the system's value for tests and the developer gallery.
//
// Settings > Appearance > Motion (Same as this device / Reduced / Full) is
// applied in Theme, not here.
class SystemMotion : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool reduced READ reduced NOTIFY reducedChanged)
    Q_PROPERTY(bool overridden READ isOverridden NOTIFY reducedChanged)

public:
    explicit SystemMotion(QObject* parent = nullptr);

    [[nodiscard]] bool reduced() const;
    [[nodiscard]] bool isOverridden() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setOverride(bool reduced);
    Q_INVOKABLE void clearOverride();

signals:
    void reducedChanged();

private:
    bool m_system = false;
    bool m_hasOverride = false;
    bool m_override = false;
};
