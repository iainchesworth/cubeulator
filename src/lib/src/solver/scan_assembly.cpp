#include <cube/solver/scan_assembly.hpp>

#include <cube/model/face.hpp>
#include <cube/profiling.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace cube::solver {

namespace {

constexpr std::optional<Color> to_solver_color(model::FacetColor color) noexcept {
    switch (color) {
        case model::FacetColor::white:
            return Color::White;
        case model::FacetColor::yellow:
            return Color::Yellow;
        case model::FacetColor::red:
            return Color::Red;
        case model::FacetColor::orange:
            return Color::Orange;
        case model::FacetColor::green:
            return Color::Green;
        case model::FacetColor::blue:
            return Color::Blue;
        case model::FacetColor::unknown:
            return std::nullopt;
    }
    return std::nullopt;
}

// The inverse of canonical_color(): which cube position a centre colour
// pins a captured face to. Total over the enum, so a face position exists
// for every colour - it's the CALLER's centres that can collide, and
// assemble_scanned_faces_with_mapping() checks for that.
constexpr Face face_for_center_color(Color color) noexcept {
    switch (color) {
        case Color::Yellow:
            return Face::U;
        case Color::White:
            return Face::D;
        case Color::Red:
            return Face::L;
        case Color::Orange:
            return Face::R;
        case Color::Green:
            return Face::F;
        case Color::Blue:
            return Face::B;
    }
    return Face::U;
}

// One quarter-turn of a row-major 3x3 sticker grid. Direction convention:
// the OUTPUT is what the camera would have seen had the physical face been
// turned 90 degrees clockwise in the image plane, i.e. the sticker that
// ends up at (row, col) came from (2 - col, row):
//
//     rotated[3 * row + col] = source[3 * (2 - col) + row]
//
// which as a flat gather is rotated[i] = source[{6,3,0,7,4,1,8,5,2}[i]].
// The search below tries all four powers of this permutation per face, so
// the choice of direction cannot affect which assemblies are reachable -
// pinning it just keeps the tests honest. Centre index 4 is a fixed point,
// which is exactly why rotating a face can never change its identity.
constexpr std::array<std::size_t, 9> kClockwiseSource = {6, 3, 0, 7, 4, 1, 8, 5, 2};

constexpr std::array<Color, 9> rotated_clockwise(const std::array<Color, 9>& face) noexcept {
    std::array<Color, 9> rotated{};
    for (std::size_t i = 0; i < face.size(); ++i) {
        rotated.at(i) = face.at(kClockwiseSource.at(i));
    }
    return rotated;
}

}  // namespace

namespace {

// The six input faces, converted to solver colours and set at the cube
// position their centre pins them to.
struct PinnedFaces {
    std::array<std::array<Color, 9>, 6> base{};  // indexed by solver Face
    std::array<std::size_t, 6> input_index_of_face{};
};

// Step 1: convert colours and identify each input face's cube position by its
// centre. Six inputs against six distinct-centre slots means a collision check
// alone proves the mapping is a bijection.
std::optional<PinnedFaces> pin_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept {
    PinnedFaces pinned;
    std::array<bool, 6> face_assigned{};
    for (std::size_t input_index = 0; input_index < faces.size(); ++input_index) {
        std::array<Color, 9> converted{};
        for (std::size_t i = 0; i < converted.size(); ++i) {
            const auto color = to_solver_color(faces.at(input_index).at(i).color);
            if (!color) {
                return std::nullopt;
            }
            converted.at(i) = *color;
        }

        const Face face = face_for_center_color(converted.at(model::center_facelet_index));
        const auto face_index = static_cast<std::size_t>(face);
        if (face_assigned.at(face_index)) {
            return std::nullopt;  // duplicate centres
        }
        face_assigned.at(face_index) = true;
        pinned.base.at(face_index) = converted;
        pinned.input_index_of_face.at(face_index) = input_index;

        // Handedness correction. An outside-view capture relates to the
        // solver's cross-unfolded layout by a pure rotation only where
        // that layout is camera-handed (U, F, L); on D, R, B it is
        // MIRROR-handed and no quarter-rotation bridges a reflection.
        // Those inputs take one fixed flip here, centre-pinned because
        // which face an input is only becomes known now; the rotation
        // search absorbs whatever in-plane rotation remains, so WHICH
        // reflection is immaterial.
        if (face == Face::D || face == Face::R || face == Face::B) {
            for (std::size_t row = 0; row < 3; ++row) {
                std::swap(pinned.base.at(face_index).at(row * 3),
                          pinned.base.at(face_index).at((row * 3) + 2));
            }
        }
    }
    return pinned;
}

// Step 2: the four rotations of every face, computed once rather than
// re-rotated inside the 4096-combination loop.
std::array<std::array<std::array<Color, 9>, 4>, 6> rotations_of(
    const std::array<std::array<Color, 9>, 6>& base) noexcept {
    std::array<std::array<std::array<Color, 9>, 4>, 6> rotations{};
    for (std::size_t face_index = 0; face_index < rotations.size(); ++face_index) {
        rotations.at(face_index).at(0) = base.at(face_index);
        for (std::size_t quarter = 1; quarter < 4; ++quarter) {
            rotations.at(face_index).at(quarter) =
                rotated_clockwise(rotations.at(face_index).at(quarter - 1));
        }
    }
    return rotations;
}

// Step 3: exhaustive search over all 4^6 per-face rotation combinations,
// encoded as six base-4 digits of `combo`. from_facelets is the cheap
// physical-validity pre-filter; is_solvable is the authority. Distinct-cube
// deduplication (rather than counting raw combinations) is what makes
// rotationally symmetric faces - every face of a solved cube - degenerate
// instead of ambiguous. Two distinct solvable assemblies fail like none.
std::optional<Cube> unique_solvable_assembly(
    const std::array<std::array<std::array<Color, 9>, 4>, 6>& rotations) noexcept {
    std::optional<Cube> found;
    for (unsigned combo = 0; combo < 4096; ++combo) {
        std::array<std::array<Color, 9>, 6> candidate{};
        unsigned digits = combo;
        for (std::size_t face_index = 0; face_index < candidate.size(); ++face_index) {
            candidate.at(face_index) = rotations.at(face_index).at(digits & 3U);
            digits >>= 2U;
        }

        const auto cube = Cube::from_facelets(candidate);
        if (!cube || !cube->is_solvable()) {
            continue;
        }
        if (found && *found != *cube) {
            return std::nullopt;  // two distinct solvable assemblies
        }
        found = *cube;
    }
    return found;
}

}  // namespace

Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept {
    CUBE_ZONE_SCOPED_N("solver.assemble_scan");
    const std::optional<PinnedFaces> pinned = pin_faces(faces);
    if (!pinned) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    const std::optional<Cube> cube = unique_solvable_assembly(rotations_of(pinned->base));
    if (!cube) {
        // Zero survivors, or two distinct ones - see the header for why they
        // share invalid_cube_state.
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return AssembledScan{.cube = *cube, .input_index_of_face = pinned->input_index_of_face};
}

std::vector<Cube> assemble_all_scanned_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces, std::size_t limit) noexcept {
    CUBE_ZONE_SCOPED_N("solver.assemble_all");
    std::vector<Cube> survivors;
    const std::optional<PinnedFaces> pinned = pin_faces(faces);
    if (!pinned) {
        return survivors;
    }
    const auto rotations = rotations_of(pinned->base);
    for (unsigned combo = 0; combo < 4096 && survivors.size() < limit; ++combo) {
        std::array<std::array<Color, 9>, 6> candidate{};
        unsigned digits = combo;
        for (std::size_t face_index = 0; face_index < candidate.size(); ++face_index) {
            candidate.at(face_index) = rotations.at(face_index).at(digits & 3U);
            digits >>= 2U;
        }
        const auto cube = Cube::from_facelets(candidate);
        if (!cube || !cube->is_solvable()) {
            continue;
        }
        if (std::ranges::find(survivors, *cube) == survivors.end()) {
            survivors.push_back(*cube);
        }
    }
    return survivors;
}

Result<Cube> assemble_scanned_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept {
    // Checked by hand rather than with Result::transform: the Android NDK's
    // libc++ (r26) has no monadic std::expected.
    const auto assembled = assemble_scanned_faces_with_mapping(faces);
    if (!assembled) {
        return std::unexpected(assembled.error());
    }
    return assembled->cube;
}

}  // namespace cube::solver
