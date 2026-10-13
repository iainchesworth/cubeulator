#include <cube/vision/held_cube.hpp>

#include <algorithm>
#include <cstddef>

namespace cube::vision {

std::array<HoldFit, 24> fit_holds(const std::array<cv::Vec3f, 9>& lab, const solver::Cube& cube,
                                  const SessionColours& colours) {
    std::array<HoldFit, 24> fits{};
    const std::array<solver::Hold, 24> holds = solver::all_holds();
    for (std::size_t i = 0; i < holds.size(); ++i) {
        const std::array<solver::Color, 9> shown = solver::camera_view(cube, holds[i]);
        float total = 0.0F;
        for (std::size_t cell = 0; cell < 9; ++cell) {
            total += colour_distance(lab[cell], colours[static_cast<std::size_t>(shown[cell])]);
        }
        fits[i] = HoldFit{holds[i], total / 9.0F};
    }
    // Stable, so holds that fit equally keep the order all_holds() gives
    // them and the answer does not depend on the sort.
    std::stable_sort(fits.begin(), fits.end(),
                     [](const HoldFit& a, const HoldFit& b) { return a.distance < b.distance; });
    return fits;
}

bool HoldReading::shows(const solver::Hold& hold) const noexcept {
    return std::find(holds.begin(), holds.end(), hold) != holds.end();
}

HoldReading read_hold(const std::array<cv::Vec3f, 9>& lab, const solver::Cube& cube,
                      const SessionColours& colours) {
    const std::array<HoldFit, 24> fits = fit_holds(lab, cube, colours);
    HoldReading reading;
    reading.distance = fits.front().distance;
    if (reading.distance > kHoldFitDistance) {
        return reading;
    }

    // The best fit, and every hold that shows the camera exactly what it
    // does. Anything that would look different has to be clearly worse.
    const std::array<solver::Color, 9> shown = solver::camera_view(cube, fits.front().hold);
    for (const HoldFit& fit : fits) {
        if (solver::camera_view(cube, fit.hold) == shown) {
            reading.holds.push_back(fit.hold);
        } else if (fit.distance < reading.distance + kHoldClearDistance) {
            reading.holds.clear();
            return reading;
        }
    }
    return reading;
}

}  // namespace cube::vision
