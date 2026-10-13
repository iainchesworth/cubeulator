#pragma once

#include <cstdint>

namespace cube::platform::camera::detail {

// Held for as long as a thread is calling into a camera backend, so that
// whatever the platform's capture stack needs of its calling thread is in
// place. Where the stack asks nothing of the thread, this does nothing.
//
// There is exactly one definition of this class's members per platform:
// platform/windows/capture_thread_scope_windows.cpp (COM, which the
// MSMF/DSHOW backends require) and platform/posix/ for the rest. CMake
// compiles only the file the platform's fragment names, so neither the code
// that holds a scope nor this header branches on the platform.
class CaptureThreadScope {
public:
    CaptureThreadScope() noexcept;
    // Defined per platform: Windows undoes its CoInitializeEx here. Where a
    // platform defines it as `= default` the destructor is trivial, which is
    // the point of the lint below, and not something this declaration can
    // say for every platform at once.
    ~CaptureThreadScope();  // NOLINT(performance-trivially-destructible)

    CaptureThreadScope(const CaptureThreadScope&) = delete;
    CaptureThreadScope& operator=(const CaptureThreadScope&) = delete;
    CaptureThreadScope(CaptureThreadScope&&) = delete;
    CaptureThreadScope& operator=(CaptureThreadScope&&) = delete;

private:
    // What the constructor must hand its destructor (the Windows one keeps
    // its CoInitializeEx result here). Opaque on purpose: this header names
    // no platform type. Unused where a platform has nothing to undo, hence
    // the attribute (clang's -Wunused-private-field is an error under -Werror).
    [[maybe_unused]] std::int32_t state_ = 0;
};

}  // namespace cube::platform::camera::detail
