#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>

#include <cube/nxn/cube.hpp>
#include <cube/solver/cube.hpp>

// What a puzzle state is, said once, so that an algorithm can be written
// against the idea and not against NCube: a state has an edge length and a
// colour at every (face, row, col). Both the 3x3 cube::solver::Cube and the
// N x N cube here are one; so is anything a future scanner or simulator
// builds, as long as it can answer those two questions.
//
// Everything below is a read-only algorithm over that idea, written with
// ranges, or a view that shows one state as another without copying it.

namespace cube::nxn {

// The edge length of a puzzle state. A customisation point by overload (found
// by argument-dependent lookup); NCube and the 3x3 Cube have one.
[[nodiscard]] inline int puzzle_size(const NCube& cube) noexcept {
    return cube.size();
}
[[nodiscard]] constexpr int puzzle_size(const solver::Cube& /*cube*/) noexcept {
    return 3;
}

template <typename S>
concept FaceletState = requires(const S& state, Face face) {
    { puzzle_size(state) } -> std::convertible_to<int>;
    { state.at(face, 0, 0) } -> std::same_as<Color>;
};

// Every sticker of the state, in face * N^2 + row * N + col order, as a lazy
// range. It refers to `state`, which must outlive it.
template <FaceletState S>
[[nodiscard]] auto facelets_of(const S& state) {
    const int n = puzzle_size(state);
    return std::views::iota(0, 6 * n * n) | std::views::transform([&state, n](int i) {
               const int within = i % (n * n);
               return state.at(static_cast<Face>(i / (n * n)), within / n, within % n);
           });
}

// Whether every face of the state is a single colour.
template <FaceletState S>
[[nodiscard]] bool has_uniform_faces(const S& state) {
    const int n = puzzle_size(state);
    return std::ranges::all_of(std::views::iota(0, 6), [&state, n](int face) {
        const Color first = state.at(static_cast<Face>(face), 0, 0);
        return std::ranges::all_of(std::views::iota(0, n * n), [&](int within) {
            return state.at(static_cast<Face>(face), within / n, within % n) == first;
        });
    });
}

// How many stickers of each colour the state has (N^2 of each, on a real
// cube), indexed by Color's enumerator value.
template <FaceletState S>
[[nodiscard]] std::array<int, 6> colour_counts(const S& state) {
    std::array<int, 6> counts{};
    for (const Color colour : facelets_of(state)) {
        ++counts.at(static_cast<std::size_t>(colour));
    }
    return counts;
}

// --- A 3x3 made of an N x N --------------------------------------------------
//
// A view that maps the 6 x 3 x 3 index space of a 3x3 onto the corners, edge
// cubies and face centres of an N x N cube's storage: row or column 0 and 2
// are the cube's own first and last, and 1 is the middle one (the one beside
// the middle, when there is none). The result is a 3x3 you can read, and hand
// to code that reads 3x3s, with no copy and no knowledge of N on the reader's
// side - this mapping is the only place the size appears.
//
// What it shows is the cube's real 3x3 only once the cube is reduced, which
// is when its centres are solid and its edges are paired; before that it is a
// sample of stickers that have not been put in order.
//
// In terms of the standard library this is std::mdspan with a custom layout
// (a mapping that is injective but not exhaustive or strided); it is written
// out for the reason FaceletGrid is.
class Facelets3x3 {
public:
    constexpr Facelets3x3(std::span<const Color> data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    [[nodiscard]] static constexpr std::size_t extent(std::size_t rank) noexcept {
        return rank == 0 ? 6 : 3;
    }
    [[nodiscard]] static constexpr std::size_t rank() noexcept { return 3; }

    // Which of the underlying cube's rows (or columns) the 3x3's line `i` is.
    [[nodiscard]] constexpr std::size_t line(std::size_t i) const noexcept {
        if (i == 0) {
            return 0;
        }
        return i == 1 ? (size_ - 1) / 2 : size_ - 1;
    }

    [[nodiscard]] constexpr const Color& operator[](std::size_t face, std::size_t row,
                                                    std::size_t col) const noexcept {
        return data_[(face * size_ * size_) + (line(row) * size_) + line(col)];
    }

private:
    std::span<const Color> data_;
    std::size_t size_;
};

// `cube` seen as a 3x3. Requires cube.size() >= 3. Refers to the cube's
// storage, which must outlive the view.
[[nodiscard]] inline Facelets3x3 virtual_3x3(const NCube& cube) noexcept {
    return Facelets3x3{cube.facelets(), static_cast<std::size_t>(cube.size())};
}

}  // namespace cube::nxn
