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
//         -> up to three N x N lattices (read_corner)
//         -> which pairs share an edge, and which edge of each
//   a candidate cube -> does each side match it, and do its edges meet where
//                       the cube puts them (fit_corner, in corner_fit.hpp)
//
// No pose is estimated: the lattices are placed on the picture and nothing
// more is asked of them than their shared edges.

// A lattice's edges, as the side is read row-major from its top-left: its top
// row, right column, bottom row and left column.
enum class GridEdge : std::uint8_t { Top, Right, Bottom, Left };

// One side as seen at an angle: N x N cells, nine for a 3x3.
struct ObliqueSide {
    // Sticker centres in the input frame's pixels, row-major in the lattice's
    // own axes: index 0 is the corner whose column and row steps turn
    // clockwise in the picture, so that the side is read from outside the cube,
    // never mirrored. Its rotation is whatever the picture gives it.
    std::vector<cv::Point2f> centres = std::vector<cv::Point2f>(9);
    std::vector<cv::Vec3f> lab = std::vector<cv::Vec3f>(9);
    // Which cells had a blob on them; the rest are placed by the lattice.
    std::vector<bool> found = std::vector<bool>(9, false);
    float pitch = 0.0F;
};

// An edge two sides have in common. Walking the shared edge in each side's own
// reading order (Top left to right, Right top to bottom, Bottom right to left,
// Left bottom to top) goes opposite ways, so the stickers pair first with
// last: side `first`'s edge cell t sits against side `second`'s edge cell N-1-t.
struct SharedEdge {
    std::size_t first = 0;
    std::size_t second = 0;
    GridEdge on_first = GridEdge::Top;
    GridEdge on_second = GridEdge::Top;
};

struct CornerRead {
    // The cube's size: every side has size x size cells.
    int size = 3;
    std::vector<ObliqueSide> sides;
    std::vector<SharedEdge> edges;
};

// The biggest cube the corner reader reads. A frame holds up to three sides of N x N
// blobs and the search grows with the square of that: a frame with no side square to the
// camera takes about 2 ms to read at 4x4, 6 at 5x5 and 12 at 6x6 on a desktop (more than
// 20 ms for a 7x7 on one), a cost the app pays on the frames that have no side to read.
inline constexpr int kMaxCornerSize = 6;

// Finds two or three lattices that touch. Nothing when the frame shows fewer
// than two with a shared edge, or has too many blobs to be a cube in a hand.
// `bgr` is an 8-bit BGR frame; frames wider than the reader's working width are
// reduced first. `size` is the cube's (2 to kMaxCornerSize); nothing for another.
[[nodiscard]] std::optional<CornerRead> read_corner(const cv::Mat& bgr, int size = 3);
// The same read from a frame already prepared (prepared_frame.hpp), which is
// how a frame goes to read_side and then here without being worked on twice.
[[nodiscard]] std::optional<CornerRead> read_corner(const PreparedFrame& frame, int size = 3);

// How far a frame got through read_corner.
enum class CornerStage : std::uint8_t {
    // Fewer blobs than two sides have between them (twelve for a 3x3).
    too_few_blobs,
    // More than a cube in a hand makes: a busy room.
    too_many_blobs,
    // Fewer than two disjoint N x N windows of sticker blobs.
    no_pair_of_lattices,
    // Two or more, and no two of them share an edge.
    no_shared_edge,
    // A lattice runs off the picture, so a sticker has no colour to read.
    off_the_frame,
    // A read.
    read,
};

// What read_corner did with a frame, for whoever is finding out why two sides
// in view were not read (`cube_scan funnel`). The read is what read_corner
// gives; the rest is the working it already holds.
struct CornerExplained {
    CornerStage stage = CornerStage::too_few_blobs;
    // Sticker-shaped blobs the frame had, and the distinct windows of two thirds
    // or more of a side's stickers that the search made (zero when it was not reached).
    std::size_t blobs = 0;
    std::size_t windows = 0;
    std::optional<CornerRead> read;
};
[[nodiscard]] CornerExplained read_corner_explained(const PreparedFrame& frame, int cube_size = 3);

}  // namespace cube::vision
