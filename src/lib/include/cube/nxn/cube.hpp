#pragma once

#include <cstddef>
#include <cstdint>
#include <mdspan>
#include <span>
#include <vector>

#include <cube/error.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/solver/cube.hpp>

namespace cube::nxn {

// A read-only (face, row, col) view of an N x N x N cube's stickers, over the
// storage they are already in: six N x N matrices, indexed face-major. The
// face extent is static (a cube has six); the rows and columns are N.
using FaceletView =
    std::mdspan<const Color,
                std::extents<std::size_t, 6, std::dynamic_extent, std::dynamic_extent>>;

// An N x N x N cube's 6 * N * N stickers.
//
// The same idea as cube::solver::Cube - one colour per facelet, fixed by
// which face the sticker is on - for any size from 2 to kMaxSize. Storage is
// one flat array; view() reads it as the six N x N matrices (face, row, col)
// the layout is described by. A turn is a table lookup (see Geometry), so it
// costs the same for a layer deep inside a 7x7 as for the outer layer of a
// 3x3.
//
// A size-3 NCube is the same cube as a solver::Cube - same facelet numbering,
// same turns - and converts to and from one without loss, which is what lets
// the 3x3 algorithms and solvers run on it; see to_cube3() and apply(Move).
class NCube {
public:
    // A solved cube of `size`, coloured the way Cube::solved() is: each face
    // shows canonical_color(face). Fails with invalid_cube_state if `size`
    // is outside [kMinSize, kMaxSize].
    [[nodiscard]] static Result<NCube> solved(int size);

    // A cube from raw stickers, numbered face * N^2 + row * N + col. Fails
    // with invalid_cube_state if the count is not 6 * N^2 or a colour does not
    // appear exactly N^2 times (a necessary condition for a real cube).
    [[nodiscard]] static Result<NCube> from_facelets(int size, std::span<const Color> facelets);

    // The 3x3 cube `cube` as an NCube of size 3.
    [[nodiscard]] static NCube from_cube3(const solver::Cube& cube);

    [[nodiscard]] int size() const noexcept { return geometry_->size(); }
    [[nodiscard]] const Geometry& geometry() const noexcept { return *geometry_; }
    [[nodiscard]] std::span<const Color> facelets() const noexcept { return facelets_; }
    // One sticker by its facelet number.
    [[nodiscard]] Color facelet(std::size_t index) const { return facelets_.at(index); }

    [[nodiscard]] Color at(Face face, int row, int col) const noexcept {
        return facelets_.at(geometry_->index(face, row, col));
    }

    // The stickers as six N x N matrices: view()[face, row, col].
    [[nodiscard]] FaceletView view() const noexcept {
        const auto n = static_cast<std::size_t>(size());
        return FaceletView{facelets_.data(), n, n};
    }

    // --- Turning -----------------------------------------------------------

    void apply(SliceTurn turn);
    void apply(LayerMove move);
    // A 3x3 move is the turn of the outer layer of its face, on a cube of any
    // size, so every algorithm written for the 3x3 applies unchanged.
    void apply(solver::Move move);
    void apply(const Permutation& permutation);

    template <typename Move>
    void apply(std::span<const Move> moves) {
        for (const Move& move : moves) {
            apply(move);
        }
    }
    template <typename Move>
    void apply(const std::vector<Move>& moves) {
        apply(std::span<const Move>{moves});
    }

    // --- Questions ---------------------------------------------------------

    // True iff every face is a single colour, whichever colour that is. A
    // cube turned as a whole (whose faces are all uniform in some other
    // arrangement of the scheme) is solved: this class has no fixed centres on
    // even sizes, so "solved" cannot mean "equal to solved()".
    [[nodiscard]] bool is_solved() const noexcept;

    // Whether the cube can be treated as a 3x3 whose centres are solved and
    // whose edges are paired - the state the 3x3 core of a reduction needs
    // (each face's centre block a single colour, both stickers of every edge
    // the same all along it). Every cube of size 2 and 3 is reduced, though
    // only size 3 and up has a 3x3 to convert to.
    [[nodiscard]] bool is_reduced() const noexcept;

    // The 3x3 this cube is, once reduced: the corners as they are, each edge
    // as its paired wings show, each face's centre as its block's colour - and
    // every colour renamed so each centre shows canonical_color of its face.
    // That renaming is the whole-cube view change that makes the result a
    // cube the 3x3 solvers can read, which is also why the cube's size does
    // not matter to them. Fails with invalid_cube_state if the cube is not
    // reduced, is of size 2 (which has no edges to read), or has centres that
    // are not six different colours, and with whatever Cube::from_facelets
    // says about the result.
    [[nodiscard]] Result<solver::Cube> to_cube3() const;

    // The 3x3 a size-3 cube is, exactly as it is held: no colour is renamed,
    // so a cube whose centres are not where the 3x3 solvers' scheme puts them
    // (canonical_color) stays a cube they refuse as unsolvable, as it always
    // was for a scan assembled into the wrong slots. to_cube3() would read it
    // as the same cube held in another orientation and solve it. Fails with
    // invalid_cube_state if the size is not 3, and with whatever
    // Cube::from_facelets says about the stickers.
    [[nodiscard]] Result<solver::Cube> to_cube3_as_held() const;

    friend bool operator==(const NCube&, const NCube&) = default;

private:
    NCube(const Geometry& geometry, std::vector<Color> facelets)
        : geometry_(&geometry), facelets_(std::move(facelets)) {}

    const Geometry* geometry_;
    std::vector<Color> facelets_;
};

inline void apply_turn(NCube& cube, solver::Move move) {
    cube.apply(move);
}
inline void apply_turn(solver::Cube& cube, solver::Move move) {
    cube = cube.apply(move);
}

// Anything a 3x3 algorithm can be run on: a type that takes a face turn. Both
// solver::Cube (which hands back the turned cube) and NCube (which turns in
// place) are, through apply_turn, so an algorithm is run the same way on
// either.
template <typename C>
concept FaceTurnable = requires(C& cube, solver::Move move) { apply_turn(cube, move); };

// Runs `algorithm` (a 3x3 move sequence - R U R' U' ...) on any cube that can
// take face turns.
template <FaceTurnable C>
void run_algorithm(C& cube, std::span<const solver::Move> algorithm) {
    for (const solver::Move move : algorithm) {
        apply_turn(cube, move);
    }
}

// How to scramble a cube: a solved cube of `size` turned by `moves` random
// layer turns of any layer, drawn from `seed`. A struct of named fields
// because three integers in a row are easily written in the wrong order.
struct ScrambleSpec {
    int size = 3;
    unsigned seed = 1;
    int moves = 25;
};

// The same cube on every platform, for tests and benchmarks.
[[nodiscard]] NCube random_scramble(ScrambleSpec spec);

// The colours of the six faces' middles, by face: what the cube is "meant"
// to look like. Taken from the face centre cubies on an odd size; derived
// from the corner pieces on an even one, where the centres have no fixed
// identity until they are solved. Fails with invalid_cube_state when the
// corners do not agree on a single scheme.
[[nodiscard]] Result<std::array<Color, 6>> scheme_of(const NCube& cube);

// The same for stickers not (yet) held in an NCube, numbered face * N^2 +
// row * N + col. Also fails with invalid_cube_state when `size` is outside
// [kMinSize, kMaxSize] or the stickers are not that many colours.
[[nodiscard]] Result<std::array<Color, 6>> scheme_of(int size, std::span<const Color> facelets);

}  // namespace cube::nxn
