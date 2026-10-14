#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <mdspan>
#include <ranges>

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
    // `size` is captured by value, as an init-capture, because the range
    // outlives this function; a plain capture of `n` would be flagged as
    // needless for a state whose size is a constant (the 3x3's).
    return std::views::iota(0, 6 * n * n) | std::views::transform([&state, size = n](int i) {
               const int within = i % (size * size);
               return state.at(static_cast<Face>(i / (size * size)), within / size, within % size);
           });
}

// Whether every face of the state is a single colour.
template <FaceletState S>
[[nodiscard]] bool has_uniform_faces(const S& state) {
    const int n = puzzle_size(state);
    return std::ranges::all_of(std::views::iota(0, 6), [&](int face) {
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
// An mdspan layout that maps the 6 x 3 x 3 index space of a 3x3 onto the
// corners, edge cubies and face centres of an N x N cube's storage: row or
// column 0 and 2 are the cube's own first and last, and 1 is the middle one
// (the one beside the middle, when there is none). The result is a 3x3 you can
// read, and hand to code that reads 3x3s, with no copy and no knowledge of N
// on the reader's side - the mapping is the only place the size appears.
//
// What it shows is the cube's real 3x3 only once the cube is reduced, which
// is when its centres are solid and its edges are paired; before that it is a
// sample of stickers that have not been put in order.
struct Projected3x3Layout {
    template <class Extents>
    class mapping {
    public:
        using extents_type = Extents;
        using index_type = Extents::index_type;
        using size_type = Extents::size_type;
        using rank_type = Extents::rank_type;
        using layout_type = Projected3x3Layout;

        constexpr mapping(const Extents& extents, index_type size) noexcept
            : extents_(extents), size_(size) {}

        [[nodiscard]] constexpr const Extents& extents() const noexcept { return extents_; }

        // The span of storage it reaches into: the whole cube's.
        [[nodiscard]] constexpr index_type required_span_size() const noexcept {
            return 6 * size_ * size_;
        }

        [[nodiscard]] constexpr index_type operator()(index_type face, index_type row,
                                                      index_type col) const noexcept {
            return (face * size_ * size_) + (line(row) * size_) + line(col);
        }

        // Distinct (face, row, col) reach distinct stickers for any size from
        // 3, but they are not a contiguous or strided run of the storage.
        [[nodiscard]] static constexpr bool is_always_unique() noexcept { return true; }
        [[nodiscard]] static constexpr bool is_always_exhaustive() noexcept { return false; }
        [[nodiscard]] static constexpr bool is_always_strided() noexcept { return false; }
        [[nodiscard]] constexpr bool is_unique() const noexcept { return true; }
        [[nodiscard]] constexpr bool is_exhaustive() const noexcept { return size_ == 3; }
        [[nodiscard]] constexpr bool is_strided() const noexcept { return false; }

        friend constexpr bool operator==(const mapping&, const mapping&) noexcept = default;

    private:
        // Which of the cube's rows (or columns) the 3x3's line `i` is.
        [[nodiscard]] constexpr index_type line(index_type i) const noexcept {
            if (i == 0) {
                return 0;
            }
            return i == 1 ? (size_ - 1) / 2 : size_ - 1;
        }

        Extents extents_;
        index_type size_;
    };
};

using Facelets3x3 =
    std::mdspan<const Color, std::extents<std::size_t, 6, 3, 3>, Projected3x3Layout>;

// `cube` seen as a 3x3. Requires cube.size() >= 3. Refers to the cube's
// storage, which must outlive the view.
[[nodiscard]] inline Facelets3x3 virtual_3x3(const NCube& cube) noexcept {
    using Mapping = Projected3x3Layout::mapping<std::extents<std::size_t, 6, 3, 3>>;
    return Facelets3x3{cube.facelets().data(), Mapping{std::extents<std::size_t, 6, 3, 3>{},
                                                       static_cast<std::size_t>(cube.size())}};
}

}  // namespace cube::nxn
