// macOS and iOS half of system_query.cpp. Compiled only on Apple platforms
// (src/app/CMakeLists.txt, kit block). Not built or run on the machine this
// was written on: the calls are the documented ones and are small, but a
// Mac build of the kit is the first thing to check.
#include "system_query.hpp"

#include <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

namespace cube::app::kit {

double querySystemTextScale() {
#if TARGET_OS_IOS
    // Dynamic Type: how much larger the system makes a 16 pt body font.
    UIFontMetrics* metrics = [UIFontMetrics metricsForTextStyle:UIFontTextStyleBody];
    const CGFloat scaled = [metrics scaledValueForValue:16.0];
    return scaled > 0.0 ? static_cast<double>(scaled / 16.0) : 1.0;
#else
    // macOS has no system-wide text size; the in-app setting is the only one.
    return 1.0;
#endif
}

bool querySystemReducedMotion() {
#if TARGET_OS_IOS
    return UIAccessibilityIsReduceMotionEnabled() ? true : false;
#else
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion] ? true : false;
#endif
}

}  // namespace cube::app::kit
