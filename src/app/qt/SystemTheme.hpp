#pragma once

#include <qqmlintegration.h>
#include <QObject>

// Whether the OS is currently in dark mode, for Settings' "System (default)"
// appearance option. Deliberately not just a QML binding on
// Qt.styleHints.colorScheme: Qt's own docs note that property's change
// notifier isn't implemented for QML, so a binding on it won't update live
// when the user flips their OS theme while the app is running. This wraps
// the real QStyleHints::colorSchemeChanged C++ signal instead, so it does.
class SystemTheme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool dark READ isDark NOTIFY darkChanged)

public:
    explicit SystemTheme(QObject* parent = nullptr);

    [[nodiscard]] bool isDark() const;

signals:
    void darkChanged();
};
