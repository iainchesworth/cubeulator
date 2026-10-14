#pragma once

#include <cube/vision/prepared_frame.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cube::vision {

// Two or three sides of the cube in one view.
//
// Six square-on reads say what is on each side but not which way up each is
// relative to its neighbours, and for about one scrambled cube in eighty that
// leaves two cubes that fit (docs/recovery-plan.md, R5). A cube turned between
// sides, or held corner-on, shows two sides at once, and where they meet they
// share an edge: the stickers along it are neighbours. That is the one fact
// the flat reads lack.
//
//   frame -> sticker blobs, with the shear of a side seen at an angle
//         -> up to three 3x3 lattices (read_corner)
//         -> which pairs share an edge, and which edge of each
//   a candidate cube -> does each side match it, and do its edges meet where
//                       the cube puts them (fit_corner, in corner_fit.hpp)
//
// No pose is estimated: the lattices are placed on the picture and nothing
// more is asked of them than their shared edges.

// A lattice's edges, as the 3x3 is read row-major from its top-left: its top
// row, right column, bottom row and left column.
enum class GridEdge : std::uint8_t { Top, Right, Bottom, Left };

// One side as seen at an angle.
struct ObliqueSide {
    // Sticker centres in the input frame's pixels, row-major in the lattice's
    // own axes: index 0 is the corner whose column and row steps turn
    // clockwise in the picture, so that the side is read from outside the cube,
    // never mirrored. Its rotation is whatever the picture gives it.
    std::array<cv::Point2f, 9> centres{};
    std::array<cv::Vec3f, 9> lab{};
    // Which cells had a blob on them; the rest are placed by the lattice.
    std::array<bool, 9> found{};
    float pitch = 0.0F;
};

// An edge two sides have in common. Walking the shared edge in each side's own
// reading order (Top left to right, Right top to bottom, Bottom right to left,
// Left bottom to top) goes opposite ways, so the stickers pair first with
// last: side `first`'s edge cell t sits against side `second`'s edge cell 2-t.
struct SharedEdge {
    std::size_t first = 0;
    std::size_t second = 0;
    GridEdge on_first = GridEdge::Top;
    GridEdge on_second = GridEdge::Top;
};

struct CornerRead {
    std::vector<ObliqueSide> sides;
    std::vector<SharedEdge> edges;
};

// Finds two or three lattices that touch. Nothing when the frame shows fewer
// than two with a shared edge, or has too many blobs to be a cube in a hand.
// `bgr` is an 8-bit BGR frame; frames wider than the reader's working width are
// reduced first.
[[nodiscard]] std::optional<CornerRead> read_corner(const cv::Mat& bgr);
// The same read from a frame already prepared (prepared_frame.hpp), which is
// how a frame goes to read_side and then here without being worked on twice.
[[nodiscard]] std::optional<CornerRead> read_corner(const PreparedFrame& frame);

}  // namespace cube::vision
