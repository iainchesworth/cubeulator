#pragma once

#include "interfaces/desktop_shell.hpp"

#include <QObject>

#include <memory>

namespace cube::app {

// What one platform does to keep the display from dimming or locking
// (docs/design/platforms.md, "Keep awake"): the platform's own implementation,
// declared in interfaces/desktop_shell.hpp.
using KeepAwakeBackend = cube::platform::desktop_shell::KeepAwakeBackend;

// QML-facing switch. Screens that need the display awake - the scan and
// solve screens, in E3 - set `active`; nothing else is needed.
//
//   active  what the screen asked for.
//   held    whether the operating system is, right now, keeping the display
//           awake for us. It is false while the app is not in front even if
//           `active` is true: the request is dropped when the app goes to
//           the background and taken up again when it comes back, so a
//           forgotten `active = false` never keeps a laptop awake from the
//           background.
class KeepAwake : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    Q_PROPERTY(bool held READ held NOTIFY heldChanged FINAL)

public:
    explicit KeepAwake(QObject* parent = nullptr);
    // For tests: supply the backend.
    explicit KeepAwake(std::unique_ptr<KeepAwakeBackend> backend, QObject* parent = nullptr);
    ~KeepAwake() override;

    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool held() const { return held_; }

    void setActive(bool active);

signals:
    void activeChanged();
    void heldChanged();

private:
    void sync();
    void setHeld(bool held);
    [[nodiscard]] static bool appInFront();

    std::unique_ptr<KeepAwakeBackend> backend_;
    bool active_ = false;
    bool requested_ = false;  // what the backend has been asked for
    bool held_ = false;
};

}  // namespace cube::app
