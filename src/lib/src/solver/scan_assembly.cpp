#include <cube/solver/scan_assembly.hpp>

#include <cube/model/face.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/search_work.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

// Which pair of stickers is an edge, and which three are a corner, in the
// cube's own layout: (face, facelet index) of each. They are found on a solved
// cube, where every edge and corner carries colours no other does, so that this
// does not repeat the layout cube.cpp already holds.
struct Cell {
    std::uint8_t face = 0;
    std::uint8_t index = 0;
};

struct Seams {
    // By the face of the seam that is turned LAST when faces are turned from the
    // sixth to the first: its lowest-numbered face, which is when a seam can be
    // looked at, all its faces having been turned.
    std::array<std::vector<std::array<Cell, 2>>, 6> edges;
    std::array<std::vector<std::array<Cell, 3>>, 6> corners;
};

// The three pairs of opposite colours, which no edge or corner carries two of.
constexpr int axis_pair_of(Color color) noexcept {
    switch (color) {
        case Color::White:
        case Color::Yellow:
            return 0;
        case Color::Orange:
        case Color::Red:
            return 1;
        case Color::Green:
        case Color::Blue:
            return 2;
    }
    return 0;
}

const Seams& seams() noexcept {
    static const Seams found = [] {
        Seams out;
        const Cube solved = Cube::solved();
        constexpr std::array<Color, 6> kColours = {
            Color::White, Color::Yellow, Color::Orange, Color::Red, Color::Green, Color::Blue,
        };
        const auto cell = [](Face face, int index) {
            return Cell{
                .face = static_cast<std::uint8_t>(face),
                .index = static_cast<std::uint8_t>(index),
            };
        };
        for (std::size_t i = 0; i < kColours.size(); ++i) {
            for (std::size_t j = i + 1; j < kColours.size(); ++j) {
                if (axis_pair_of(kColours.at(i)) == axis_pair_of(kColours.at(j))) {
                    continue;
                }
                const Cube::EdgeLocation at = solved.locate_edge(kColours.at(i), kColours.at(j));
                const std::array<Cell, 2> edge = {
                    cell(at.face_a, at.index_a),
                    cell(at.face_b, at.index_b),
                };
                out.edges.at(std::min(edge.at(0).face, edge.at(1).face)).push_back(edge);
                for (std::size_t k = j + 1; k < kColours.size(); ++k) {
                    if (axis_pair_of(kColours.at(k)) == axis_pair_of(kColours.at(i)) ||
                        axis_pair_of(kColours.at(k)) == axis_pair_of(kColours.at(j))) {
                        continue;
                    }
                    const Cube::CornerLocation corner =
                        solved.locate_corner(kColours.at(i), kColours.at(j), kColours.at(k));
                    const std::array<Cell, 3> cells = {
                        cell(corner.face_a, corner.index_a),
                        cell(corner.face_b, corner.index_b),
                        cell(corner.face_c, corner.index_c),
                    };
                    const std::uint8_t lowest =
                        std::min({cells.at(0).face, cells.at(1).face, cells.at(2).face});
                    out.corners.at(lowest).push_back(cells);
                }
            }
        }
        return out;
    }();
    return found;
}

// Whether the seams whose last face is `face` are possible on `candidate`:
// none carries two stickers of one axis pair, which is two of from_facelets'
// own checks (a subset of it, so a combination that fails here fails there).
bool seams_hold(std::size_t face, const std::array<std::array<Color, 9>, 6>& candidate) noexcept {
    const Seams& all = seams();
    const auto colour_at = [&candidate](const Cell& at) {
        return candidate.at(at.face).at(at.index);
    };
    const bool edges_hold = std::ranges::all_of(all.edges.at(face), [&colour_at](const auto& edge) {
        return axis_pair_of(colour_at(edge.at(0))) != axis_pair_of(colour_at(edge.at(1)));
    });
    return edges_hold &&
           std::ranges::all_of(all.corners.at(face), [&colour_at](const auto& corner) {
               const int a = axis_pair_of(colour_at(corner.at(0)));
               const int b = axis_pair_of(colour_at(corner.at(1)));
               const int c = axis_pair_of(colour_at(corner.at(2)));
               return a != b && a != c && b != c;
           });
}

// Every colour is on nine stickers (rotations only move them about a face), or
// no combination is a cube.
bool colour_counts_are_nine(
    const std::array<std::array<std::array<Color, 9>, 4>, 6>& rotations) noexcept {
    std::array<int, 6> counts{};
    for (const auto& face : rotations) {
        for (const Color color : face.at(0)) {
            ++counts.at(static_cast<std::size_t>(color));
        }
    }
    return std::ranges::all_of(counts, [](int count) { return count == 9; });
}

// Step 3: the 4^6 per-face rotation combinations, in the order of a base-4
// number with face 0 the lowest digit, and each cube they make that is
// physically valid and solvable handed to `visit`, which says whether to go on.
// from_facelets is the cheap physical-validity pre-filter; is_solvable is the
// authority. Distinct-cube deduplication (rather than counting raw
// combinations) is what makes rotationally symmetric faces - every face of a
// solved cube - degenerate instead of ambiguous.
//
// Not every combination is built and looked at: the faces are turned from the
// sixth to the first, and a combination is given up as soon as the faces turned
// so far put two stickers of one axis pair on an edge or a corner they all
// share - which from_facelets would refuse it for anyway, so the cubes found and
// the order they are found in are what trying all 4,096 gives (the unit test
// holds it to that on a few hundred inputs, cubes and not). What that saves is
// most of the search for sides that are no cube, a few microseconds where it
// was a millisecond; for sides that make cubes it is what a face that looks the
// same turned (below) saves, and what is left is from_facelets and is_solvable
// on each combination that survives.
//
// The work done is counted, as the other searches count theirs
// (search_work.hpp): one node for each time faces are looked at.
template <typename Visit>
void walk_solvable(const std::array<std::array<std::array<Color, 9>, 4>, 6>& rotations,
                   const Visit& visit) noexcept {
    if (!colour_counts_are_nine(rotations)) {
        return;
    }
    // A face that looks the same turned a quarter, a half or three quarters (a
    // face of one colour looks the same four ways) is tried once: the cube a
    // repeat makes is the cube the first made, one combination earlier, so it
    // adds nothing to what is found or to the order it is found in.
    std::array<std::array<std::size_t, 4>, 6> quarters{};
    std::array<std::size_t, 6> distinct{};
    for (std::size_t face = 0; face < rotations.size(); ++face) {
        for (std::size_t quarter = 0; quarter < 4; ++quarter) {
            const auto& turned = rotations.at(face);
            bool seen = false;
            for (std::size_t before = 0; before < distinct.at(face) && !seen; ++before) {
                seen = turned.at(quarters.at(face).at(before)) == turned.at(quarter);
            }
            if (!seen) {
                quarters.at(face).at(distinct.at(face)++) = quarter;
            }
        }
    }
    std::array<std::array<Color, 9>, 6> candidate{};
    bool going = true;
    std::uint64_t nodes = 0;
    const detail::SearchWorkScope work([&nodes] { return nodes; });
    const auto turn = [&](const auto& self, std::size_t face) -> void {
        for (std::size_t at = 0; at < distinct.at(face) && going; ++at) {
            candidate.at(face) = rotations.at(face).at(quarters.at(face).at(at));
            ++nodes;
            if (!seams_hold(face, candidate)) {
                continue;
            }
            if (face > 0) {
                self(self, face - 1);
                continue;
            }
            const auto cube = Cube::from_facelets(candidate);
            if (cube && cube->is_solvable()) {
                going = visit(*cube);
            }
        }
    };
    turn(turn, rotations.size() - 1);
}

std::optional<Cube> unique_solvable_assembly(
    const std::array<std::array<std::array<Color, 9>, 4>, 6>& rotations) noexcept {
    std::optional<Cube> found;
    bool ambiguous = false;
    walk_solvable(rotations, [&found, &ambiguous](const Cube& cube) {
        if (found && *found != cube) {
            ambiguous = true;  // two distinct solvable assemblies
            return false;
        }
        found = cube;
        return true;
    });
    return ambiguous ? std::nullopt : found;
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
    if (limit == 0) {
        return survivors;
    }
    walk_solvable(rotations, [&survivors, limit](const Cube& cube) {
        if (std::ranges::find(survivors, cube) == survivors.end()) {
            survivors.push_back(cube);
        }
        return survivors.size() < limit;
    });
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

namespace {

// Six vectors of nine as six arrays of nine, or nothing if they are not.
std::optional<std::array<std::array<model::Facelet, 9>, 6>> as_arrays(
    const std::array<std::vector<model::Facelet>, 6>& faces) noexcept {
    std::array<std::array<model::Facelet, 9>, 6> out{};
    for (std::size_t side = 0; side < faces.size(); ++side) {
        if (faces.at(side).size() != 9) {
            return std::nullopt;
        }
        std::ranges::copy(faces.at(side), out.at(side).begin());
    }
    return out;
}

}  // namespace

Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::vector<model::Facelet>, 6>& faces) noexcept {
    const auto arrays = as_arrays(faces);
    if (!arrays) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return assemble_scanned_faces_with_mapping(*arrays);
}

std::vector<Cube> assemble_all_scanned_faces(
    const std::array<std::vector<model::Facelet>, 6>& faces, std::size_t limit) noexcept {
    const auto arrays = as_arrays(faces);
    return arrays ? assemble_all_scanned_faces(*arrays, limit) : std::vector<Cube>{};
}

Result<Cube> assemble_scanned_faces(
    const std::array<std::vector<model::Facelet>, 6>& faces) noexcept {
    const auto arrays = as_arrays(faces);
    if (!arrays) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return assemble_scanned_faces(*arrays);
}

}  // namespace cube::solver
