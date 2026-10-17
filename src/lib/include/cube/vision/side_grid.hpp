#pragma once

#include <cstddef>

namespace cube::vision {

// The geometry of a side of N x N stickers as the readers lay it out: cells
// numbered row * N + column, row-major from the one nearest the image's top-left.
// Nothing here is specific to 3: a 3x3's nine cells are the case N = 3.

// The side length of a square of `cells` stickers (3 for nine, 5 for
// twenty-five), or 0 when `cells` is not a square of two or more.
[[nodiscard]] constexpr int side_length_of(std::size_t cells) noexcept {
    std::size_t size = 0;
    while ((size + 1) * (size + 1) <= cells) {
        ++size;
    }
    return (size >= 2 && size * size == cells) ? static_cast<int>(size) : 0;
}

// The middle sticker of a side of an odd size N, the one a quarter-turn leaves
// where it was. An even size has none; it is answered as the first of the four
// middle ones, which nothing should lean on.
[[nodiscard]] constexpr std::size_t middle_cell(int size) noexcept {
    const auto n = static_cast<std::size_t>(size);
    return (((n - 1) / 2) * n) + ((n - 1) / 2);
}

// The cell of a read that lands at cell `cell` once the read is turned `roll`
// quarter-turns (0..3), which is what laying one read of a side on another is.
// For N = 3 the four rows are
//
//   0: 0 1 2 3 4 5 6 7 8     1: 2 5 8 1 4 7 0 3 6
//   2: 8 7 6 5 4 3 2 1 0     3: 6 3 0 7 4 1 8 5 2
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr std::size_t rolled_cell(int size, int roll, std::size_t cell) noexcept {
    const auto n = static_cast<std::size_t>(size);
    const std::size_t row = cell / n;
    const std::size_t col = cell % n;
    switch (static_cast<unsigned>(roll) % 4U) {
        case 1U:
            return (col * n) + (n - 1 - row);
        case 2U:
            return ((n - 1 - row) * n) + (n - 1 - col);
        case 3U:
            return ((n - 1 - col) * n) + row;
        default:
            return cell;
    }
}

static_assert(rolled_cell(3, 1, 0) == 2 && rolled_cell(3, 1, 1) == 5 && rolled_cell(3, 1, 3) == 1 &&
                  rolled_cell(3, 1, 8) == 6 && rolled_cell(3, 2, 0) == 8 &&
                  rolled_cell(3, 3, 0) == 6 && rolled_cell(3, 3, 1) == 3 &&
                  rolled_cell(3, 3, 8) == 2 && rolled_cell(3, 3, 4) == 4,
              "the quarter-turn tables of a 3x3 side");

}  // namespace cube::vision
