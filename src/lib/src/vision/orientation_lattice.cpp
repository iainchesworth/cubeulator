#include <cube/vision/orientation_lattice.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cube::vision {

namespace {

using cube::model::Face;

// ---------------------------------------------------------------------------
// Exact integer geometry. Every table exported by this file is computed at
// compile time from the small set of first principles below, so a table
// can only be wrong if a principle is wrong - and each principle is
// regression-locked by orientation_lattice_test.cpp against the code that
// owns it (cube.cpp's position_of(), scan_assembly.cpp's kClockwiseSource,
// corner_observation.cpp's corner_slot_mappings()).
// ---------------------------------------------------------------------------

struct Vec3 {
    int x = 0;
    int y = 0;
    int z = 0;

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

// Row-major 3x3 integer matrix; the lattice rotations are exactly the
// signed permutation matrices with determinant +1, so integer arithmetic
// is not an approximation but the natural representation.
struct Mat3 {
    std::array<int, 9> m{};

    [[nodiscard]] constexpr Vec3 apply(Vec3 v) const noexcept {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }

    // For rotations the transpose is the inverse.
    [[nodiscard]] constexpr Mat3 transposed() const noexcept {
        return {{m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]}};
    }

    friend constexpr Mat3 operator*(const Mat3& a, const Mat3& b) noexcept {
        Mat3 out;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                int sum = 0;
                for (int k = 0; k < 3; ++k) {
                    sum += a.m[static_cast<std::size_t>(3 * row + k)] *
                           b.m[static_cast<std::size_t>(3 * k + col)];
                }
                out.m[static_cast<std::size_t>(3 * row + col)] = sum;
            }
        }
        return out;
    }

    friend constexpr bool operator==(const Mat3&, const Mat3&) = default;
};

constexpr Mat3 kIdentityMat = {{1, 0, 0, 0, 1, 0, 0, 0, 1}};

// Body-frame outward normals per cube.cpp's face_normal(): x: L->R,
// y: D->U, z: B->F. Ordered by model::Face (U,R,F,D,L,B) so a Face value
// indexes directly.
constexpr std::array<Vec3, 6> kFaceNormal = {{
    {0, 1, 0},    // U
    {1, 0, 0},    // R
    {0, 0, 1},    // F
    {0, -1, 0},   // D
    {-1, 0, 0},   // L
    {0, 0, -1},   // B
}};

constexpr Face face_from_normal(Vec3 n) noexcept {
    for (std::size_t i = 0; i < kFaceNormal.size(); ++i) {
        if (kFaceNormal[i] == n) {
            return static_cast<Face>(i);
        }
    }
    return Face::U;  // unreachable for unit axis vectors
}

// Sticker position in [-1,0,1]^3 for solver row-major index `index` on
// `face`, and its inverse - transcribed from cube.cpp's position_of() /
// index_on_face() (the facelet-layout ground truth this whole library is
// pinned to; the transcription is what the regression tests lock).
constexpr Vec3 position_of(Face face, int index) noexcept {
    const int row = index / 3;
    const int col = index % 3;
    switch (face) {
        case Face::U:
            return {col - 1, 1, row - 1};
        case Face::D:
            return {col - 1, -1, row - 1};
        case Face::R:
            return {1, 1 - row, col - 1};
        case Face::L:
            return {-1, 1 - row, col - 1};
        case Face::F:
            return {col - 1, 1 - row, 1};
        case Face::B:
            return {col - 1, 1 - row, -1};
    }
    return {};
}

constexpr int index_on_face(Face face, Vec3 pos) noexcept {
    switch (face) {
        case Face::U:
        case Face::D:
            return (pos.z + 1) * 3 + (pos.x + 1);
        case Face::R:
        case Face::L:
            return (1 - pos.y) * 3 + (pos.z + 1);
        case Face::F:
        case Face::B:
            return (1 - pos.y) * 3 + (pos.x + 1);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// The 24 elements, in the header's 4*near + k order.
// ---------------------------------------------------------------------------

// k = 0 base orientation per near face (header comment documents the
// resulting on-screen appearance). Rows are display axes, columns body
// axes: entry (r, c) = dot(display_axis_r, image of body_axis_c).
constexpr std::array<Mat3, 6> kNearFaceBase = {{
    {{1, 0, 0, 0, 0, -1, 0, 1, 0}},    // U near: +90 about x, U normal -> +z
    {{0, 0, -1, 0, 1, 0, 1, 0, 0}},    // R near: -90 about y, R normal -> +z
    {{1, 0, 0, 0, 1, 0, 0, 0, 1}},     // F near: identity
    {{1, 0, 0, 0, 0, 1, 0, -1, 0}},    // D near: -90 about x, D normal -> +z
    {{0, 0, 1, 0, 1, 0, -1, 0, 0}},    // L near: +90 about y, L normal -> +z
    {{-1, 0, 0, 0, 1, 0, 0, 0, -1}},   // B near: 180 about y, U stays up
}};

// One quarter turn of the cube clockwise AS SEEN ON SCREEN (display +y is
// up, so screen-clockwise is -90 about +z: up (0,1,0) -> right (1,0,0)).
// Left-multiplied because the extra turn happens in the display frame.
constexpr Mat3 kScreenClockwise = {{0, 1, 0, -1, 0, 0, 0, 0, 1}};

constexpr std::array<Mat3, kLatticeSize> build_lattice_matrices() noexcept {
    std::array<Mat3, kLatticeSize> out{};
    for (std::size_t near = 0; near < 6; ++near) {
        Mat3 element = kNearFaceBase[near];
        for (std::size_t k = 0; k < 4; ++k) {
            out[4 * near + k] = element;
            element = kScreenClockwise * element;
        }
    }
    return out;
}

constexpr std::array<Mat3, kLatticeSize> kLatticeMat = build_lattice_matrices();

static_assert(kLatticeMat[kIdentityLatticeIndex] == kIdentityMat,
              "index convention: F near at k = 0 must be the identity orientation");

// Every element must put its claimed near face's normal on +z, and all 24
// must be distinct (a duplicate would mean a base/turn derivation error).
constexpr bool lattice_matrices_are_sound() noexcept {
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        if (kLatticeMat[i].apply(kFaceNormal[i / 4]) != Vec3{0, 0, 1}) {
            return false;
        }
        for (std::size_t j = i + 1; j < kLatticeSize; ++j) {
            if (kLatticeMat[i] == kLatticeMat[j]) {
                return false;
            }
        }
    }
    return true;
}
static_assert(lattice_matrices_are_sound());

// ---------------------------------------------------------------------------
// Quaternions from the integer matrices, still at compile time. The 24
// lattice quaternions only ever need sqrt of {1..4}-ish sums, well inside
// Newton-iteration territory.
// ---------------------------------------------------------------------------

constexpr double constexpr_sqrt(double value) noexcept {
    double guess = value > 1.0 ? value : 1.0;
    for (int i = 0; i < 64; ++i) {
        guess = 0.5 * (guess + value / guess);
    }
    return guess;
}

// Shepperd's method as in pose_estimation.cpp's quaternion_from_rotation,
// reduced: lattice matrices are exact, so branch selection only needs to
// dodge the zero divisors, not numerical cancellation.
constexpr Quat quaternion_from_matrix(const Mat3& r) noexcept {
    const double m00 = r.m[0];
    const double m01 = r.m[1];
    const double m02 = r.m[2];
    const double m10 = r.m[3];
    const double m11 = r.m[4];
    const double m12 = r.m[5];
    const double m20 = r.m[6];
    const double m21 = r.m[7];
    const double m22 = r.m[8];
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    const double trace = m00 + m11 + m22;
    if (trace > 0.0) {
        const double s = 2.0 * constexpr_sqrt(trace + 1.0);
        w = 0.25 * s;
        x = (m21 - m12) / s;
        y = (m02 - m20) / s;
        z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = 2.0 * constexpr_sqrt(1.0 + m00 - m11 - m22);
        w = (m21 - m12) / s;
        x = 0.25 * s;
        y = (m01 + m10) / s;
        z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = 2.0 * constexpr_sqrt(1.0 + m11 - m00 - m22);
        w = (m02 - m20) / s;
        x = (m01 + m10) / s;
        y = 0.25 * s;
        z = (m12 + m21) / s;
    } else {
        const double s = 2.0 * constexpr_sqrt(1.0 + m22 - m00 - m11);
        w = (m10 - m01) / s;
        x = (m02 + m20) / s;
        y = (m12 + m21) / s;
        z = 0.25 * s;
    }
    // Canonical double-cover representative per the header: w > 0, else
    // first nonzero component positive (180-degree elements have w = 0).
    double sign = 1.0;
    if (w < 0.0 || (w == 0.0 && (x < 0.0 || (x == 0.0 && (y < 0.0 || (y == 0.0 && z < 0.0)))))) {
        sign = -1.0;
    }
    const double norm = constexpr_sqrt(w * w + x * x + y * y + z * z);
    return {static_cast<float>(sign * w / norm), static_cast<float>(sign * x / norm),
            static_cast<float>(sign * y / norm), static_cast<float>(sign * z / norm)};
}

constexpr std::array<Quat, kLatticeSize> build_lattice_quats() noexcept {
    std::array<Quat, kLatticeSize> out{};
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        out[i] = quaternion_from_matrix(kLatticeMat[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Derived permutation tables.
// ---------------------------------------------------------------------------

constexpr std::array<std::array<Face, 6>, kLatticeSize> build_face_permutation() noexcept {
    std::array<std::array<Face, 6>, kLatticeSize> out{};
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        for (std::size_t f = 0; f < 6; ++f) {
            out[i][f] = face_from_normal(kLatticeMat[i].apply(kFaceNormal[f]));
        }
    }
    return out;
}

// The gauge behind every obs_to_body entry: resolve_alignment returning
// element i means body_orientation = obs_orientation * L_i, i.e. the
// detector's model frame relates to the body frame by G = L_i^-1
// (v_body = G * v_model). A model-frame sticker position therefore lands
// at body position transpose(M_i) * p_model, and index_on_face() names its
// solver facelet index. This is exact for ANY true orientation, not just
// lattice-aligned ones: the model frame is cube-fixed, so the mapping is
// rigid regardless of how far the pose is from a lattice element.
constexpr Vec3 model_to_body(std::size_t lattice_index, Vec3 p_model) noexcept {
    return kLatticeMat[lattice_index].transposed().apply(p_model);
}

// A frontal FaceObservation's screen-row-major cell (r, c) is, in the
// detector's model frame, the near-face (model F) sticker at
// (col - 1, 1 - row, +1): kCubeModel labels the detected quad's tl cell as
// its model near-face tl, and after pose_estimation.cpp's OpenCV->display
// axis flip the model frame is exactly the display-style cube frame used
// here (x right, y up, z toward viewer at canonical presentation).
constexpr std::array<FaceCellMap, kLatticeSize> build_face_obs_to_body() noexcept {
    std::array<FaceCellMap, kLatticeSize> out{};
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        const Face body_face = face_from_normal(model_to_body(i, kFaceNormal[static_cast<std::size_t>(Face::F)]));
        out[i].body_face = body_face;
        for (int obs = 0; obs < 9; ++obs) {
            const Vec3 p_model{obs % 3 - 1, 1 - obs / 3, 1};
            out[i].cell[static_cast<std::size_t>(obs)] = index_on_face(body_face, model_to_body(i, p_model));
        }
    }
    return out;
}

// Corner views in the corner detector's model frame. The detector's fixed
// screen sectors make its model assignment constant: top face = model U,
// lower-left = model F, lower-right = model R (project_cube_from_corner's
// kCornerModel). Each view's screen-row-major cell k shows a fixed model
// facelet, derived in corner_observation.cpp's corner_slot_mappings()
// comment for the first documented pose (where model == body, so the
// derivation there IS the model-frame statement): top and lower-left read
// solver row-major directly, lower-right is mirrored (solver (r, 2 - c)).
struct CornerViewModel {
    Face model_face;
    // model solver facelet index shown at view cell k
    std::array<int, 9> model_cell;
};

constexpr std::array<CornerViewModel, 3> kCornerViewModel = {{
    {Face::U, {0, 1, 2, 3, 4, 5, 6, 7, 8}},
    {Face::F, {0, 1, 2, 3, 4, 5, 6, 7, 8}},
    {Face::R, {2, 1, 0, 5, 4, 3, 8, 7, 6}},
}};

constexpr std::array<std::array<FaceCellMap, 3>, kLatticeSize> build_corner_obs_to_body() noexcept {
    std::array<std::array<FaceCellMap, 3>, kLatticeSize> out{};
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        for (std::size_t view = 0; view < 3; ++view) {
            const CornerViewModel& model = kCornerViewModel[view];
            const Face body_face =
                face_from_normal(model_to_body(i, kFaceNormal[static_cast<std::size_t>(model.model_face)]));
            out[i][view].body_face = body_face;
            for (std::size_t k = 0; k < 9; ++k) {
                const Vec3 p_model = position_of(model.model_face, model.model_cell[k]);
                out[i][view].cell[k] = index_on_face(body_face, model_to_body(i, p_model));
            }
        }
    }
    return out;
}

// Body-to-body sticker relabelling. Both frames name the SAME physical
// cube, so element g relates them by its own matrix directly: a sticker at
// body position p under the source labelling sits at M_g * p under the
// target labelling. The face it lands on is the one whose normal is
// M_g * normal(f) - exactly build_face_permutation's rule, so body_face
// here agrees with kFacePermutation by construction (asserted below) - and
// index_on_face names its solver facelet there.
//
// No transpose, unlike model_to_body above, and the difference is not
// incidental: there the lattice element is the INVERSE of an unknown
// model->body gauge, here it IS the source->target body rotation.
constexpr std::array<std::array<FaceCellMap, 6>, kLatticeSize> build_body_relabel() noexcept {
    std::array<std::array<FaceCellMap, 6>, kLatticeSize> out{};
    for (std::size_t g = 0; g < kLatticeSize; ++g) {
        for (std::size_t f = 0; f < 6; ++f) {
            const auto source = static_cast<Face>(f);
            const Face target = face_from_normal(kLatticeMat[g].apply(kFaceNormal[f]));
            out[g][f].body_face = target;
            for (int k = 0; k < 9; ++k) {
                out[g][f].cell[static_cast<std::size_t>(k)] =
                    index_on_face(target, kLatticeMat[g].apply(position_of(source, k)));
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Group algebra: composition and inverse, as index tables.
//
// The lattice is a GROUP, and until now nothing needed to say so - every
// consumer resolved one element against a prediction and used it alone.
// Epoch bridging needs the group structure itself: it learns "new epoch's
// labels relate to the dying epoch's by element g" and must fold that into
// a gauge already expressed as "the dying epoch's labels relate to the
// store's by element gauge". Composing the two is the whole operation, and
// composing PERMUTATIONS by hand at the call site would silently pick an
// order convention; a table derived from the exact integer matrices cannot.
//
// Both are exact: the matrices are signed permutations with determinant +1,
// so a product is again one of the 24 and lookup is equality, never a
// nearest-match over floats.
constexpr int index_of_matrix(const Mat3& m) noexcept {
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        if (kLatticeMat[i] == m) {
            return static_cast<int>(i);
        }
    }
    return -1;  // caught by the closure static_assert below
}

constexpr std::array<std::array<int, kLatticeSize>, kLatticeSize> build_lattice_compose() noexcept {
    std::array<std::array<int, kLatticeSize>, kLatticeSize> out{};
    for (std::size_t a = 0; a < kLatticeSize; ++a) {
        for (std::size_t b = 0; b < kLatticeSize; ++b) {
            out[a][b] = index_of_matrix(kLatticeMat[a] * kLatticeMat[b]);
        }
    }
    return out;
}

constexpr std::array<int, kLatticeSize> build_lattice_inverse() noexcept {
    std::array<int, kLatticeSize> out{};
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        // For a rotation the transpose IS the inverse, so this needs no
        // search over products - but it is verified as one below.
        out[i] = index_of_matrix(kLatticeMat[i].transposed());
    }
    return out;
}

// The group axioms, checked on the shipped tables. Closure (no -1) is the
// one that would bite first if a matrix derivation above were wrong; the
// identity and inverse laws pin the index conventions the bridge relies on.
constexpr bool lattice_group_is_sound() noexcept {
    const auto compose = build_lattice_compose();
    const auto inverse = build_lattice_inverse();
    const auto permutation = build_face_permutation();
    constexpr auto kId = static_cast<std::size_t>(kIdentityLatticeIndex);
    for (std::size_t a = 0; a < kLatticeSize; ++a) {
        if (inverse[a] < 0) {
            return false;
        }
        // Identity acts as identity on both sides.
        if (compose[a][kId] != static_cast<int>(a) || compose[kId][a] != static_cast<int>(a)) {
            return false;
        }
        // Inverse really inverts, both sides.
        if (compose[a][static_cast<std::size_t>(inverse[a])] != kIdentityLatticeIndex ||
            compose[static_cast<std::size_t>(inverse[a])][a] != kIdentityLatticeIndex) {
            return false;
        }
        for (std::size_t b = 0; b < kLatticeSize; ++b) {
            if (compose[a][b] < 0) {
                return false;  // closure
            }
            // Composition must agree with the face permutation it induces,
            // because that agreement is exactly what the router's gauge
            // fold relies on: store = perm[gauge][perm[g][f]] is only the
            // same as perm[compose[gauge][g]][f] if this holds.
            for (std::size_t f = 0; f < 6; ++f) {
                const auto via_pair =
                    permutation[a][static_cast<std::size_t>(permutation[b][f])];
                if (permutation[static_cast<std::size_t>(compose[a][b])][f] != via_pair) {
                    return false;
                }
            }
        }
    }
    return true;
}
static_assert(lattice_group_is_sound());

// Compile-time soundness: every cell map must be a permutation of 0..8
// fixing the centre (all are rigid in-plane images of a face grid), or a
// derivation above mixed up a frame.
constexpr bool cell_map_is_permutation(const FaceCellMap& map) noexcept {
    if (map.cell[4] != 4) {
        return false;
    }
    std::array<bool, 9> seen{};
    for (const int index : map.cell) {
        if (index < 0 || index > 8 || seen[static_cast<std::size_t>(index)]) {
            return false;
        }
        seen[static_cast<std::size_t>(index)] = true;
    }
    return true;
}

constexpr bool obs_tables_are_sound() noexcept {
    const auto face_maps = build_face_obs_to_body();
    const auto corner_maps = build_corner_obs_to_body();
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        if (!cell_map_is_permutation(face_maps[i])) {
            return false;
        }
        for (const FaceCellMap& map : corner_maps[i]) {
            if (!cell_map_is_permutation(map)) {
                return false;
            }
        }
        // The face path's near face and the corner path's lower-left view
        // both sit on model F, so they must agree per element.
        if (face_maps[i].body_face != corner_maps[i][1].body_face) {
            return false;
        }
    }
    return true;
}
static_assert(obs_tables_are_sound());

constexpr bool body_relabel_is_sound() noexcept {
    const auto relabel = build_body_relabel();
    const auto permutation = build_face_permutation();
    for (std::size_t g = 0; g < kLatticeSize; ++g) {
        for (std::size_t f = 0; f < 6; ++f) {
            if (!cell_map_is_permutation(relabel[g][f])) {
                return false;
            }
            // The cell map must refine kFacePermutation, never contradict
            // it: registration picks a hypothesis by face permutation and
            // then scores it cell-wise, so a disagreement here would score
            // one hypothesis against another's face assignment.
            if (relabel[g][f].body_face != permutation[g][f]) {
                return false;
            }
        }
        // The identity element must relabel nothing at all, or a stored
        // face would fail to match a re-show of itself under the gauge
        // that says "nothing moved".
        if (g == static_cast<std::size_t>(kIdentityLatticeIndex)) {
            for (std::size_t f = 0; f < 6; ++f) {
                for (int k = 0; k < 9; ++k) {
                    if (relabel[g][f].cell[static_cast<std::size_t>(k)] != k) {
                        return false;
                    }
                }
            }
        }
    }
    // THE property epoch re-registration stands on: for any source face,
    // all 24 elements produce DISTINCT (target face, cell map) pairs. The
    // four elements sharing a target face differ only by roll, and it is
    // precisely their cell maps that must differ - a rotation-invariant
    // fingerprint cannot separate them, which is why single-face
    // registration was structurally unable to resolve before this table.
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t a = 0; a < kLatticeSize; ++a) {
            for (std::size_t b = a + 1; b < kLatticeSize; ++b) {
                if (relabel[a][f].body_face == relabel[b][f].body_face &&
                    relabel[a][f].cell == relabel[b][f].cell) {
                    return false;
                }
            }
        }
    }
    return true;
}
static_assert(body_relabel_is_sound());

// ---------------------------------------------------------------------------
// Runtime float helpers.
// ---------------------------------------------------------------------------

[[nodiscard]] float quat_dot(const Quat& a, const Quat& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

// Geodesic distance from a double-cover-safe absolute dot product.
[[nodiscard]] float angle_deg_from_abs_dot(float abs_dot) noexcept {
    const float clamped = abs_dot > 1.0F ? 1.0F : abs_dot;
    return 2.0F * std::acos(clamped) * 57.29577951308232F;
}

}  // namespace

const std::array<Quat, kLatticeSize> kLattice24 = build_lattice_quats();
const std::array<std::array<Face, 6>, kLatticeSize> kFacePermutation = build_face_permutation();
const std::array<FaceCellMap, kLatticeSize> kFaceObsToBody = build_face_obs_to_body();
const std::array<std::array<FaceCellMap, 3>, kLatticeSize> kCornerObsToBody = build_corner_obs_to_body();
const std::array<std::array<FaceCellMap, 6>, kLatticeSize> kBodyRelabel = build_body_relabel();
const std::array<std::array<int, kLatticeSize>, kLatticeSize> kLatticeCompose = build_lattice_compose();
const std::array<int, kLatticeSize> kLatticeInverse = build_lattice_inverse();

Quat quat_multiply(const Quat& a, const Quat& b) noexcept {
    return {a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
            a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
            a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]};
}

Quat quat_conjugate(const Quat& q) noexcept { return {q[0], -q[1], -q[2], -q[3]}; }

float quat_angle_deg(const Quat& a, const Quat& b) noexcept {
    return angle_deg_from_abs_dot(std::abs(quat_dot(a, b)));
}

LatticeQuantization quantize24(const Quat& q) noexcept {
    LatticeQuantization best;
    float best_abs_dot = -1.0F;
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        const float abs_dot = std::abs(quat_dot(q, kLattice24[i]));
        if (abs_dot > best_abs_dot) {
            best_abs_dot = abs_dot;
            best.index = static_cast<int>(i);
        }
    }
    best.theta_deg = angle_deg_from_abs_dot(best_abs_dot);
    return best;
}

AlignmentResolution resolve_alignment(const Quat& q_obs, const Quat& q_pred) noexcept {
    // argmin of angle == argmax of |<q_obs * L_i, q_pred>|; track the
    // runner-up too because the margin is the whole point (a resolution
    // the prediction can't separate from its runner-up must not be
    // trusted, however small its own theta is).
    AlignmentResolution best;
    float best_abs_dot = -1.0F;
    float second_abs_dot = -1.0F;
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        const float abs_dot = std::abs(quat_dot(quat_multiply(q_obs, kLattice24[i]), q_pred));
        if (abs_dot > best_abs_dot) {
            second_abs_dot = best_abs_dot;
            best_abs_dot = abs_dot;
            best.lattice_index = static_cast<int>(i);
        } else if (abs_dot > second_abs_dot) {
            second_abs_dot = abs_dot;
        }
    }
    best.theta_deg = angle_deg_from_abs_dot(best_abs_dot);
    best.margin_deg = angle_deg_from_abs_dot(second_abs_dot) - best.theta_deg;
    return best;
}

float frontal_deg(const Quat& q) noexcept {
    // Display-frame z components of the rotated body axes are the third
    // row of q's rotation matrix; the most camera-facing of the six signed
    // normals has z component max |row entry|.
    const float zx = 2.0F * (q[1] * q[3] - q[0] * q[2]);
    const float zy = 2.0F * (q[2] * q[3] + q[0] * q[1]);
    const float zz = 1.0F - 2.0F * (q[1] * q[1] + q[2] * q[2]);
    float best = std::abs(zx);
    best = std::max(best, std::abs(zy));
    best = std::max(best, std::abs(zz));
    const float clamped = best > 1.0F ? 1.0F : best;
    return std::acos(clamped) * 57.29577951308232F;
}

float face_frontal_deg(const Quat& q, cube::model::Face face) noexcept {
    const float zx = 2.0F * (q[1] * q[3] - q[0] * q[2]);
    const float zy = 2.0F * (q[2] * q[3] + q[0] * q[1]);
    const float zz = 1.0F - 2.0F * (q[1] * q[1] + q[2] * q[2]);
    // cos(angle to viewer) = z component of the rotated outward normal;
    // kFaceNormal order U(+y), R(+x), F(+z), D(-y), L(-x), B(-z).
    const std::array<float, 6> facing = {zy, zx, zz, -zy, -zx, -zz};
    const float cosine = std::clamp(facing[static_cast<std::size_t>(face)], -1.0F, 1.0F);
    return std::acos(cosine) * 57.29577951308232F;
}

cube::model::Face nearest_face(const Quat& q) noexcept {
    const float zx = 2.0F * (q[1] * q[3] - q[0] * q[2]);
    const float zy = 2.0F * (q[2] * q[3] + q[0] * q[1]);
    const float zz = 1.0F - 2.0F * (q[1] * q[1] + q[2] * q[2]);
    // Same model::Face order as kFaceNormal: U(+zy), R(+zx), F(+zz),
    // D(-zy), L(-zx), B(-zz); strict > keeps ties on the lowest enum value.
    const std::array<float, 6> facing = {zy, zx, zz, -zy, -zx, -zz};
    std::size_t best = 0;
    for (std::size_t f = 1; f < facing.size(); ++f) {
        if (facing[f] > facing[best]) {
            best = f;
        }
    }
    return static_cast<Face>(best);
}

}  // namespace cube::vision
