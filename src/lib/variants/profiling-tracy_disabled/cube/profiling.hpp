#pragma once

// Tracy instrumentation macros, no-op variant - the default. See the
// profiling-tracy_enabled sibling's identically-pathed cube/profiling.hpp for
// the real-Tracy variant and for why CMake, not an #ifdef, selects between
// the two.
//
// Every macro expands to nothing, so instrumented source compiles as the
// source would with no instrumentation in it, and Tracy need not be installed
// at all. The corollary: an argument is NOT evaluated here. Keep the
// expression handed to CUBE_PLOT / CUBE_ZONE_VALUE free of side effects, and
// never let it be the only use of a variable (that is an unused-variable
// warning, an error under -Werror, in exactly the build nobody profiles).

namespace cube::profiling {

inline constexpr bool enabled = false;
inline constexpr bool to_recorder = false;

}  // namespace cube::profiling

// Macros, not functions, on purpose: a function would evaluate its arguments
// and could not vanish, and the real variant needs the name to be a literal
// whose address Tracy keeps.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CUBE_ZONE_SCOPED()
#define CUBE_ZONE_SCOPED_N(name)
#define CUBE_ZONE_BEGIN(var, name)
#define CUBE_ZONE_END(var)
#define CUBE_ZONE_VALUE(var, number)
#define CUBE_ZONE_SCOPED_VALUE(number)
#define CUBE_FRAME_MARK()
#define CUBE_FRAME_MARK_NAMED(name)
#define CUBE_PLOT(name, value)
#define CUBE_MESSAGE(text)
#define CUBE_THREAD_NAME(name)
#define CUBE_APP_INFO(text)
// NOLINTEND(cppcoreguidelines-macro-usage)
