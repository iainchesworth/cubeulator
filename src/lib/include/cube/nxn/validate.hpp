#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>

// Is this a cube a person could hold?
//
// The 3x3 answers with Cube::is_solvable(). This is the same question for a
// cube of any size, asked of the stickers alone: hand entry, a scan's assembly
// and a history entry read back from disk all hold a facelet array and need to
// know whether turns of layers could have produced it, and if not, what to tell
// the person.
//
// The rules are the solver's, said once. A turn of a layer rotates cubies, so
// what can differ between two cubes is only where the pieces are and how they
// are turned:
//
//   * the six face colours are whatever the centres (odd sizes) or the corners
//     (even sizes) say they are - a cube need not be coloured the way
//     Cube::solved() is, nor held the way it is - and every colour is renamed
//     to the canonical scheme before the pieces are read;
//   * every corner is one of the eight real corners, each once, in an
//     orientation, and the orientations add up to a multiple of three;
//   * every wing is a real wing of its ring, each once. A wing is told from its
//     twin on the same edge by which of its two colours is on which sticker, so
//     "a flipped wing" is another wing and there is nothing to add up;
//   * an odd size's middle edges are twelve real edges, each once, whose flips
//     add up to an even number;
//   * every ring of centre stickers holds each colour equally often. Centres
//     have no identity of their own, so nothing else can be asked of them;
//   * on an odd size only, the corners and the middle edges are permuted with
//     the same parity - the 3x3 rule. An even size has no middle edge to hold a
//     corner to, and a wing's turn has a parity of its own, so nothing ties
//     them together at all.
//
// tests/unit/nxn/validate_test.cpp holds these equal to the solver: every cube
// it can solve is valid, and every cube it refuses is not, over scrambles and
// over cubes damaged in each way a person could damage one.

namespace cube::nxn {

// Why a cube is not one a person could hold. Ordered as the checks are made, so
// the first is the most basic thing wrong.
enum class Fault : std::uint8_t {
    None,
    // The wrong number of stickers for the size, or a colour that is not one.
    NotACube,
    // A colour does not appear N^2 times.
    ColourCounts,
    // The colours cannot be the six faces of one cube: on an odd size the middle
    // stickers are not six different colours, on an even one the corners do
    // not agree on which colours are opposite.
    NoScheme,
    // A ring of centre stickers does not hold every colour equally often.
    CentreColours,
    // A corner, wing or middle edge shows colours no piece of its kind has.
    UnknownPiece,
    // Two pieces of one kind show the same colours, so another is missing.
    RepeatedPiece,
    // The corners are turned in place, the twists not adding up.
    TwistedCorner,
    // The middle edges are flipped, the flips not adding up.
    FlippedEdge,
    // Two pieces are swapped: the corners and the middle edges are not
    // permuted with the same parity.
    SwappedPieces,
};

[[nodiscard]] constexpr std::string_view to_string(Fault fault) noexcept {
    switch (fault) {
        case Fault::None:
            return "the cube is one a person could hold";
        case Fault::NotACube:
            return "the stickers are not those of a cube of this size";
        case Fault::ColourCounts:
            return "a colour does not appear as many times as a face has stickers";
        case Fault::NoScheme:
            return "the colours cannot be the six faces of one cube";
        case Fault::CentreColours:
            return "a ring of centre stickers does not hold every colour equally";
        case Fault::UnknownPiece:
            return "a piece shows colours that no piece of a cube has";
        case Fault::RepeatedPiece:
            return "two pieces show the same colours";
        case Fault::TwistedCorner:
            return "a corner is turned in place";
        case Fault::FlippedEdge:
            return "an edge is flipped";
        case Fault::SwappedPieces:
            return "two pieces are swapped";
    }
    return "unknown";
}

// The verdict, and where to look. `piece` and `facelet` say which kind of piece
// is at fault and a sticker of it (the one to point at first); they mean
// something only for the faults that are about one piece. A fault about the
// whole cube (the colour counts, the scheme, the parity) leaves them at their
// defaults.
struct Verdict {
    Fault fault = Fault::None;
    Role piece = Role::Corner;
    std::uint16_t facelet = 0;

    [[nodiscard]] constexpr bool ok() const noexcept { return fault == Fault::None; }

    friend constexpr bool operator==(const Verdict&, const Verdict&) = default;
};

// Whether `facelets` (numbered face * N^2 + row * N + col, as NCube is) are a
// cube of `size` that turns could have produced. Allocation-free and
// O(stickers) once the size's tables exist (the first call for a size builds
// them), so the scan assembly can ask it of every candidate.
[[nodiscard]] Verdict validate(int size, std::span<const Color> facelets);

[[nodiscard]] inline Verdict validate(const NCube& cube) {
    return validate(cube.size(), cube.facelets());
}

}  // namespace cube::nxn
