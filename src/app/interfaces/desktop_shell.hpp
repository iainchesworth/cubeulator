#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace cube::platform::desktop_shell {

// What the app's shell needs from a desktop operating system and Qt has no
// cross-platform API for: keeping the display awake, the process's identity
// with the OS shell, and a few facts about how the platform's windows behave
// (docs/design/platforms.md).
//
// There is exactly one definition of each function below per platform,
// living in platform/<os>/desktop_shell_<os>.cpp, which that platform's
// fragment adds to the desktop shell library in
// cube_platform_configure_desktop_shell(). Phones share
// platform/posix/desktop_shell_mobile.cpp, which does nothing. CMake compiles
// only the file the platform names, so the shell's own code contains no
// preprocessor branching and no test of the OS.

// What one platform does to keep the display from dimming or locking.
//
// There is no cross-platform Qt API for this, so each desktop OS has its own
// implementation - a display-required execution state on Windows, a
// no-display-sleep power assertion on macOS, the session's inhibit interface
// on Linux. None of them asks the person for anything.
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
[[nodiscard]] std::unique_ptr<KeepAwakeBackend> make_keep_awake_backend();

// Gives the process the identity the OS shell groups its windows and
// shortcuts by. Called once, before the first window exists.
// `app_user_model_id` is the Windows AppUserModelID (cmake/AppIdentity.cmake);
// platforms without such a thing ignore it. Returns an empty string, or what
// went wrong; the app carries on either way, as the window is simply grouped
// by its executable, as it would be without this.
[[nodiscard]] std::string apply_process_identity(std::string_view app_user_model_id);

// How the platform's windows behave, as values the shell reads instead of
// asking which OS it is on.
struct Traits {
    // The window's size and position are the person's to keep: they are
    // restored, and kept on a screen that is still there. False on a phone,
    // where the OS owns the window.
    bool remembers_window_geometry = false;
    // The app puts its own icon on the window. False where the icon the OS
    // draws comes from the application bundle (macOS) and a window icon would
    // replace it with a bare one, and on phones.
    bool sets_window_icon = false;
    // The title bar offers full screen as a button of its own, and a Qt window
    // can lose that offer unless it is asked to keep it (macOS's green button).
    bool fullscreen_button_hint = false;
    // The app's menu is a menu bar at the top of the screen, holding the
    // standard window commands, and full screen has the chord that goes with
    // it (macOS).
    bool native_menu_bar = false;
};

[[nodiscard]] Traits traits() noexcept;

}  // namespace cube::platform::desktop_shell
