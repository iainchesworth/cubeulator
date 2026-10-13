#include <cube/vision/held_cube.hpp>

#include <cube/profiling.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace cube::vision {

namespace {

constexpr std::array<bool, 9> kAllNine = {true, true, true, true, true, true, true, true, true};

[[nodiscard]] std::array<HoldFit, 24> fit(const std::array<cv::Vec3f, 9>& lab,
                                          const std::array<bool, 9>& counted,
                                          const solver::Cube& cube, const SessionColours& colours) {
    CUBE_ZONE_SCOPED_N("vision.held_cube.fit");
    std::array<HoldFit, 24> fits{};
    const std::array<solver::Hold, 24> holds = solver::all_holds();
    for (std::size_t i = 0; i < holds.size(); ++i) {
        const std::array<solver::Color, 9> shown = solver::camera_view(cube, holds[i]);
        float total = 0.0F;
        int cells = 0;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            if (!counted[cell]) {
                continue;
            }
            total += colour_distance(lab[cell], colours[static_cast<std::size_t>(shown[cell])]);
            ++cells;
        }
        fits[i] = HoldFit{holds[i], cells > 0 ? total / static_cast<float>(cells)
                                              : std::numeric_limits<float>::max()};
    }
    // Stable, so holds that fit equally keep the order all_holds() gives
    // them and the answer does not depend on the sort.
    std::stable_sort(fits.begin(), fits.end(),
                     [](const HoldFit& a, const HoldFit& b) { return a.distance < b.distance; });
    return fits;
}

// What the fits say: the best, and every hold that would look the same to
// the camera in the cells that were counted - provided nothing that would
// look different there comes close.
[[nodiscard]] HoldReading reading_of(const std::array<HoldFit, 24>& fits,
                                     const std::array<bool, 9>& counted, const solver::Cube& cube) {
    HoldReading reading;
    reading.distance = fits.front().distance;
    if (reading.distance > kHoldFitDistance) {
        return reading;
    }
    const auto same_where_counted = [&counted](const std::array<solver::Color, 9>& a,
                                               const std::array<solver::Color, 9>& b) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            if (counted[cell] && a[cell] != b[cell]) {
                return false;
            }
        }
        return true;
    };
    const std::array<solver::Color, 9> shown = solver::camera_view(cube, fits.front().hold);
    for (const HoldFit& candidate : fits) {
        if (same_where_counted(solver::camera_view(cube, candidate.hold), shown)) {
            reading.holds.push_back(candidate.hold);
        } else if (candidate.distance < reading.distance + kHoldClearDistance) {
            reading.holds.clear();
            return reading;
        }
    }
    return reading;
}

}  // namespace

std::array<HoldFit, 24> fit_holds(const std::array<cv::Vec3f, 9>& lab, const solver::Cube& cube,
                                  const SessionColours& colours) {
    return fit(lab, kAllNine, cube, colours);
}

std::array<HoldFit, 24> fit_holds(const SideRead& read, const solver::Cube& cube,
                                  const SessionColours& colours) {
    return fit(read.lab, read.found, cube, colours);
}

bool HoldReading::shows(const solver::Hold& hold) const noexcept {
    return std::find(holds.begin(), holds.end(), hold) != holds.end();
}

HoldReading read_hold(const std::array<cv::Vec3f, 9>& lab, const solver::Cube& cube,
                      const SessionColours& colours) {
    return reading_of(fit(lab, kAllNine, cube, colours), kAllNine, cube);
}

HoldReading read_hold(const SideRead& read, const solver::Cube& cube,
                      const SessionColours& colours) {
    return reading_of(fit(read.lab, read.found, cube, colours), read.found, cube);
}

int stickers_off(const SideRead& read, const std::array<solver::Color, 9>& expected,
                 const SessionColours& colours) {
    int off = 0;
    for (std::size_t cell = 0; cell < 9; ++cell) {
        if (!read.found[cell]) {
            continue;
        }
        const auto own = static_cast<std::size_t>(expected[cell]);
        const float to_own = colour_distance(read.lab[cell], colours[own]);
        for (std::size_t other = 0; other < colours.size(); ++other) {
            if (other != own && colour_distance(read.lab[cell], colours[other]) < to_own) {
                ++off;
                break;
            }
        }
    }
    return off;
}

}  // namespace cube::vision
