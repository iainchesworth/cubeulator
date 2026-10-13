// Windows: a display-required execution state, and the AppUserModelID.
//
// SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED) keeps the
// display on until the same thread clears it, or the thread ends. The state
// belongs to the calling thread, so acquire and release must come from one
// thread; KeepAwake is a GUI-thread object, which gives that for free.
//
// The AppUserModelID is what the taskbar groups a window with its pinned
// shortcut by; the installer stamps the same value on the Start menu shortcut
// (packaging/windows/StampShortcut.ps1).
#include "interfaces/desktop_shell.hpp"

#include <windows.h>
// shobjidl.h requires windows.h first.
#include <shobjidl.h>

#include <memory>
#include <string>
#include <string_view>

namespace cube::platform::desktop_shell {

namespace {

class WindowsKeepAwake final : public KeepAwakeBackend {
public:
    void acquire() override {
        if (held_) {
            return;
        }
        // The call returns the previous state, or 0 on failure.
        held_ = ::SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED) != 0;
        report(held_);
    }

    void release() override {
        if (!held_) {
            return;
        }
        ::SetThreadExecutionState(ES_CONTINUOUS);
        held_ = false;
    }

private:
    bool held_ = false;
};

}  // namespace

std::unique_ptr<KeepAwakeBackend> make_keep_awake_backend() {
    return std::make_unique<WindowsKeepAwake>();
}

std::string apply_process_identity(std::string_view app_user_model_id) {
    // The identifier is ASCII (cmake/AppIdentity.cmake), so widening is exact.
    const std::wstring id(app_user_model_id.begin(), app_user_model_id.end());
    const HRESULT result = ::SetCurrentProcessExplicitAppUserModelID(id.c_str());
    if (FAILED(result)) {
        return "SetCurrentProcessExplicitAppUserModelID failed, HRESULT " +
               std::to_string(static_cast<long>(result));
    }
    return {};
}

Traits traits() noexcept {
    return Traits{
        .remembers_window_geometry = true,
        .sets_window_icon = true,
        .fullscreen_button_hint = false,
        .native_menu_bar = false,
    };
}

}  // namespace cube::platform::desktop_shell
