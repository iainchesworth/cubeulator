#pragma once

// WCAG 2.x contrast, for the tests that hold what is drawn to what can be
// read. The formula is the one docs/design/tools/check_tokens.py holds the
// design tokens to, so a pair that passes there and is drawn as it is
// specified passes here.

#include <QColor>

#include <algorithm>
#include <cmath>

namespace cube::test {

// The thresholds the design system uses (check_tokens.py): body text and the
// figures on a screen, and the floor for anything else a person is meant to
// make out.
inline constexpr double kTextContrast = 4.5;
inline constexpr double kLargeOrMutedContrast = 3.0;

[[nodiscard]] inline double linear_channel(double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] inline double relative_luminance(const QColor& colour) {
    return 0.2126 * linear_channel(colour.redF()) + 0.7152 * linear_channel(colour.greenF()) +
           0.0722 * linear_channel(colour.blueF());
}

// `top` painted over `below` (which is taken as opaque).
[[nodiscard]] inline QColor over(const QColor& top, const QColor& below) {
    const double alpha = top.alphaF();
    return QColor::fromRgbF(top.redF() * alpha + below.redF() * (1.0 - alpha),
                            top.greenF() * alpha + below.greenF() * (1.0 - alpha),
                            top.blueF() * alpha + below.blueF() * (1.0 - alpha));
}

[[nodiscard]] inline double contrast_ratio(const QColor& a, const QColor& b) {
    const double la = relative_luminance(a);
    const double lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace cube::test
