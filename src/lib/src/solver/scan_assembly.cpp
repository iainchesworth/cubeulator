#include <cube/solver/scan_assembly.hpp>

#include <cube/model/face.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

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
        case Color::Orange:
            return Face::L;
        case Color::Red:
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
        rotated[i] = face[kClockwiseSource[i]];
    }
    return rotated;
}

}  // namespace

Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept {
    return assemble_scanned_faces_with_mapping(faces, nullptr);
}

Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces, const RotationHint* hint) noexcept {
    // Step 1: convert colours and identify each input face's cube position
    // by its centre. Six inputs against six distinct-centre slots means a
    // collision check alone proves the mapping is a bijection.
    std::array<std::array<Color, 9>, 6> base{};  // indexed by solver Face
    std::array<std::size_t, 6> input_index_of_face{};
    std::array<bool, 6> face_assigned{};
    for (std::size_t input_index = 0; input_index < faces.size(); ++input_index) {
        std::array<Color, 9> converted{};
        for (std::size_t i = 0; i < converted.size(); ++i) {
            const auto color = to_solver_color(faces[input_index][i].color);
            if (!color) {
                return std::unexpected(CubeError::invalid_cube_state);
            }
            converted[i] = *color;
        }

        const Face face = face_for_center_color(converted[model::center_facelet_index]);
        const auto face_index = static_cast<std::size_t>(face);
        if (face_assigned[face_index]) {
            return std::unexpected(CubeError::invalid_cube_state);  // duplicate centres
        }
        face_assigned[face_index] = true;
        base[face_index] = converted;
        input_index_of_face[face_index] = input_index;
    }

    // Step 2: precompute the four rotations of every face once, rather
    // than re-rotating inside the 4096-combination loop.
    std::array<std::array<std::array<Color, 9>, 4>, 6> rotations{};
    for (std::size_t face_index = 0; face_index < rotations.size(); ++face_index) {
        rotations[face_index][0] = base[face_index];
        for (std::size_t quarter = 1; quarter < 4; ++quarter) {
            rotations[face_index][quarter] = rotated_clockwise(rotations[face_index][quarter - 1]);
        }
    }

    // Re-key the hint from input indices to solver-face digit positions
    // (the combo's base-4 digits are indexed by solver Face below), and
    // decide whether it is USABLE - carrying at least one 0-3 entry.
    // Out-of-range entries degrade to "no expectation" per the header. An
    // unusable hint must take the exact unhinted code path, early-exit
    // included, so passing nullptr and passing an empty hint cannot ever
    // diverge in behaviour.
    std::array<int, 6> hint_digit_of_face{};
    hint_digit_of_face.fill(RotationHint::kNoHint);
    bool hint_usable = false;
    if (hint != nullptr) {
        for (std::size_t face_index = 0; face_index < hint_digit_of_face.size(); ++face_index) {
            const int digit = hint->quarter_turns[input_index_of_face[face_index]];
            if (digit >= 0 && digit < 4) {
                hint_digit_of_face[face_index] = digit;
                hint_usable = true;
            }
        }
    }

    // Step 3: exhaustive search over all 4^6 per-face rotation
    // combinations, encoded as six base-4 digits of `combo`. from_facelets
    // is the cheap physical-validity pre-filter; is_solvable is the
    // authority. Distinct-cube deduplication (rather than counting raw
    // combinations) is what makes rotationally symmetric faces - every
    // face of a solved cube - degenerate instead of ambiguous.
    //
    // Hinted mode collects EVERY distinct survivor with its best hint
    // score instead of failing at the second one: the hint's whole job is
    // to pick among genuinely distinct solvable assemblies, which only
    // near-solved cubes can produce at all - so the survivor set is tiny
    // in practice, and the fixed cap below both bounds memory and encodes
    // "more ambiguity than a hint can honestly arbitrate" (overflow fails
    // exactly like the unhinted ambiguity, same user remedy).
    struct Survivor {
        Cube cube;
        int best_score;
    };
    constexpr std::size_t kMaxHintSurvivors = 8;
    std::array<std::optional<Survivor>, kMaxHintSurvivors> survivors{};
    std::size_t survivor_count = 0;

    std::optional<Cube> found;
    for (unsigned combo = 0; combo < 4096; ++combo) {
        std::array<std::array<Color, 9>, 6> candidate{};
        unsigned digits = combo;
        for (std::size_t face_index = 0; face_index < candidate.size(); ++face_index) {
            candidate[face_index] = rotations[face_index][digits & 3U];
            digits >>= 2U;
        }

        const auto cube = Cube::from_facelets(candidate);
        if (!cube || !cube->is_solvable()) {
            continue;
        }

        if (!hint_usable) {
            if (!found) {
                found = *cube;
            } else if (*found != *cube) {
                // Two distinct solvable assemblies - see the header for why
                // this shares invalid_cube_state with the zero-survivor case.
                return std::unexpected(CubeError::invalid_cube_state);
            }
            continue;
        }

        // Hinted bookkeeping: score = matched hinted digits. Per-cube MAX
        // over combinations, because a rotationally symmetric face reaches
        // the same cube through several digits and the hint should judge
        // the friendliest reading, not an arbitrary one.
        int score = 0;
        digits = combo;
        for (std::size_t face_index = 0; face_index < candidate.size(); ++face_index) {
            if (hint_digit_of_face[face_index] == static_cast<int>(digits & 3U)) {
                ++score;
            }
            digits >>= 2U;
        }
        bool merged = false;
        for (std::size_t i = 0; i < survivor_count; ++i) {
            if (survivors[i]->cube == *cube) {
                survivors[i]->best_score = std::max(survivors[i]->best_score, score);
                merged = true;
                break;
            }
        }
        if (!merged) {
            if (survivor_count == kMaxHintSurvivors) {
                return std::unexpected(CubeError::invalid_cube_state);
            }
            survivors[survivor_count++] = Survivor{*cube, score};
        }
    }

    if (hint_usable) {
        if (survivor_count == 0) {
            return std::unexpected(CubeError::invalid_cube_state);
        }
        // A unique survivor wins regardless of its score - the hint only
        // ever SELECTS among ambiguity, it never vetoes a determined scan.
        std::size_t best = 0;
        bool tie = false;
        for (std::size_t i = 1; i < survivor_count; ++i) {
            if (survivors[i]->best_score > survivors[best]->best_score) {
                best = i;
                tie = false;
            } else if (survivors[i]->best_score == survivors[best]->best_score) {
                tie = true;
            }
        }
        if (survivor_count > 1 && tie) {
            // The hint could not strictly separate the survivors: the
            // ambiguity stands, same failure and remedy as unhinted.
            return std::unexpected(CubeError::invalid_cube_state);
        }
        return AssembledScan{survivors[best]->cube, input_index_of_face};
    }

    if (!found) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return AssembledScan{*found, input_index_of_face};
}

Result<Cube> assemble_scanned_faces(const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept {
    return assemble_scanned_faces_with_mapping(faces).transform(
        [](const AssembledScan& assembled) { return assembled.cube; });
}

}  // namespace cube::solver
