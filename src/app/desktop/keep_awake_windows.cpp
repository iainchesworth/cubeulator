// Windows: a display-required execution state.
//
// SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED) keeps the
// display on until the same thread clears it, or the thread ends. The state
// belongs to the calling thread, so acquire and release must come from one
// thread; KeepAwake is a GUI-thread object, which gives that for free.
#include "keep_awake.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace cube::app {

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

std::unique_ptr<KeepAwakeBackend> makePlatformKeepAwakeBackend() {
    return std::make_unique<WindowsKeepAwake>();
}

}  // namespace cube::app
