#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace cube::app {

// What the platform says about itself, for QML to bind to. QML used to test
// Qt.platform.os for these, which put a platform ladder in shared screens; the
// answers now come from the platform's own implementation (see
// interfaces/platform_info.hpp), so QML asks and never has to know which
// platforms exist. Read-only, and fixed for the life of the process.
class PlatformFacts : public QObject {
    Q_OBJECT
    // A phone: camera permission is asked for in-app and there is one camera
    // to use, so the screens show a permission step instead of a device list.
    Q_PROPERTY(bool isMobile READ isMobile CONSTANT)
    // The graphics backends the Settings screen offers beyond "auto", as a
    // list of { key, label } maps in display order.
    Q_PROPERTY(QVariantList graphicsBackends READ graphicsBackends CONSTANT)
    // The families this platform's interface is set in for Japanese and
    // Simplified Chinese, in preference order (Theme.qml takes the first that
    // is installed).
    Q_PROPERTY(QStringList japaneseFaces READ japaneseFaces CONSTANT)
    Q_PROPERTY(QStringList chineseFaces READ chineseFaces CONSTANT)

public:
    explicit PlatformFacts(QObject* parent = nullptr);

    [[nodiscard]] bool isMobile() const noexcept;
    [[nodiscard]] QVariantList graphicsBackends() const;
    [[nodiscard]] QStringList japaneseFaces() const;
    [[nodiscard]] QStringList chineseFaces() const;

private:
    bool is_mobile_ = false;
    QVariantList graphics_backends_;
    QStringList japanese_faces_;
    QStringList chinese_faces_;
};

}  // namespace cube::app
