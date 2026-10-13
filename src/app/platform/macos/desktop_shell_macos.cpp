// macOS: a no-display-sleep power assertion.
//
// IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep) keeps the
// display from sleeping, and so from locking on idle, until the assertion is
// released. It needs no entitlement and shows no prompt. The reason string
// appears in `pmset -g assertions` and Activity Monitor, so it is the app's
// name rather than a sentence.
#include "interfaces/desktop_shell.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

namespace cube::platform::desktop_shell {

namespace {

class MacKeepAwake final : public KeepAwakeBackend {
public:
    void acquire() override {
        if (held_) {
            return;
        }
        const IOReturn status =
            ::IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                                          CFSTR("Cubeulator"), &assertion_);
        held_ = status == kIOReturnSuccess;
        report(held_);
    }

    void release() override {
        if (!held_) {
            return;
        }
        ::IOPMAssertionRelease(assertion_);
        assertion_ = kIOPMNullAssertionID;
        held_ = false;
    }

private:
    IOPMAssertionID assertion_ = kIOPMNullAssertionID;
    bool held_ = false;
};

}  // namespace

std::unique_ptr<KeepAwakeBackend> make_keep_awake_backend() {
    return std::make_unique<MacKeepAwake>();
}

// The Dock and the menu bar identify the app by its bundle (Info.plist); there
// is no separate process identity to set.
std::string apply_process_identity(std::string_view /*app_user_model_id*/) {
    return {};
}

Traits traits() noexcept {
    return Traits{
        .remembers_window_geometry = true,
        // The Dock icon is the bundle's, drawn on the system's rounded square;
        // a window icon would replace it with a bare one.
        .sets_window_icon = false,
        // The green button offers full screen, which a Qt window can lose
        // without the hint.
        .fullscreen_button_hint = true,
        .native_menu_bar = true,
    };
}

}  // namespace cube::platform::desktop_shell
