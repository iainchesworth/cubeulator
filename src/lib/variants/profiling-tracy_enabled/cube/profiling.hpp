#pragma once

// Tracy instrumentation macros, real-Tracy variant. CMake - not an #ifdef -
// selects this file over the profiling-tracy_disabled sibling: when
// CUBEULATOR_ENABLE_TRACY is on, src/lib/CMakeLists.txt puts this directory
// on cube::lib's public include path (see cmake/Tracy.cmake). Exactly one of
// the two is ever compiled, both at this same "cube/profiling.hpp" relative
// path, so every `#include <cube/profiling.hpp>` and every CUBE_ZONE_* use
// compiles unchanged either way.
//
// Zone names are string LITERALS: Tracy keeps the pointer, not a copy. For
// the same reason the name given to CUBE_PLOT, CUBE_FRAME_MARK_NAMED and
// CUBE_MESSAGE must outlive the process - never pass a temporary.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <tracy/TracyC.h>
#include <tracy/Tracy.hpp>

namespace cube::profiling {

// True in this variant, false in the sibling. A test pins it against the
// CMake option so the wrong directory cannot be selected unnoticed.
inline constexpr bool enabled = true;
// False here: this variant hands events to Tracy, not to the in-process
// recorder (the profiling-recorder sibling).
inline constexpr bool to_recorder = false;

inline void app_info(std::string_view text) {
    TracyAppInfo(text.data(), text.size());
}

// Tracy's plot call is overloaded on int64_t, float and double, so an int
// argument is ambiguous between all three. Route every value through the
// obvious one of the two Tracy keeps exact: integers stay integers, anything
// floating becomes a double.
template <typename T>
void plot(const char* name, T value) {
    static_assert(std::is_arithmetic_v<T>, "CUBE_PLOT takes a number");
    if constexpr (std::is_floating_point_v<T>) {
        TracyPlot(name, static_cast<double>(value));
    } else {
        TracyPlot(name, static_cast<std::int64_t>(value));
    }
}

// The manual (begin ... end) zone, made exception safe.
//
// Tracy's own C-API pair (TracyCZoneN / TracyCZoneEnd) leaves the zone open
// when an exception unwinds past the end call, and an unclosed zone nests
// every later zone on that thread beneath it for the rest of the trace. This
// app catches exceptions around its analysis pass on purpose (a degenerate
// frame must drop one frame, not the scan), so the pair has to survive that:
// the destructor closes the zone if nothing else did. end() is idempotent.
class ManualZone {
public:
    explicit ManualZone(const ___tracy_source_location_data* location) noexcept
        : context_(___tracy_emit_zone_begin(location, 1)) {}
    ~ManualZone() { end(); }

    ManualZone(const ManualZone&) = delete;
    ManualZone& operator=(const ManualZone&) = delete;
    ManualZone(ManualZone&&) = delete;
    ManualZone& operator=(ManualZone&&) = delete;

    void value(std::uint64_t number) const noexcept {
        if (open_) {
            ___tracy_emit_zone_value(context_, number);
        }
    }

    void end() noexcept {
        if (open_) {
            open_ = false;
            ___tracy_emit_zone_end(context_);
        }
    }

private:
    TracyCZoneCtx context_;
    bool open_ = true;
};

}  // namespace cube::profiling

// Macros, not functions, on purpose: Tracy's zone is an object that must live
// in the caller's scope, its name must be a literal whose address it keeps,
// and the disabled sibling must be able to make every one of these vanish
// without evaluating its arguments.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)

// A zone covering the rest of the enclosing C++ scope, named after the
// function.
#define CUBE_ZONE_SCOPED() ZoneScoped
// The same, with an explicit name.
#define CUBE_ZONE_SCOPED_N(name) ZoneScopedN(name)
// A manual (not lexically scoped) begin/end pair, for a span that does not
// correspond to one C++ scope - e.g. one stage inside an already-large
// function that should not be restructured into blocks just to give each
// stage its own. `var` names the zone the two calls share. Prefer the scoped
// form wherever a block is natural; this one exists for the stages-in-one-
// function shape.
#define CUBE_ZONE_BEGIN(var, name)                                          \
    static constexpr ___tracy_source_location_data var##_location{          \
        name, __func__, __FILE__, static_cast<std::uint32_t>(__LINE__), 0}; \
    ::cube::profiling::ManualZone var(&var##_location)
#define CUBE_ZONE_END(var) var.end()
// Attach an unsigned number to a zone (shown beside it in the GUI and exported
// by csvexport) - a count, a size, a pass number. The first form is for a
// manual zone and names its variable; the second is for the zone the
// enclosing CUBE_ZONE_SCOPED*() opened.
// (The parameter is `number`, not `value`: a parameter named `value` would
// also replace the `.value` member this expands to.)
#define CUBE_ZONE_VALUE(var, number) var.value(static_cast<std::uint64_t>(number))
#define CUBE_ZONE_SCOPED_VALUE(number) ZoneValue(static_cast<std::uint64_t>(number))

// One per real-time frame, for the frame view. The unnamed set is the direct
// scan's per-frame pass; "Capture" (camera frames delivered) and "UI" (Qt Quick
// presentations) are named sets alongside it, so three unrelated cadences
// never interleave into one meaningless graph.
#define CUBE_FRAME_MARK() FrameMark
#define CUBE_FRAME_MARK_NAMED(name) FrameMarkNamed(name)

// A value on a timeline plot. `name` is a literal; `value` is an integer,
// floating-point or bool expression.
#define CUBE_PLOT(name, value) ::cube::profiling::plot(name, value)

// A message on the timeline, for a discrete event (scan complete, tracker
// lost). `text` is a literal.
#define CUBE_MESSAGE(text) TracyMessageL(text)

// Name the calling thread in the GUI's thread list. Once per thread: every
// call adds a table entry, so never put this in a per-frame or per-task path.
#define CUBE_THREAD_NAME(name) tracy::SetThreadName(name)

// Free text shown in the trace's information window (version, build).
#define CUBE_APP_INFO(text) ::cube::profiling::app_info(text)

// NOLINTEND(cppcoreguidelines-macro-usage)
