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

// What the search needs from six input faces: each one's four rotations,
// filed under the solver face its centre pins it to, and which input that
// was.
struct Prepared {
    std::array<std::array<std::array<Color, 9>, 4>, 6> rotations{};
    std::array<std::size_t, 6> input_index_of_face{};
};

Result<Prepared> prepare(const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept {
    // Step 1: convert colours and identify each input face's cube position
    // by its centre. Six inputs against six distinct-centre slots means a
    // collision check alone proves the mapping is a bijection.
    std::array<std::array<Color, 9>, 6> base{};  // indexed by solver Face
    std::array<std::size_t, 6> input_index_of_face{};
    std::array<bool, 6> face_assigned{};
    for (std::size_t input_index = 0; input_index < faces.size(); ++input_index) {
        std::array<Color, 9> converted{};
        for (std::size_t i = 0; i < converted.size(); ++i) {
            const auto color = to_solver_color(faces.at(input_index).at(i).color);
            if (!color) {
                return std::unexpected(CubeError::invalid_cube_state);
            }
            converted.at(i) = *color;
        }

        const Face face = face_for_center_color(converted.at(model::center_facelet_index));
        const auto face_index = static_cast<std::size_t>(face);
        if (face_assigned.at(face_index)) {
            return std::unexpected(CubeError::invalid_cube_state);  // duplicate centres
        }
        face_assigned.at(face_index) = true;
        base.at(face_index) = converted;
        input_index_of_face.at(face_index) = input_index;

        // Handedness correction. An outside-view capture relates to the
        // solver's cross-unfolded layout by a pure rotation only where
        // that layout is camera-handed (U, F, L); on D, R, B it is
        // MIRROR-handed - the same fact the guided path's per-face
        // tables encode (scan_conversion.cpp) - and no quarter-rotation
        // bridges a reflection. Those inputs take one fixed flip here,
        // centre-pinned because which face an input is only becomes
        // known now; the rotation search below absorbs whatever
        // in-plane rotation remains, so WHICH reflection is immaterial.
        if (face == Face::D || face == Face::R || face == Face::B) {
            for (int row = 0; row < 3; ++row) {
                std::swap(base.at(face_index).at(static_cast<std::size_t>(row * 3)),
                          base.at(face_index).at(static_cast<std::size_t>((row * 3) + 2)));
            }
        }
    }

    // Step 2: precompute the four rotations of every face once, rather
    // than re-rotating inside the 4096-combination loop.
    Prepared prepared;
    auto& rotations = prepared.rotations;
    for (std::size_t face_index = 0; face_index < rotations.size(); ++face_index) {
        rotations.at(face_index).at(0) = base.at(face_index);
        for (std::size_t quarter = 1; quarter < 4; ++quarter) {
            rotations.at(face_index).at(quarter) =
                rotated_clockwise(rotations.at(face_index).at(quarter - 1));
        }
    }

    prepared.input_index_of_face = input_index_of_face;
    return prepared;
}

}  // namespace

Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces) noexcept {
    return assemble_scanned_faces_with_mapping(faces, nullptr);
}

Result<AssembledScan> assemble_scanned_faces_with_mapping(
    const std::array<std::array<model::Facelet, 9>, 6>& faces, const RotationHint* hint) noexcept {
    CUBE_ZONE_SCOPED_N("solver.assemble_scan");
    const Result<Prepared> prepared = prepare(faces);
    if (!prepared) {
        return std::unexpected(prepared.error());
    }
    const auto& rotations = prepared->rotations;
    const auto& input_index_of_face = prepared->input_index_of_face;

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
            const int digit = hint->quarter_turns.at(input_index_of_face.at(face_index));
            if (digit >= 0 && digit < 4) {
                hint_digit_of_face.at(face_index) = digit;
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
            candidate.at(face_index) = rotations.at(face_index).at(digits & 3U);
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
            if (std::cmp_equal(hint_digit_of_face.at(face_index), digits & 3U)) {
                ++score;
            }
            digits >>= 2U;
        }
        bool merged = false;
        for (std::size_t i = 0; i < survivor_count; ++i) {
            if (survivors.at(i)->cube == *cube) {
                survivors.at(i)->best_score = std::max(survivors.at(i)->best_score, score);
                merged = true;
                break;
            }
        }
        if (!merged) {
            if (survivor_count == kMaxHintSurvivors) {
                return std::unexpected(CubeError::invalid_cube_state);
            }
            survivors.at(survivor_count++) = Survivor{.cube = *cube, .best_score = score};
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
            if (survivors.at(i)->best_score > survivors.at(best)->best_score) {
                best = i;
                tie = false;
            } else if (survivors.at(i)->best_score == survivors.at(best)->best_score) {
                tie = true;
            }
        }
        if (survivor_count > 1 && tie) {
            // The hint could not strictly separate the survivors: the
            // ambiguity stands, same failure and remedy as unhinted.
            return std::unexpected(CubeError::invalid_cube_state);
        }
        return AssembledScan{.cube = survivors.at(best)->cube,
                             .input_index_of_face = input_index_of_face};
    }

    if (!found) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return AssembledScan{.cube = *found, .input_index_of_face = input_index_of_face};
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

std::vector<Cube> assemble_all_scanned_faces(
    const std::array<std::array<model::Facelet, 9>, 6>& faces, std::size_t limit) noexcept {
    CUBE_ZONE_SCOPED_N("solver.assemble_all");
    std::vector<Cube> survivors;
    const Result<Prepared> prepared = prepare(faces);
    if (!prepared) {
        return survivors;
    }
    for (unsigned combo = 0; combo < 4096 && survivors.size() < limit; ++combo) {
        std::array<std::array<Color, 9>, 6> candidate{};
        unsigned digits = combo;
        for (std::size_t face_index = 0; face_index < candidate.size(); ++face_index) {
            candidate[face_index] = prepared->rotations[face_index][digits & 3U];
            digits >>= 2U;
        }
        const auto cube = Cube::from_facelets(candidate);
        if (!cube || !cube->is_solvable()) {
            continue;
        }
        if (std::find(survivors.begin(), survivors.end(), *cube) == survivors.end()) {
            survivors.push_back(*cube);
        }
    }
    return survivors;
}

RotationHint rotation_hint_for_tracked_scan(
    const std::array<std::array<model::Facelet, 9>, 6>& faces,
    const std::array<int, 6>& model_face_ids) noexcept {
    // Solver-frame outward axis per Face - the same embedding cube.cpp's
    // face_from_normal/position_of derivation fixes (X runs L->R, Y D->U,
    // Z B->F).
    constexpr auto axis_of_face = [](Face face) -> std::array<int, 3> {
        switch (face) {
            case Face::R:
                return {1, 0, 0};
            case Face::L:
                return {-1, 0, 0};
            case Face::U:
                return {0, 1, 0};
            case Face::D:
                return {0, -1, 0};
            case Face::F:
                return {0, 0, 1};
            case Face::B:
                return {0, 0, -1};
        }
        return {0, 0, 0};
    };

    // Pin every input to its cube position by centre colour, exactly as
    // the assembly search itself will - a hint keyed any other way could
    // disagree with the search about which face it is advising.
    std::array<Face, 6> face_of_input{};
    std::array<bool, 6> face_taken{};
    std::array<bool, 6> id_taken{};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const int id = model_face_ids.at(i);
        if (id < 0 || id >= 6 || id_taken.at(static_cast<std::size_t>(id))) {
            return {};
        }
        id_taken.at(static_cast<std::size_t>(id)) = true;
        const auto color = to_solver_color(faces.at(i).at(model::center_facelet_index).color);
        if (!color) {
            return {};
        }
        const Face face = face_for_center_color(*color);
        if (face_taken.at(static_cast<std::size_t>(face))) {
            return {};
        }
        face_taken.at(static_cast<std::size_t>(face)) = true;
        face_of_input.at(i) = face;
    }

    // The body->solver rotation, column a = the image of body axis +e_a.
    // Each centre correspondence contributes one signed column; opposite
    // body faces must agree on it, and the columns must compose to a
    // proper rotation (det +1) - det 0 means two body axes claimed one
    // solver axis, det -1 a reflection, both misread centres, no advice.
    std::array<std::array<int, 3>, 3> column{};
    std::array<bool, 3> column_set{};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const int id = model_face_ids.at(i);
        const int body_axis = id / 2;
        const int sign = (id % 2 == 0) ? 1 : -1;
        const std::array<int, 3> solver_axis = axis_of_face(face_of_input.at(i));
        const std::array<int, 3> image = {sign * solver_axis.at(0), sign * solver_axis.at(1),
                                          sign * solver_axis.at(2)};
        if (!column_set.at(static_cast<std::size_t>(body_axis))) {
            column.at(static_cast<std::size_t>(body_axis)) = image;
            column_set.at(static_cast<std::size_t>(body_axis)) = true;
        } else if (column.at(static_cast<std::size_t>(body_axis)) != image) {
            return {};
        }
    }
    const auto& cx = column.at(0);
    const auto& cy = column.at(1);
    const auto& cz = column.at(2);
    const int det = (cx.at(0) * ((cy.at(1) * cz.at(2)) - (cy.at(2) * cz.at(1)))) -
                    (cy.at(0) * ((cx.at(1) * cz.at(2)) - (cx.at(2) * cz.at(1)))) +
                    (cz.at(0) * ((cx.at(1) * cy.at(2)) - (cx.at(2) * cy.at(1))));
    if (det != 1) {
        return {};
    }

    RotationHint hint;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const int id = model_face_ids.at(i);
        const int body_axis = id / 2;
        const int sign = (id % 2 == 0) ? 1 : -1;
        // Outside-view sampling axes, matching face_projection.cpp: the
        // cyclic pair on negative body faces, swapped on positive ones.
        const int axis_u = (sign > 0) ? (body_axis + 2) % 3 : (body_axis + 1) % 3;
        const int axis_v = (sign > 0) ? (body_axis + 1) % 3 : (body_axis + 2) % 3;
        const Face face = face_of_input.at(i);

        // Where each captured cell's sticker lands on the solver face:
        // body-frame cell centre -> solver frame -> the (row, col) formulas
        // documented in cube.hpp. assembled[j] = captured[source_of[j]].
        std::array<int, 9> source_of{};
        source_of.fill(-1);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                std::array<int, 3> p{};
                p.at(static_cast<std::size_t>(body_axis)) = sign;
                p.at(static_cast<std::size_t>(axis_u)) = col - 1;
                p.at(static_cast<std::size_t>(axis_v)) = row - 1;
                std::array<int, 3> q{};
                for (int k = 0; k < 3; ++k) {
                    q.at(static_cast<std::size_t>(k)) =
                        (column.at(0).at(static_cast<std::size_t>(k)) * p.at(0)) +
                        (column.at(1).at(static_cast<std::size_t>(k)) * p.at(1)) +
                        (column.at(2).at(static_cast<std::size_t>(k)) * p.at(2));
                }
                int assembled_index = -1;
                switch (face) {
                    case Face::U:
                    case Face::D:
                        assembled_index = ((q.at(2) + 1) * 3) + (q.at(0) + 1);
                        break;
                    case Face::R:
                    case Face::L:
                        assembled_index = ((1 - q.at(1)) * 3) + (q.at(2) + 1);
                        break;
                    case Face::F:
                    case Face::B:
                        assembled_index = ((1 - q.at(1)) * 3) + (q.at(0) + 1);
                        break;
                }
                if (assembled_index < 0 || assembled_index > 8) {
                    return {};
                }
                source_of.at(static_cast<std::size_t>(assembled_index)) = (row * 3) + col;
            }
        }

        // The digit is the k whose candidate array matches the geometry.
        // The search rotates the CORRECTED capture (assembly flips D, R,
        // B inputs before building rotations - see the correction in
        // assemble_scanned_faces_with_mapping), so the flip composes into
        // the comparison here: candidate[j] = captured[flip[C_k[j]]] on
        // corrected faces, captured[C_k[j]] otherwise. A proper rotation
        // always yields exactly one k; anything else is a convention
        // breach upstream, so the whole hint degrades.
        constexpr std::array<int, 9> kFlipColumns = {2, 1, 0, 5, 4, 3, 8, 7, 6};
        const bool flip_corrected = face == Face::D || face == Face::R || face == Face::B;
        std::array<int, 9> gather = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        int digit = RotationHint::kNoHint;
        for (int k = 0; k < 4; ++k) {
            bool matches = true;
            for (std::size_t j = 0; j < gather.size(); ++j) {
                const int captured_index =
                    flip_corrected ? kFlipColumns.at(static_cast<std::size_t>(gather.at(j)))
                                   : gather.at(j);
                if (captured_index != source_of.at(j)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                digit = k;
                break;
            }
            std::array<int, 9> next{};
            for (std::size_t j = 0; j < next.size(); ++j) {
                next.at(j) = gather.at(kClockwiseSource.at(j));
            }
            gather = next;
        }
        if (digit == RotationHint::kNoHint) {
            return {};
        }
        hint.quarter_turns.at(i) = digit;
    }
    return hint;
}

RotationHint rotation_hint_for_store_scan(const std::array<std::array<model::Facelet, 9>, 6>& faces,
                                          const std::array<model::Face, 6>& body_faces) noexcept {
    // Solver-frame outward axis per Face - the same embedding the tracked
    // producer above pins against (X runs L->R, Y D->U, Z B->F).
    constexpr auto axis_of_face = [](Face face) -> std::array<int, 3> {
        switch (face) {
            case Face::R:
                return {1, 0, 0};
            case Face::L:
                return {-1, 0, 0};
            case Face::U:
                return {0, 1, 0};
            case Face::D:
                return {0, -1, 0};
            case Face::F:
                return {0, 0, 1};
            case Face::B:
                return {0, 0, -1};
        }
        return {0, 0, 0};
    };
    // A body label's outward axis, by NAME: the store's slots are the
    // solver layout of their body face, so a model::Face name means in the
    // body frame exactly what the solver Face name means in the solver
    // frame. (model::Face and Face order their enumerators differently -
    // which is why this goes through names, never through indices.)
    constexpr auto body_axis_of = [](model::Face face) -> int {
        switch (face) {
            case model::Face::R:
            case model::Face::L:
                return 0;
            case model::Face::U:
            case model::Face::D:
                return 1;
            case model::Face::F:
            case model::Face::B:
                return 2;
        }
        return 0;
    };
    constexpr auto body_sign_of = [](model::Face face) -> int {
        switch (face) {
            case model::Face::U:
            case model::Face::R:
            case model::Face::F:
                return 1;
            case model::Face::D:
            case model::Face::L:
            case model::Face::B:
                return -1;
        }
        return 1;
    };

    // Pin every input to its cube position by centre colour, exactly as
    // the assembly search itself will - a hint keyed any other way could
    // disagree with the search about which face it is advising.
    std::array<Face, 6> face_of_input{};
    std::array<bool, 6> face_taken{};
    std::array<bool, 6> body_taken{};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto body_index = static_cast<std::size_t>(body_faces.at(i));
        if (body_index >= body_taken.size() || body_taken.at(body_index)) {
            return {};
        }
        body_taken.at(body_index) = true;
        const auto color = to_solver_color(faces.at(i).at(model::center_facelet_index).color);
        if (!color) {
            return {};
        }
        const Face face = face_for_center_color(*color);
        if (face_taken.at(static_cast<std::size_t>(face))) {
            return {};
        }
        face_taken.at(static_cast<std::size_t>(face)) = true;
        face_of_input.at(i) = face;
    }

    // The body->solver rotation, column a = the image of body axis +e_a.
    // Each centre correspondence contributes one signed column; opposite
    // body faces must agree on it, and the columns must compose to a
    // proper rotation (det +1) - det 0 means two body axes claimed one
    // solver axis, det -1 a reflection, both misread centres, no advice.
    std::array<std::array<int, 3>, 3> column{};
    std::array<bool, 3> column_set{};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const int body_axis = body_axis_of(body_faces.at(i));
        const int sign = body_sign_of(body_faces.at(i));
        const std::array<int, 3> solver_axis = axis_of_face(face_of_input.at(i));
        const std::array<int, 3> image = {sign * solver_axis.at(0), sign * solver_axis.at(1),
                                          sign * solver_axis.at(2)};
        if (!column_set.at(static_cast<std::size_t>(body_axis))) {
            column.at(static_cast<std::size_t>(body_axis)) = image;
            column_set.at(static_cast<std::size_t>(body_axis)) = true;
        } else if (column.at(static_cast<std::size_t>(body_axis)) != image) {
            return {};
        }
    }
    const auto& cx = column.at(0);
    const auto& cy = column.at(1);
    const auto& cz = column.at(2);
    const int det = (cx.at(0) * ((cy.at(1) * cz.at(2)) - (cy.at(2) * cz.at(1)))) -
                    (cy.at(0) * ((cx.at(1) * cz.at(2)) - (cx.at(2) * cz.at(1)))) +
                    (cz.at(0) * ((cx.at(1) * cy.at(2)) - (cx.at(2) * cy.at(1))));
    if (det != 1) {
        return {};
    }

    constexpr std::array<int, 9> kFlipColumns = {2, 1, 0, 5, 4, 3, 8, 7, 6};
    RotationHint hint;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const model::Face body = body_faces.at(i);
        const Face face = face_of_input.at(i);
        // Two independent column flips meet in this function, on two
        // different keys. The SEAM flip (slot_to_outside_view) was applied
        // on the BODY label before these inputs existed - undone per cell
        // below so the cell indexes the slot's solver layout again - while
        // the search's own correction keys on the PINNED solver face and
        // composes into the digit comparison, exactly as in the tracked
        // producer.
        const bool seam_flipped =
            body == model::Face::D || body == model::Face::R || body == model::Face::B;

        // Where each input cell's sticker lands on the solver face:
        // body-frame sticker position (position_of's embedding, applied to
        // the body NAME) -> solver frame -> the (row, col) formulas
        // documented in cube.hpp. assembled[j] = input[source_of[j]].
        std::array<int, 9> source_of{};
        source_of.fill(-1);
        for (int cell = 0; cell < 9; ++cell) {
            const int body_cell =
                seam_flipped ? kFlipColumns.at(static_cast<std::size_t>(cell)) : cell;
            const int row = body_cell / 3;
            const int col = body_cell % 3;
            std::array<int, 3> p{};
            switch (body) {
                case model::Face::U:
                    p = {col - 1, 1, row - 1};
                    break;
                case model::Face::D:
                    p = {col - 1, -1, row - 1};
                    break;
                case model::Face::R:
                    p = {1, 1 - row, col - 1};
                    break;
                case model::Face::L:
                    p = {-1, 1 - row, col - 1};
                    break;
                case model::Face::F:
                    p = {col - 1, 1 - row, 1};
                    break;
                case model::Face::B:
                    p = {col - 1, 1 - row, -1};
                    break;
            }
            std::array<int, 3> q{};
            for (int k = 0; k < 3; ++k) {
                q.at(static_cast<std::size_t>(k)) =
                    (column.at(0).at(static_cast<std::size_t>(k)) * p.at(0)) +
                    (column.at(1).at(static_cast<std::size_t>(k)) * p.at(1)) +
                    (column.at(2).at(static_cast<std::size_t>(k)) * p.at(2));
            }
            int assembled_index = -1;
            switch (face) {
                case Face::U:
                case Face::D:
                    assembled_index = ((q.at(2) + 1) * 3) + (q.at(0) + 1);
                    break;
                case Face::R:
                case Face::L:
                    assembled_index = ((1 - q.at(1)) * 3) + (q.at(2) + 1);
                    break;
                case Face::F:
                case Face::B:
                    assembled_index = ((1 - q.at(1)) * 3) + (q.at(0) + 1);
                    break;
            }
            if (assembled_index < 0 || assembled_index > 8) {
                return {};
            }
            source_of.at(static_cast<std::size_t>(assembled_index)) = cell;
        }

        // The digit is the k whose candidate array matches the geometry.
        // The search rotates the CORRECTED capture (assembly flips D, R,
        // B inputs before building rotations - see the correction in
        // assemble_scanned_faces_with_mapping), so the flip composes into
        // the comparison here: candidate[j] = input[flip[C_k[j]]] on
        // corrected faces, input[C_k[j]] otherwise. A proper rotation
        // always yields exactly one k; anything else is a convention
        // breach upstream, so the whole hint degrades.
        const bool flip_corrected = face == Face::D || face == Face::R || face == Face::B;
        std::array<int, 9> gather = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        int digit = RotationHint::kNoHint;
        for (int k = 0; k < 4; ++k) {
            bool matches = true;
            for (std::size_t j = 0; j < gather.size(); ++j) {
                const int input_index =
                    flip_corrected ? kFlipColumns.at(static_cast<std::size_t>(gather.at(j)))
                                   : gather.at(j);
                if (input_index != source_of.at(j)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                digit = k;
                break;
            }
            std::array<int, 9> next{};
            for (std::size_t j = 0; j < next.size(); ++j) {
                next.at(j) = gather.at(kClockwiseSource.at(j));
            }
            gather = next;
        }
        if (digit == RotationHint::kNoHint) {
            return {};
        }
        hint.quarter_turns.at(i) = digit;
    }
    return hint;
}

}  // namespace cube::solver
