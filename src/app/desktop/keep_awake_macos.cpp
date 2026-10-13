// macOS: a no-display-sleep power assertion.
//
// IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep) keeps the
// display from sleeping, and so from locking on idle, until the assertion is
// released. It needs no entitlement and shows no prompt. The reason string
// appears in `pmset -g assertions` and Activity Monitor, so it is the app's
// name rather than a sentence.
#include "keep_awake.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

namespace cube::app {

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

std::unique_ptr<KeepAwakeBackend> makePlatformKeepAwakeBackend() {
    return std::make_unique<MacKeepAwake>();
}

}  // namespace cube::app
