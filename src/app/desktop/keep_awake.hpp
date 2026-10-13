#pragma once

#include <QObject>

#include <functional>
#include <memory>
#include <utility>

namespace cube::app {

// What one platform does to keep the display from dimming or locking.
//
// docs/design/platforms.md, "Keep awake": there is no cross-platform Qt API
// for this, so each desktop OS has its own implementation behind this
// interface - a display-required execution state on Windows, a no-display-
// sleep power assertion on macOS, the session's inhibit interface on Linux.
// None of them asks the person for anything.
class KeepAwakeBackend {
public:
    // Called whenever the platform's answer to acquire() is known, with
    // whether the display is now being held. Synchronous backends call it
    // from inside acquire(); the Linux one calls it when D-Bus replies.
    using Reporter = std::function<void(bool held)>;

    virtual ~KeepAwakeBackend() = default;

    // Both are idempotent.
    virtual void acquire() = 0;
    virtual void release() = 0;

    void setReporter(Reporter reporter) { reporter_ = std::move(reporter); }

protected:
    void report(bool held) const {
        if (reporter_) {
            reporter_(held);
        }
    }

private:
    Reporter reporter_;
};

// The backend for the platform this binary was built for.
[[nodiscard]] std::unique_ptr<KeepAwakeBackend> makePlatformKeepAwakeBackend();

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
