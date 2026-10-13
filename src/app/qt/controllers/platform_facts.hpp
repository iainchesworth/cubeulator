#pragma once

#include <QObject>
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

public:
    explicit PlatformFacts(QObject* parent = nullptr);

    [[nodiscard]] bool isMobile() const noexcept;
    [[nodiscard]] QVariantList graphicsBackends() const;

private:
    bool is_mobile_ = false;
    QVariantList graphics_backends_;
};

}  // namespace cube::app
