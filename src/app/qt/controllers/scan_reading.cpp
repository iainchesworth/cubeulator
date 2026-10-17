#include "controllers/scan_reading.hpp"

#include <cube/model/facelet.hpp>
#include <cube/nxn/cube.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace cube::app {

namespace {

using cube::model::FacetColor;
using cube::solver::Color;
using cube::solver::Face;

constexpr int kSize = 3;
constexpr int kCells = kSize * kSize;

using Side = std::array<cube::model::Facelet, kCells>;

[[nodiscard]] std::optional<Color> color_of(FacetColor colour) noexcept {
    switch (colour) {
        case FacetColor::white:
            return Color::White;
        case FacetColor::yellow:
            return Color::Yellow;
        case FacetColor::red:
            return Color::Red;
        case FacetColor::orange:
            return Color::Orange;
        case FacetColor::green:
            return Color::Green;
        case FacetColor::blue:
            return Color::Blue;
        case FacetColor::unknown:
            break;
    }
    return std::nullopt;
}

// The side turned a quarter clockwise: the cell at (row, col) goes to (col, 2 - row).
[[nodiscard]] Side turned(const Side& side) {
    Side out{};
    for (int row = 0; row < kSize; ++row) {
        for (int col = 0; col < kSize; ++col) {
            out.at(static_cast<std::size_t>((col * kSize) + (kSize - 1 - row))) =
                side.at(static_cast<std::size_t>((row * kSize) + col));
        }
    }
    return out;
}

[[nodiscard]] Reading blank() {
    Reading reading;
    reading.size = kSize;
    reading.stickers.assign(6 * kCells, kUnpainted);
    reading.doubtful.assign(6 * kCells, 0);
    return reading;
}

}  // namespace

Reading reading_from_cube(const cube::solver::Cube& cube,
                          const std::optional<cube::vision::ColorResolution>& naming) {
    const cube::nxn::NCube held = cube::nxn::NCube::from_cube3(cube);
    Reading reading = blank();
    for (std::size_t i = 0; i < held.facelets().size(); ++i) {
        reading.stickers.at(i) = static_cast<std::int8_t>(held.facelets()[i]);
    }
    if (!naming.has_value()) {
        return reading;
    }

    for (const Face face : cube::solver::kAllFaces) {
        // The camera saw this side from outside, as CubeEdit shows it, so the two
        // differ by a turn in place and nothing else.
        std::array<Color, kCells> seen{};
        for (int i = 0; i < kCells; ++i) {
            seen.at(static_cast<std::size_t>(i)) = held.facelet(seen_facelet(face, kSize, i));
        }
        const Color middle = seen.at(kCells / 2);

        for (const Side& named : naming->faces) {
            const std::optional<Color> centre = color_of(named.at(kCells / 2).color);
            if (!centre.has_value() || *centre != middle) {
                continue;
            }
            Side side = named;
            for (int turns = 0; turns < 4; ++turns) {
                bool agrees = true;
                for (int i = 0; i < kCells && agrees; ++i) {
                    agrees = color_of(side.at(static_cast<std::size_t>(i)).color) ==
                             std::optional<Color>{seen.at(static_cast<std::size_t>(i))};
                }
                if (agrees) {
                    for (int i = 0; i < kCells; ++i) {
                        reading.doubtful.at(seen_facelet(face, kSize, i)) =
                            side.at(static_cast<std::size_t>(i)).confidence < kDoubtBelow ? 1 : 0;
                    }
                    break;
                }
                side = turned(side);
            }
            break;
        }
    }
    return reading;
}

std::optional<Reading> reading_from_named_sides(const cube::vision::ColorResolution& naming) {
    Reading reading = blank();
    reading.upright = false;
    std::array<bool, 6> placed{};
    for (const Side& named : naming.faces) {
        const std::optional<Color> centre = color_of(named.at(kCells / 2).color);
        if (!centre.has_value()) {
            continue;
        }
        // A side belongs where its middle colour is the colour that side wears when
        // the cube is solved (canonical_color): the scan's own convention.
        for (const Face face : cube::solver::kAllFaces) {
            if (cube::solver::canonical_color(face) != *centre ||
                placed.at(static_cast<std::size_t>(face))) {
                continue;
            }
            placed.at(static_cast<std::size_t>(face)) = true;
            for (int i = 0; i < kCells; ++i) {
                const auto& sticker = named.at(static_cast<std::size_t>(i));
                const std::size_t at = seen_facelet(face, kSize, i);
                const std::optional<Color> colour = color_of(sticker.color);
                reading.stickers.at(at) =
                    colour.has_value() ? static_cast<std::int8_t>(*colour) : kUnpainted;
                reading.doubtful.at(at) = sticker.confidence < kDoubtBelow ? 1 : 0;
            }
            break;
        }
    }
    if (std::ranges::none_of(placed, [](bool p) { return p; })) {
        return std::nullopt;
    }
    return reading;
}

}  // namespace cube::app
