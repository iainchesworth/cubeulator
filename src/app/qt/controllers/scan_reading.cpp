#include "controllers/scan_reading.hpp"

#include <cube/model/facelet.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/strategy.hpp>

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

namespace {

struct Swap {
    Face a_face;
    int a_index;
    Face b_face;
    int b_index;
};

// Two stickers of different colours, on different pieces, whichever the scramble.
[[nodiscard]] Swap demo_swap(const cube::nxn::NCube& cube) {
    const int size = cube.size();
    const int last = size * size - 1;
    const Swap candidates[] = {
        {Face::F, 0, Face::U, last},
        {Face::F, 0, Face::R, 0},
        {Face::D, 0, Face::L, last},
        {Face::B, last, Face::U, 0},
    };
    for (const Swap& swap : candidates) {
        if (cube.facelet(seen_facelet(swap.a_face, size, swap.a_index)) !=
            cube.facelet(seen_facelet(swap.b_face, size, swap.b_index))) {
            return swap;
        }
    }
    return candidates[0];
}

[[nodiscard]] char side_letter(Face face) {
    switch (face) {
        case Face::U:
            return 'U';
        case Face::D:
            return 'D';
        case Face::L:
            return 'L';
        case Face::R:
            return 'R';
        case Face::F:
            return 'F';
        case Face::B:
            return 'B';
    }
    return 'U';
}

[[nodiscard]] cube::nxn::NCube demo_cube(unsigned seed, int size) {
    return cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 40 + (8 * size)});
}

}  // namespace

Reading demo_reading(unsigned seed, int size, bool upright) {
    const cube::nxn::NCube cube = demo_cube(seed, size);
    const Swap swap = demo_swap(cube);
    Reading reading;
    reading.size = cube.size();
    reading.upright = upright;
    for (const Color colour : cube.facelets()) {
        reading.stickers.push_back(static_cast<std::int8_t>(colour));
    }
    reading.doubtful.assign(reading.stickers.size(), 0);
    const std::size_t a = seen_facelet(swap.a_face, size, swap.a_index);
    const std::size_t b = seen_facelet(swap.b_face, size, swap.b_index);
    std::swap(reading.stickers.at(a), reading.stickers.at(b));
    reading.doubtful.at(a) = 1;
    reading.doubtful.at(b) = 1;
    // One the camera was unsure of and had right.
    reading.doubtful.at(seen_facelet(Face::D, size, size)) = 1;
    return reading;
}

std::array<DemoSticker, 2> demo_misread_stickers(unsigned seed, int size) {
    const cube::nxn::NCube cube = demo_cube(seed, size);
    const Swap swap = demo_swap(cube);
    return {DemoSticker{.side = side_letter(swap.a_face),
                        .index = swap.a_index,
                        .colour = static_cast<std::int8_t>(
                            cube.facelet(seen_facelet(swap.a_face, size, swap.a_index)))},
            DemoSticker{.side = side_letter(swap.b_face),
                        .index = swap.b_index,
                        .colour = static_cast<std::int8_t>(
                            cube.facelet(seen_facelet(swap.b_face, size, swap.b_index)))}};
}

}  // namespace cube::app
