#pragma once

namespace cube::app::kit {

// What the operating system says about the person's text size and animation
// preferences. Each platform keeps these in a different place
// (docs/design/platforms.md, "Text size, motion, theme, language"); this is
// the one place that knows where. A platform with no such setting answers
// the neutral value, 1.0 and false.
//
// Cheap enough to call on app activation; not meant for a per-frame loop
// (the Linux implementation asks gsettings).

// The system text-size multiplier, 1.0 when the system has none. Not clamped:
// Theme clamps the product with the app's own setting.
[[nodiscard]] double querySystemTextScale();

// True when the person has asked the system to cut back animation.
[[nodiscard]] bool querySystemReducedMotion();

}  // namespace cube::app::kit
