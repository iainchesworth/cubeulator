#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <optional>
#include <vector>

namespace cube::vision {

inline constexpr int grid_dimension = 3;
inline constexpr int cells_per_grid = grid_dimension * grid_dimension;

struct CellRegion {
    cv::Rect bounds;
};

struct GridDetection {
    std::array<CellRegion, cells_per_grid> cells{};
    float coverage_ratio = 0.0F;
};

// Locates a cube face's 3x3 sticker grid in a BGR frame: locally-adaptive
// thresholding isolates candidate sticker contours in a plausible
// size/shape range, then a RANSAC-style fit finds the best-scoring 3x3
// lattice explaining those candidates. Tolerates a minority of
// missed/merged/occluded cells (their CellRegion is extrapolated from the
// fitted lattice rather than the frame) and ignores background clutter that
// doesn't fit the lattice. Returns nullopt when no lattice hypothesis
// matches at least 4 of the 9 slots.
[[nodiscard]] std::optional<GridDetection> detect_cube_grid(const cv::Mat& bgr_frame);

// The lattice fit underlying detect_cube_grid(), reported in full rather
// than collapsed to axis-aligned cell rects. face_observation.hpp's
// homography refinement needs what the rects discard: the fitted basis
// vectors, the sub-pixel matched centers the homography is estimated from,
// and which slots were matched by a real contour versus extrapolated.
struct GridFitDetail {
    cv::Point2f anchor;    // frame position of the center slot (row 1, col 1)
    cv::Point2f col_step;  // basis vector for +1 column, in frame pixels
    cv::Point2f row_step;  // basis vector for +1 row, in frame pixels
    // Row-major; nullopt where no candidate contour matched the slot.
    std::array<std::optional<cv::Point2f>, cells_per_grid> matched_centers{};
    // Row-major; the matched contour's bounds, or extrapolated from the
    // lattice where unmatched - the same rule detect_cube_grid() applies.
    std::array<cv::Rect, cells_per_grid> cell_bounds{};
    int inlier_count = 0;  // slots matched by a real candidate contour, at most 9
};

// detect_cube_grid() with the underlying lattice fit exposed instead of the
// simplified GridDetection view. Same candidate extraction, same fit, same
// acceptance threshold; returns nullopt in exactly the cases
// detect_cube_grid() does.
[[nodiscard]] std::optional<GridFitDetail> detect_cube_grid_detailed(const cv::Mat& bgr_frame);

// Internal pipeline stages, exposed so face_observation.cpp can run the
// identical lattice fit over a *wider* candidate set (union of the
// brightness-threshold path below and its own HSV colour-mask path) without
// duplicating the fitting logic. Not a stable API for anything outside the
// vision module.
namespace detail {

// One candidate sticker contour: a roughly-square blob in the plausible
// size range for a grid cell.
struct StickerCandidate {
    cv::Rect bounds;
    cv::Point2f center;
    double area = 0.0;
};

// The CLAHE + adaptive-threshold candidate extraction detect_cube_grid()
// uses (see grid_detection.cpp for why that beats a global threshold).
[[nodiscard]] std::vector<StickerCandidate> find_square_candidates(const cv::Mat& bgr_frame);

// The inverted twin: cells DARKER than their local surroundings, which is
// what a cube with a light/white plastic lattice between tiles produces
// (its coloured cells are local brightness minima the forward threshold
// can never see). See grid_detection.cpp.
[[nodiscard]] std::vector<StickerCandidate> find_dark_square_candidates(const cv::Mat& bgr_frame);

// Contour extraction + area/squareness filtering over a caller-built 8-bit
// binary mask - the shared back half of find_square_candidates(), split out
// so alternative segmentation strategies feed the same filters and produce
// comparable candidates.
[[nodiscard]] std::vector<StickerCandidate> candidates_from_binary_mask(const cv::Mat& binary_mask);

// The RANSAC-style 3x3 lattice fit over an arbitrary candidate set.
// `frame_size` bounds extrapolated cell centers (an unmatched slot's
// reconstructed center must stay strictly inside the frame - downstream
// samplers index it unchecked). Returns nullopt when no hypothesis matches
// at least 6 of the 9 slots.
[[nodiscard]] std::optional<GridFitDetail> fit_lattice_detailed(std::vector<StickerCandidate> candidates,
                                                                 cv::Size frame_size);

// One possible placement of a PARTIAL lattice (2+ mutually-consistent
// candidates) within the 3x3 grid. Unlike GridFitDetail this carries no
// acceptance verdict: with only a few anchors the geometry alone cannot say
// which of the several slot placements is the real one (an L of three tiles
// fits four different corners of the face), so face_observation.cpp scores
// every placement photometrically and picks the survivor.
struct PartialPlacement {
    cv::Point2f anchor;    // frame position of the center slot (row 1, col 1)
    cv::Point2f col_step;  // basis vector for +1 column, in frame pixels
    cv::Point2f row_step;  // basis vector for +1 row, in frame pixels
    // Row-major; nullopt where no candidate matched the slot.
    std::array<std::optional<cv::Point2f>, cells_per_grid> matched_centers{};
    int inlier_count = 0;
};

// Enumerates plausible placements of partial lattices over `candidates`,
// for frames where fit_lattice_detailed() has too few inliers to accept a
// unique grid (a white-heavy face on a white-lattice cube segments only
// 2-3 coloured tiles). Seeds every candidate pair whose separation is
// consistent with one cell pitch predicted from the candidates' own tile
// size (pitch ~= 1.1x tile side - unlike fit_lattice_detailed's
// nearest-neighbour pitch estimate, this needs no minimum candidate count
// and cannot be dragged by background clutter), fits a lattice around each
// seed, then emits every translation of each fitted lattice that keeps all
// matched candidates inside the 3x3 and all nine cell centers inside the
// frame. Vertical seed pairs are allowed (2-anchor frames often segment a
// single column); results are ordered best-fit-first but carry NO
// photometric validation - callers must gate them against the frame.
[[nodiscard]] std::vector<PartialPlacement> partial_lattice_placements(std::vector<StickerCandidate> candidates,
                                                                        cv::Size frame_size);

}  // namespace detail

}  // namespace cube::vision
