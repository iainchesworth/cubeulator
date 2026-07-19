#pragma once

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <array>

namespace cube::vision {

// Provisional single-sticker classification against the fixed reference
// palette - used only for live UI display while scanning (overlay cell
// tints, progress cube). The authoritative classification is
// resolve_colors(), which runs once all six faces are captured.
[[nodiscard]] cube::model::Facelet classify_lab(const cv::Vec3f& lab);

// Final, global colour assignment for a completed scan: clusters all 54
// captured sticker colours into exactly 6 groups of exactly 9, seeded from
// the six face centres (which must be six distinct colours on any real
// cube). Because the cube's own stickers define the clusters, a global
// lighting shift or an unusual sticker shade moves every sample of that
// colour together and cancels out - the failure mode a fixed reference
// palette can't survive. Cluster identities are then named by nearest
// reference colour so downstream code still speaks FacetColor.
//
// Faces are indexed by cube::model::Face (U,R,F,D,L,B), matching
// ScanProgress. Fails with CubeError::no_valid_cube_detected when the six
// centre colours don't separate into six plausible clusters (e.g. two
// centres read as the same colour - a physically impossible scan).
[[nodiscard]] Result<std::array<std::array<cube::model::Facelet, 9>, 6>> resolve_colors(
    const std::array<CapturedFace, 6>& faces);

}  // namespace cube::vision
