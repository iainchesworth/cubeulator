#pragma once

// Instrumentation macros, in-process recorder variant. CMake - not an #ifdef -
// selects this file over its profiling-tracy_enabled and profiling-
// tracy_disabled siblings: when CUBEULATOR_ENABLE_PROFILE_RECORDER is on,
// src/lib/CMakeLists.txt puts this directory on cube::lib's public include
// path (see cmake/ProfileRecorder.cmake). Exactly one of the three is ever
// compiled, all at this same "cube/profiling.hpp" relative path, so every
// `#include <cube/profiling.hpp>` and every CUBE_ZONE_* use compiles unchanged
// whichever is selected.
//
// This is the variant for a target Tracy's client cannot run on - the browser
// - and for anyone who wants the same zones in a Chrome trace file or a CSV
// without a profiler attached (docs/profiling.md, "In the browser"). Events go
// to cube::profile::Recorder::global() (cube/profile/recorder.hpp), which
// writes them out on request.
//
// The rules the Tracy variant states hold here too: zone names are string
// LITERALS (the pointer is kept), and the names given to CUBE_PLOT,
// CUBE_FRAME_MARK_NAMED and CUBE_MESSAGE must outlive the process.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <cube/profile/recorder.hpp>

namespace cube::profiling {

// True in this variant and in the Tracy one, false in the no-op one.
inline constexpr bool enabled = true;
// True only here: events land in cube::profile::Recorder::global(). A test
// pins both against the CMake options so the wrong directory cannot be
// selected unnoticed.
inline constexpr bool to_recorder = true;

inline void app_info(std::string_view text) {
    ::cube::profile::Recorder::global().set_app_info(text);
}

// Every value becomes a double: the recorder keeps one number type, and the
// integers a plot carries (counts, sizes) are far inside a double's exact
// range.
template <typename T>
void plot(const char* name, T value) {
    static_assert(std::is_arithmetic_v<T>, "CUBE_PLOT takes a number");
    ::cube::profile::Recorder::global().plot(name, static_cast<double>(value));
}

// One zone: a start time taken on construction and one event written on end().
//
// Two kinds, because CUBE_ZONE_SCOPED_VALUE has to find "the zone the
// enclosing CUBE_ZONE_SCOPED*() opened" without a name to find it by (Tracy's
// own macro uses a fixed variable name, which would make a second zone in the
// same scope a redefinition):
//   * scoped zones link themselves into a per-thread stack, so the innermost
//     one is the target of a value;
//   * manual zones (CUBE_ZONE_BEGIN) are not on the stack - they have a name
//     in the caller's scope already - and end() is idempotent, so an exception
//     leaves nothing open, as the Tracy variant's ManualZone also guarantees.
class Zone {
public:
    struct Scoped {};
    struct Manual {};
    static constexpr Scoped scoped{};
    static constexpr Manual manual{};

    Zone(const char* name, Scoped) noexcept
        : name_(name), parent_(current_), start_us_(::cube::profile::Recorder::now_us()) {
        current_ = this;
        stacked_ = true;
    }
    Zone(const char* name, Manual) noexcept
        : name_(name), parent_(nullptr), start_us_(::cube::profile::Recorder::now_us()) {}
    ~Zone() { end(); }

    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    Zone(Zone&&) = delete;
    Zone& operator=(Zone&&) = delete;

    void value(std::uint64_t number) noexcept {
        value_ = number;
        has_value_ = true;
    }

    void end() noexcept {
        if (!open_) {
            return;
        }
        open_ = false;
        if (stacked_) {
            current_ = parent_;
        }
        const double end_us = ::cube::profile::Recorder::now_us();
        auto& recorder = ::cube::profile::Recorder::global();
        if (has_value_) {
            recorder.zone_with_value(name_, start_us_, end_us, value_);
        } else {
            recorder.zone(name_, start_us_, end_us);
        }
    }

    // The innermost scoped zone open on this thread, or null.
    static Zone* innermost() noexcept { return current_; }

private:
    inline static thread_local Zone* current_ = nullptr;

    const char* name_;
    Zone* parent_;
    double start_us_;
    std::uint64_t value_ = 0;
    bool has_value_ = false;
    bool open_ = true;
    bool stacked_ = false;
};

// CUBE_ZONE_SCOPED_VALUE's target. With no scoped zone open the value has
// nowhere to go and is dropped, as Tracy drops a value with no zone.
inline void scoped_zone_value(std::uint64_t number) noexcept {
    if (Zone* zone = Zone::innermost(); zone != nullptr) {
        zone->value(number);
    }
}

}  // namespace cube::profiling

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

// Two-step so that __LINE__ expands before it is pasted: each scoped zone gets
// a variable of its own and two zones can share a scope.
#define CUBE_PROFILING_PASTE_(a, b) a##b
#define CUBE_PROFILING_PASTE(a, b) CUBE_PROFILING_PASTE_(a, b)

// A zone covering the rest of the enclosing C++ scope, named after the
// function.
#define CUBE_ZONE_SCOPED() CUBE_ZONE_SCOPED_N(__func__)
// The same, with an explicit name.
#define CUBE_ZONE_SCOPED_N(name)                                           \
    const ::cube::profiling::Zone CUBE_PROFILING_PASTE(cube_zone_, __LINE__)( \
        name, ::cube::profiling::Zone::scoped)
// A manual begin/end pair; see the Tracy variant for when to use one.
#define CUBE_ZONE_BEGIN(var, name) ::cube::profiling::Zone var(name, ::cube::profiling::Zone::manual)
#define CUBE_ZONE_END(var) var.end()
// Attach an unsigned number to a zone. The first form names a manual zone's
// variable; the second is for the innermost zone CUBE_ZONE_SCOPED*() opened on
// this thread.
#define CUBE_ZONE_VALUE(var, number) var.value(static_cast<std::uint64_t>(number))
#define CUBE_ZONE_SCOPED_VALUE(number) \
    ::cube::profiling::scoped_zone_value(static_cast<std::uint64_t>(number))

// One per real-time frame. The unnamed set is called "frame" in the trace.
#define CUBE_FRAME_MARK() ::cube::profile::Recorder::global().frame("frame")
#define CUBE_FRAME_MARK_NAMED(name) ::cube::profile::Recorder::global().frame(name)

// A value on a timeline plot (Chrome's counter track).
#define CUBE_PLOT(name, value) ::cube::profiling::plot(name, value)

// A message on the timeline (an instant event).
#define CUBE_MESSAGE(text) ::cube::profile::Recorder::global().message(text)

// Name the calling thread in the trace. Once per thread.
#define CUBE_THREAD_NAME(name) ::cube::profile::Recorder::global().set_thread_name(name)

// Free text in the trace's metadata (version, build).
#define CUBE_APP_INFO(text) ::cube::profiling::app_info(text)

// NOLINTEND(cppcoreguidelines-macro-usage)
