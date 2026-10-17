#include <algorithm>
#include <cube/vision/held_cube.hpp>

#include <cube/profiling.hpp>

#include <cstddef>
#include <limits>

namespace cube::vision {

namespace {

constexpr std::array<bool, 9> kAllNine = {true, true, true, true, true, true, true, true, true};

// The holds are of a 3x3: a read of any other size is not one of them.
[[nodiscard]] std::array<cv::Vec3f, 9> nine_of(std::span<const cv::Vec3f> lab) {
    std::array<cv::Vec3f, 9> out{};
    for (std::size_t i = 0; i < out.size() && i < lab.size(); ++i) {
        out.at(i) = lab[i];
    }
    return out;
}

[[nodiscard]] std::array<bool, 9> nine_found(const SideRead& read) {
    std::array<bool, 9> out{};
    for (std::size_t i = 0; i < out.size() && i < read.found.size(); ++i) {
        out.at(i) = read.found.at(i);
    }
    return out;
}

[[nodiscard]] std::array<HoldFit, 24> fit(const std::array<cv::Vec3f, 9>& lab,
                                          const std::array<bool, 9>& counted,
                                          const solver::Cube& cube, const SessionColours& colours) {
    CUBE_ZONE_SCOPED_N("vision.held_cube.fit");
    std::array<HoldFit, 24> fits{};
    const std::array<solver::Hold, 24> holds = solver::all_holds();
    for (std::size_t i = 0; i < holds.size(); ++i) {
        const std::array<solver::Color, 9> shown = solver::camera_view(cube, holds.at(i));
        float total = 0.0F;
        int cells = 0;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            if (!counted.at(cell)) {
                continue;
            }
            total +=
                colour_distance(lab.at(cell), colours.at(static_cast<std::size_t>(shown.at(cell))));
            ++cells;
        }
        fits.at(i) = HoldFit{
            .hold = holds.at(i),
            .distance =
                cells > 0 ? total / static_cast<float>(cells) : std::numeric_limits<float>::max(),
        };
    }
    // Stable, so holds that fit equally keep the order all_holds() gives
    // them and the answer does not depend on the sort.
    std::ranges::stable_sort(
        fits, [](const HoldFit& a, const HoldFit& b) { return a.distance < b.distance; });
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
            if (counted.at(cell) && a.at(cell) != b.at(cell)) {
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

std::array<HoldFit, 24> fit_holds(std::span<const cv::Vec3f> lab, const solver::Cube& cube,
                                  const SessionColours& colours) {
    return fit(nine_of(lab), kAllNine, cube, colours);
}

std::array<HoldFit, 24> fit_holds(const SideRead& read, const solver::Cube& cube,
                                  const SessionColours& colours) {
    return fit(nine_of(read.lab), nine_found(read), cube, colours);
}

bool HoldReading::shows(const solver::Hold& hold) const noexcept {
    return std::ranges::find(holds, hold) != holds.end();
}

HoldReading read_hold(std::span<const cv::Vec3f> lab, const solver::Cube& cube,
                      const SessionColours& colours) {
    return reading_of(fit(nine_of(lab), kAllNine, cube, colours), kAllNine, cube);
}

HoldReading read_hold(const SideRead& read, const solver::Cube& cube,
                      const SessionColours& colours) {
    const std::array<bool, 9> found = nine_found(read);
    return reading_of(fit(nine_of(read.lab), found, cube, colours), found, cube);
}

int stickers_off(const SideRead& read, const std::array<solver::Color, 9>& expected,
                 const SessionColours& colours) {
    int off = 0;
    for (std::size_t cell = 0; cell < 9; ++cell) {
        if (!read.found.at(cell)) {
            continue;
        }
        const auto own = static_cast<std::size_t>(expected.at(cell));
        const float to_own = colour_distance(read.lab.at(cell), colours.at(own));
        for (std::size_t other = 0; other < colours.size(); ++other) {
            if (other != own && colour_distance(read.lab.at(cell), colours.at(other)) < to_own) {
                ++off;
                break;
            }
        }
    }
    return off;
}

}  // namespace cube::vision
