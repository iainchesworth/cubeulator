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

// QColor keeps its channels as float; the arithmetic below is in double.
[[nodiscard]] inline double red_of(const QColor& colour) {
    return static_cast<double>(colour.redF());
}
[[nodiscard]] inline double green_of(const QColor& colour) {
    return static_cast<double>(colour.greenF());
}
[[nodiscard]] inline double blue_of(const QColor& colour) {
    return static_cast<double>(colour.blueF());
}
[[nodiscard]] inline double alpha_of(const QColor& colour) {
    return static_cast<double>(colour.alphaF());
}

// Nothing behind it shows through.
[[nodiscard]] inline bool is_opaque(const QColor& colour) {
    return colour.alpha() == 255;
}

[[nodiscard]] inline double linear_channel(double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] inline double relative_luminance(const QColor& colour) {
    return 0.2126 * linear_channel(red_of(colour)) + 0.7152 * linear_channel(green_of(colour)) +
           0.0722 * linear_channel(blue_of(colour));
}

// `top` painted over `below` (which is taken as opaque).
[[nodiscard]] inline QColor over(const QColor& top, const QColor& below) {
    const double alpha = alpha_of(top);
    const auto mixed = [alpha](double above, double under) {
        return static_cast<float>(above * alpha + under * (1.0 - alpha));
    };
    return QColor::fromRgbF(mixed(red_of(top), red_of(below)),
                            mixed(green_of(top), green_of(below)),
                            mixed(blue_of(top), blue_of(below)));
}

[[nodiscard]] inline double contrast_ratio(const QColor& a, const QColor& b) {
    const double la = relative_luminance(a);
    const double lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace cube::test
