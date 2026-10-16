#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cube/nxn/geometry.hpp>

// Cubing notation for a cube of any size: the names a person reads, types and
// is shown for turns of layers.
//
//   R        the outer layer of R, a clockwise quarter turn
//   R'  R2   counter-clockwise; a half turn (R2' is read as R2)
//   Rw       the outer two layers of R together (WCA); r is the older spelling
//   3Rw      the outer three layers; nRw the outer n
//   2R       the second layer from R only; nR the nth
//   2-3R     layers 2 to 3 from R together
//   x y z    the whole cube turned as R, U, F turn
//
// A layer is counted from the face it is named for, so "2R" and "4L" are the
// same layer of a 5x5; both are read, and each is printed as it was given.
// A name does not translate: these are letters and numbers, as at 3x3.
//
// The library's own turn is a single layer (LayerMove). A name can be several,
// turned together, so what notation names is a WideMove; layers_of() turns one
// into the layer turns the cube is moved by, and group() goes the other way for
// display. See docs/nxn-app-plan.md, "Notation".

namespace cube::nxn {

// A block of adjacent layers turned together, which is what one token of
// notation names. A single layer is a block of one.
struct WideMove {
    Face face = Face::U;
    // The depth of the nearest and the farthest layer of the block, counted
    // from `face`: 0 is the face's own outer layer. first <= last.
    int first = 0;
    int last = 0;
    // Clockwise quarter turns as seen from outside `face`: 1, 2 or 3.
    int turns = 1;

    [[nodiscard]] constexpr int layer_count() const noexcept { return last - first + 1; }

    [[nodiscard]] constexpr WideMove inverse() const noexcept {
        return WideMove{.face = face, .first = first, .last = last, .turns = 4 - turns};
    }

    friend constexpr bool operator==(const WideMove&, const WideMove&) = default;
};

// The turns of single layers a block is, nearest layer first. The block's turns
// are of different layers of one axis, which commute, so the order is only a
// convention.
[[nodiscard]] std::vector<LayerMove> layers_of(const WideMove& move);
[[nodiscard]] std::vector<LayerMove> layers_of(std::span<const WideMove> moves);

// The same physical turn spelled from the nearer end of its axis (the positive
// end, R U F, when the block is as near one as the other). 4L2 on a 5x5 is 2R2;
// the block that is every layer of an axis is spelled from the positive end.
[[nodiscard]] WideMove canonical(const WideMove& move, int size);

// Why a piece of text is not notation for a cube of this size.
struct ParseFault {
    enum class Kind : std::uint8_t {
        // Not a move at all: a letter that names none, a turn count that is not
        // 2 or a prime, anything left over.
        UnknownMove,
        // A move, but the cube has no such layer: 5R on a 4x4, 4Rw on a 3x3, a
        // range that runs backwards.
        LayerOutOfRange,
        // The size is not one the library builds.
        UnsupportedSize,
    };

    Kind kind = Kind::UnknownMove;
    // Where the token that failed starts in the text, and how long it is.
    std::size_t offset = 0;
    std::size_t length = 0;

    friend constexpr bool operator==(const ParseFault&, const ParseFault&) = default;
};

// Reads whitespace-separated notation for a cube of `size`. Empty text is no
// moves. Stops at the first token that is not a move for this cube.
[[nodiscard]] std::expected<std::vector<WideMove>, ParseFault> parse(std::string_view text,
                                                                     int size);

// A block as one token: R, Rw, 3Rw', 2R2, 2-3R, x. A block that is every layer
// of an axis is a rotation of the whole cube and is printed as one (x, y, z), so
// the text of such a block parses back to the same turn, spelled from the
// positive end. Any other block parses back to exactly itself.
[[nodiscard]] std::string to_string(const WideMove& move, int size);
[[nodiscard]] std::string to_string(LayerMove move, int size);
// Space-separated.
[[nodiscard]] std::string to_string(std::span<const WideMove> moves, int size);
[[nodiscard]] std::string to_string(std::span<const LayerMove> moves, int size);

// Gathers layer turns into the wide moves a person would name, for display. In
// a run of turns about one axis (which commute) the turns of adjacent layers
// through the same angle become one block, spelled from the nearer end, so the
// run "R 2R" is the one move Rw. Nothing is cancelled and no turn is added, so
// layers_of(group(moves)) is the same turns as `moves`, in an order that leaves
// every cube where `moves` would: a person who follows the grouped moves arrives
// where the originals arrive. Blocks keep the order their first turn came in, so
// moves with nothing to gather come back as they were. The number of blocks is
// the move count a person is shown; the original turns stay what is stored and
// replayed.
[[nodiscard]] std::vector<WideMove> group(std::span<const LayerMove> moves, int size);

}  // namespace cube::nxn
