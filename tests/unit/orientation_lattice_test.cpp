#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/vision/corner_observation.hpp>
#include <cube/vision/orientation_lattice.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <random>

using namespace cube::vision;
using cube::model::Face;

namespace {

// ---------------------------------------------------------------------------
// Independent ground truth. Everything the lattice tables claim is
// re-derived here from the code that owns each convention - cube.cpp's
// position_of() facelet layout, scan_assembly.cpp's kClockwiseSource
// (re-derived from its documented (row, col) form, as
// scan_assembly_test.cpp does), scan_conversion.hpp's guided holding
// convention, and the public corner_slot_mappings() - never copied from
// orientation_lattice.cpp, so a mis-derived table cannot vouch for itself.
// ---------------------------------------------------------------------------

struct Vec3 {
    int x = 0;
    int y = 0;
    int z = 0;

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
constexpr int dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// cube.cpp's face_normal(): x: L->R, y: D->U, z: B->F; model::Face order.
constexpr std::array<Vec3, 6> kNormal = {
    {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, {-1, 0, 0}, {0, 0, -1}}};

Vec3 normal_of(Face face) {
    return kNormal[static_cast<std::size_t>(face)];
}

Face face_of_normal(Vec3 n) {
    for (std::size_t i = 0; i < kNormal.size(); ++i) {
        if (kNormal[i] == n) {
            return static_cast<Face>(i);
        }
    }
    FAIL("not a unit axis normal");
    return Face::U;
}

// cube.cpp's position_of(): sticker position for solver row-major `index`.
Vec3 position_of(Face face, int index) {
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

// Integer 3x3 rotation, row-major.
struct Mat3 {
    std::array<int, 9> m{};

    [[nodiscard]] Vec3 apply(Vec3 v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
    [[nodiscard]] Mat3 transposed() const {
        return {{m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]}};
    }

    friend Mat3 operator*(const Mat3& a, const Mat3& b) {
        Mat3 out;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int k = 0; k < 3; ++k) {
                    sum += a.m[static_cast<std::size_t>(3 * r + k)] *
                           b.m[static_cast<std::size_t>(3 * k + c)];
                }
                out.m[static_cast<std::size_t>(3 * r + c)] = sum;
            }
        }
        return out;
    }
    friend bool operator==(const Mat3&, const Mat3&) = default;
};

constexpr Mat3 kIdentityMat = {{1, 0, 0, 0, 1, 0, 0, 0, 1}};

int det(const Mat3& a) {
    return a.m[0] * (a.m[4] * a.m[8] - a.m[5] * a.m[7]) -
           a.m[1] * (a.m[3] * a.m[8] - a.m[5] * a.m[6]) +
           a.m[2] * (a.m[3] * a.m[7] - a.m[4] * a.m[6]);
}

// Bridge from the shipped float quaternions to exact integer group math:
// rotate the basis vectors and round. The rounding tolerance doubles as
// the check that each quaternion really is one of the 24 axis-aligned
// rotations (anything else leaves a component far from an integer).
std::array<double, 3> rotate(const Quat& q, std::array<double, 3> v) {
    const Quat p = {0.0F, static_cast<float>(v[0]), static_cast<float>(v[1]),
                    static_cast<float>(v[2])};
    const Quat r = quat_multiply(quat_multiply(q, p), quat_conjugate(q));
    return {r[1], r[2], r[3]};
}

Mat3 int_matrix_of(const Quat& q) {
    Mat3 out;
    for (int col = 0; col < 3; ++col) {
        std::array<double, 3> basis = {0.0, 0.0, 0.0};
        basis[static_cast<std::size_t>(col)] = 1.0;
        const auto image = rotate(q, basis);
        for (int row = 0; row < 3; ++row) {
            const double component = image[static_cast<std::size_t>(row)];
            const int rounded = static_cast<int>(std::lround(component));
            REQUIRE(std::abs(component - rounded) < 1e-4);
            out.m[static_cast<std::size_t>(3 * row + col)] = rounded;
        }
    }
    return out;
}

std::array<Mat3, kLatticeSize> lattice_matrices() {
    std::array<Mat3, kLatticeSize> out;
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        out[i] = int_matrix_of(kLattice24[i]);
    }
    return out;
}

int index_of_matrix(const std::array<Mat3, kLatticeSize>& mats, const Mat3& wanted) {
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        if (mats[i] == wanted) {
            return static_cast<int>(i);
        }
    }
    // FAIL_CHECK rather than FAIL: FAIL throws, which makes the trailing
    // return unreachable and trips C4702-as-error in the unoptimized Debug
    // (sanitizer) config - the config that cannot otherwise build the unit
    // tests at all. The non-fatal form reports the same failure and lets
    // the sentinel return stand.
    FAIL_CHECK("matrix is not a lattice element");
    return -1;
}

Quat axis_angle(std::array<double, 3> axis, double degrees) {
    const double norm = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    const double half = degrees * std::numbers::pi / 360.0;
    const double s = std::sin(half) / norm;
    return {static_cast<float>(std::cos(half)), static_cast<float>(axis[0] * s),
            static_cast<float>(axis[1] * s), static_cast<float>(axis[2] * s)};
}

// scan_assembly.cpp's clockwise gather, re-derived from the (row, col)
// form: the sticker at (row, col) after a 90-degree clockwise image-plane
// turn came from (2 - col, row).
std::array<int, 9> clockwise_source() {
    std::array<int, 9> out{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out[static_cast<std::size_t>(3 * row + col)] = 3 * (2 - col) + row;
        }
    }
    return out;
}

// scan_conversion.cpp's camera_to_solver_permutation, re-derived from
// position_of() and the holding convention documented in
// scan_conversion.hpp ("that face points at the camera with a reference
// face toward the top of frame"): camera looks along -normal, screen-up is
// the reference face's normal, screen-right completes the right-handed
// screen basis (verified sense: viewing F with U up puts R on the right).
std::array<int, 9> camera_to_solver(Face face) {
    const Face top_reference = (face == Face::U || face == Face::D) ? Face::F : Face::U;
    const Vec3 view = {-normal_of(face).x, -normal_of(face).y, -normal_of(face).z};
    const Vec3 up = normal_of(top_reference);
    const Vec3 right = cross(view, up);
    std::array<int, 9> out{};
    for (int s = 0; s < 9; ++s) {
        const Vec3 p = position_of(face, s);
        const int row = 1 - dot(up, p);
        const int col = 1 + dot(right, p);
        out[static_cast<std::size_t>(3 * row + col)] = s;
    }
    return out;
}

}  // namespace

TEST_CASE("the 24 lattice quaternions are unit, sign-canonical integer rotations", "[lattice]") {
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        CAPTURE(i);
        const Quat& q = kLattice24[i];
        const float norm = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        REQUIRE(std::abs(norm - 1.0F) < 1e-5F);

        // Documented canonical double-cover representative.
        if (q[0] == 0.0F) {
            const float first_nonzero = q[1] != 0.0F ? q[1] : (q[2] != 0.0F ? q[2] : q[3]);
            REQUIRE(first_nonzero > 0.0F);
        } else {
            REQUIRE(q[0] > 0.0F);
        }

        // int_matrix_of() itself REQUIREs near-integer entries; a proper
        // rotation additionally needs determinant +1.
        REQUIRE(det(int_matrix_of(q)) == 1);
    }
}

TEST_CASE("the lattice is a closed group with inverses and no duplicate elements", "[lattice]") {
    const auto mats = lattice_matrices();

    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        for (std::size_t j = i + 1; j < kLatticeSize; ++j) {
            REQUIRE(!(mats[i] == mats[j]));
        }
    }
    REQUIRE(index_of_matrix(mats, kIdentityMat) == kIdentityLatticeIndex);

    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        // Inverse membership (transpose is the rotation inverse).
        REQUIRE(index_of_matrix(mats, mats[i].transposed()) >= 0);
        for (std::size_t j = 0; j < kLatticeSize; ++j) {
            CAPTURE(i, j);
            // Closure, and the quaternion product must agree with the
            // matrix product up to double-cover sign - the two
            // representations may never drift apart.
            const int product = index_of_matrix(mats, mats[i] * mats[j]);
            REQUIRE(quat_angle_deg(quat_multiply(kLattice24[i], kLattice24[j]),
                                   kLattice24[static_cast<std::size_t>(product)]) < 0.1F);
        }
    }
}

TEST_CASE("element 4n+k has body face n near after k screen-clockwise quarter turns", "[lattice]") {
    const auto mats = lattice_matrices();
    // Screen-clockwise quarter turn with +y up: up maps to right.
    const Mat3 screen_clockwise = {{0, 1, 0, -1, 0, 0, 0, 0, 1}};
    for (std::size_t near = 0; near < 6; ++near) {
        for (std::size_t k = 0; k < 4; ++k) {
            CAPTURE(near, k);
            const std::size_t i = 4 * near + k;
            REQUIRE(mats[i].apply(kNormal[near]) == Vec3{0, 0, 1});
            REQUIRE(mats[4 * near + (k + 1) % 4] == screen_clockwise * mats[i]);
        }
    }
}

TEST_CASE("kFacePermutation sends every sticker of every face to its claimed display slot",
          "[lattice]") {
    const auto mats = lattice_matrices();
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        std::array<bool, 6> hit{};
        for (std::size_t f = 0; f < 6; ++f) {
            CAPTURE(i, f);
            const Face display_slot = kFacePermutation[i][f];
            hit[static_cast<std::size_t>(display_slot)] = true;
            // Not just the normal: all nine position_of() stickers must
            // land on the claimed display face's plane, which is what ties
            // the permutation to cube.cpp's geometry rather than to a
            // separate normal table.
            for (int s = 0; s < 9; ++s) {
                const Vec3 moved = mats[i].apply(position_of(static_cast<Face>(f), s));
                REQUIRE(dot(moved, normal_of(display_slot)) == 1);
            }
        }
        for (const bool covered : hit) {
            REQUIRE(covered);  // bijection
        }
    }
}

TEST_CASE("quantize24 snaps every orientation within 40 degrees of an element back to it",
          "[lattice]") {
    std::mt19937 rng{20260720};
    std::uniform_real_distribution<double> angle_dist{1.0, 39.0};
    std::normal_distribution<double> gauss{0.0, 1.0};
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        for (int trial = 0; trial < 8; ++trial) {
            CAPTURE(i, trial);
            const double angle = angle_dist(rng);
            const Quat perturb = axis_angle({gauss(rng), gauss(rng), gauss(rng)}, angle);
            // Perturb on both sides (the angular metric is bi-invariant,
            // so both must snap identically) and through the double cover.
            for (Quat q :
                 {quat_multiply(kLattice24[i], perturb), quat_multiply(perturb, kLattice24[i])}) {
                for (int negate = 0; negate < 2; ++negate) {
                    const auto snapped = quantize24(q);
                    REQUIRE(snapped.index == static_cast<int>(i));
                    REQUIRE(std::abs(snapped.theta_deg - angle) < 0.6);
                    q = {-q[0], -q[1], -q[2], -q[3]};
                }
            }
        }
    }
}

TEST_CASE("resolve_alignment recovers an injected gauge under noise with a wide margin",
          "[lattice]") {
    const auto mats = lattice_matrices();

    // A canonical-presentation observation (q_obs = identity) of a cube at
    // lattice orientation L_i must resolve to element i itself: the
    // composition q_obs * L_i is the body orientation by definition.
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        CAPTURE(i);
        const auto resolved = resolve_alignment(Quat{1.0F, 0.0F, 0.0F, 0.0F}, kLattice24[i]);
        REQUIRE(resolved.lattice_index == static_cast<int>(i));
        REQUIRE(resolved.theta_deg < 0.1F);
        REQUIRE(resolved.margin_deg > 89.0F);
    }

    std::mt19937 rng{424242};
    std::normal_distribution<double> gauss{0.0, 1.0};
    std::uniform_real_distribution<double> noise_dist{0.5, 6.0};
    std::uniform_real_distribution<double> drift_dist{0.5, 10.0};
    for (int trial = 0; trial < 12; ++trial) {
        // Random true orientation, nowhere near lattice-aligned in general.
        Quat q_true = {static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng)),
                       static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng))};
        const float n = std::sqrt(q_true[0] * q_true[0] + q_true[1] * q_true[1] +
                                  q_true[2] * q_true[2] + q_true[3] * q_true[3]);
        q_true = {q_true[0] / n, q_true[1] / n, q_true[2] / n, q_true[3] / n};

        for (std::size_t j = 0; j < kLatticeSize; ++j) {
            CAPTURE(trial, j);
            const double noise_deg = noise_dist(rng);
            const double drift_deg = drift_dist(rng);
            const Quat noise = axis_angle({gauss(rng), gauss(rng), gauss(rng)}, noise_deg);
            const Quat drift = axis_angle({gauss(rng), gauss(rng), gauss(rng)}, drift_deg);

            // Model->body gauge G = L_j: the detector reports
            // q_obs = q_true * noise * L_j, so the resolving element is
            // j's group inverse.
            const Quat q_obs = quat_multiply(quat_multiply(q_true, noise), kLattice24[j]);
            const int expected = index_of_matrix(mats, mats[j].transposed());

            const auto exact_pred = resolve_alignment(q_obs, q_true);
            REQUIRE(exact_pred.lattice_index == expected);
            REQUIRE(std::abs(exact_pred.theta_deg - noise_deg) < 0.5);
            // Runner-up candidates sit >= 90 degrees from the winner, so
            // the margin survives the injected noise with room to spare.
            REQUIRE(exact_pred.margin_deg > 90.0F - 2.0F * static_cast<float>(noise_deg) - 5.0F);

            // A drifted prediction (coasting between detections) must not
            // change the resolution, only the reported theta.
            const auto drifted_pred = resolve_alignment(q_obs, quat_multiply(q_true, drift));
            REQUIRE(drifted_pred.lattice_index == expected);
            REQUIRE(drifted_pred.theta_deg < noise_deg + drift_deg + 0.5);

            // Double cover on either argument is invisible.
            const Quat neg_obs = {-q_obs[0], -q_obs[1], -q_obs[2], -q_obs[3]};
            const Quat neg_pred = {-q_true[0], -q_true[1], -q_true[2], -q_true[3]};
            REQUIRE(resolve_alignment(neg_obs, q_true).lattice_index == expected);
            REQUIRE(resolve_alignment(q_obs, neg_pred).lattice_index == expected);
        }
    }
}

TEST_CASE("kFaceObsToBody routes screen cells to the physical stickers for every element",
          "[lattice]") {
    const auto mats = lattice_matrices();
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        CAPTURE(i);
        const FaceCellMap& map = kFaceObsToBody[i];

        // The observed face must be the element's near face, consistently
        // across the tables and the runtime helpers.
        REQUIRE(kFacePermutation[i][static_cast<std::size_t>(map.body_face)] == Face::F);
        REQUIRE(nearest_face(kLattice24[i]) == map.body_face);

        // Independent derivation by exhaustive search: at body orientation
        // M_i, screen cell (r, c) of the near face shows whichever sticker
        // position_of() places at display position (c-1, 1-r, +1).
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                CAPTURE(r, c);
                const Vec3 display_pos{c - 1, 1 - r, 1};
                int found = -1;
                for (int s = 0; s < 9; ++s) {
                    if (mats[i].apply(position_of(map.body_face, s)) == display_pos) {
                        found = s;
                        break;
                    }
                }
                REQUIRE(found >= 0);
                REQUIRE(map.cell[static_cast<std::size_t>(3 * r + c)] == found);
            }
        }
    }
}

TEST_CASE("face routing is invariant across in-plane re-keying (kClockwiseSource lock)",
          "[lattice]") {
    const auto k_cw = clockwise_source();
    for (std::size_t near = 0; near < 6; ++near) {
        // Sticker "colours" chosen as their own solver indices, so a
        // routed screen array reads back which physical facelet each
        // screen cell claims to show.
        std::array<std::array<int, 9>, 4> screen{};
        for (std::size_t k = 0; k < 4; ++k) {
            const FaceCellMap& map = kFaceObsToBody[4 * near + k];
            REQUIRE(map.body_face == static_cast<Face>(near));
            for (std::size_t cell = 0; cell < 9; ++cell) {
                screen[k][cell] = map.cell[cell];
            }
        }
        // One more screen-clockwise quarter turn of the cube is exactly
        // scan_assembly.cpp's clockwise gather on what the camera sees; if
        // this holds, routing by the resolved element makes the physical
        // assignment identical for all four detector labellings, which is
        // the 45-degree-boundary immunity the tracker is built on.
        for (std::size_t k = 0; k < 4; ++k) {
            CAPTURE(near, k);
            for (std::size_t cell = 0; cell < 9; ++cell) {
                REQUIRE(screen[(k + 1) % 4][cell] ==
                        screen[k][static_cast<std::size_t>(k_cw[cell])]);
            }
        }
    }
}

TEST_CASE("kCornerObsToBody reproduces corner_slot_mappings for both documented poses",
          "[lattice]") {
    const auto mats = lattice_matrices();
    for (const auto view_id : {CornerViewId::first_corner, CornerViewId::second_corner}) {
        CAPTURE(view_id == CornerViewId::first_corner ? "first" : "second");
        const auto mappings = corner_slot_mappings(view_id);

        // The corner detector's model frame always reads {top, lower_left,
        // lower_right} as {U, F, R}; the documented pose says which BODY
        // faces those are, which pins the model->body gauge G columnwise
        // (columns are the images of the model x/y/z axes = model R/U/F).
        Mat3 gauge;
        const std::array<Vec3, 3> images = {
            normal_of(mappings[2].face), normal_of(mappings[0].face), normal_of(mappings[1].face)};
        for (int col = 0; col < 3; ++col) {
            gauge.m[static_cast<std::size_t>(col)] = images[static_cast<std::size_t>(col)].x;
            gauge.m[static_cast<std::size_t>(3 + col)] = images[static_cast<std::size_t>(col)].y;
            gauge.m[static_cast<std::size_t>(6 + col)] = images[static_cast<std::size_t>(col)].z;
        }
        const int resolved = index_of_matrix(mats, gauge.transposed());
        if (view_id == CornerViewId::first_corner) {
            // First corner is the identity gauge by construction (model
            // and body labels coincide).
            REQUIRE(resolved == kIdentityLatticeIndex);
        }

        for (std::size_t slot = 0; slot < 3; ++slot) {
            CAPTURE(slot);
            const FaceCellMap& map = kCornerObsToBody[static_cast<std::size_t>(resolved)][slot];
            REQUIRE(map.body_face == mappings[slot].face);
            // corner_slot_mappings goes view -> guided camera order; the
            // guided permutation (derived above from position_of and the
            // documented grips) goes camera -> solver. The composition is
            // the view -> solver mapping this library ships.
            const auto cam_to_solver = camera_to_solver(mappings[slot].face);
            for (std::size_t k = 0; k < 9; ++k) {
                REQUIRE(map.cell[k] ==
                        cam_to_solver[static_cast<std::size_t>(mappings[slot].to_scan_order[k])]);
            }
        }
    }
}

TEST_CASE("kCornerObsToBody is geometrically sound for all 24 elements", "[lattice]") {
    const auto mats = lattice_matrices();
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        CAPTURE(i);
        const auto& views = kCornerObsToBody[i];

        // The three views must be three distinct, mutually adjacent body
        // faces (they meet at a physical corner), with lower_left agreeing
        // with the face path (both sit on model F).
        REQUIRE(views[1].body_face == kFaceObsToBody[i].body_face);
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t b = a + 1; b < 3; ++b) {
                REQUIRE(views[a].body_face != views[b].body_face);
                REQUIRE(dot(normal_of(views[a].body_face), normal_of(views[b].body_face)) == 0);
            }
        }

        // Independent cell check for the top view (model U), mirroring the
        // face-path test: at body orientation M_i the model-U sticker seen
        // at view cell (r, c) sits at model position (c-1, 1, r-1) -
        // position_of(U, r, c) - so its body position is
        // G * that = transpose(M_i) * that.
        for (int cell = 0; cell < 9; ++cell) {
            const Vec3 body_pos = mats[i].transposed().apply(position_of(Face::U, cell));
            int found = -1;
            for (int s = 0; s < 9; ++s) {
                if (position_of(views[0].body_face, s) == body_pos) {
                    found = s;
                    break;
                }
            }
            REQUIRE(found >= 0);
            REQUIRE(views[0].cell[static_cast<std::size_t>(cell)] == found);
        }
    }
}

TEST_CASE("frontal_deg and nearest_face report the camera-facing face and its tilt", "[lattice]") {
    const Quat identity = {1.0F, 0.0F, 0.0F, 0.0F};
    REQUIRE(nearest_face(identity) == Face::F);
    REQUIRE(frontal_deg(identity) < 0.1F);

    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        CAPTURE(i);
        REQUIRE(nearest_face(kLattice24[i]) == static_cast<Face>(i / 4));
        REQUIRE(frontal_deg(kLattice24[i]) < 0.1F);
    }

    // 20-degree pitch: F still nearest, tilt reported directly.
    const Quat pitched = axis_angle({1.0, 0.0, 0.0}, 20.0);
    REQUIRE(nearest_face(pitched) == Face::F);
    REQUIRE(std::abs(frontal_deg(pitched) - 20.0F) < 0.1F);

    // 50-degree yaw about +y crosses the 45-degree boundary: L's normal
    // (at 90 - 50 = 40 degrees from the view axis) is now the most
    // frontal face, not F (at 50).
    const Quat yawed = axis_angle({0.0, 1.0, 0.0}, 50.0);
    REQUIRE(nearest_face(yawed) == Face::L);
    REQUIRE(std::abs(frontal_deg(yawed) - 40.0F) < 0.1F);
}

TEST_CASE("face_frontal_deg reports each body face's own tilt from the viewer", "[lattice]") {
    // At identity: F dead-on, its four neighbours edge-on, B facing away.
    const Quat identity = {1.0F, 0.0F, 0.0F, 0.0F};
    REQUIRE(face_frontal_deg(identity, Face::F) < 0.1F);
    REQUIRE(std::abs(face_frontal_deg(identity, Face::U) - 90.0F) < 0.1F);
    REQUIRE(std::abs(face_frontal_deg(identity, Face::R) - 90.0F) < 0.1F);
    REQUIRE(std::abs(face_frontal_deg(identity, Face::B) - 180.0F) < 0.1F);

    // Consistency with the aggregate helpers: the nearest face's own angle
    // IS frontal_deg, at every lattice element and under arbitrary tilt.
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> angle(0.0, 179.0);
    for (int i = 0; i < 200; ++i) {
        const Quat q = axis_angle({unit(rng), unit(rng), unit(rng) + 1.5}, angle(rng));
        const Face front = nearest_face(q);
        REQUIRE(std::abs(face_frontal_deg(q, front) - frontal_deg(q)) < 0.1F);
        for (std::size_t f = 0; f < 6; ++f) {
            REQUIRE(face_frontal_deg(q, static_cast<Face>(f)) >= frontal_deg(q) - 0.1F);
        }
    }
}

TEST_CASE("face-path cell routing is constant across in-plane re-key boundaries", "[lattice]") {
    // The M5 boundary-sweep property: as the cube turns IN-PLANE through a
    // full revolution, the detector re-keys its row-major labelling at
    // every 45-degree boundary, and the resolved element's obs_to_body map
    // must exactly cancel each re-key - every physical sticker keeps ONE
    // body cell index for the whole sweep. The harness is an independent
    // screen-space model of the detector: physical sticker p (solver
    // row-major on F at theta = 0) sits at display (x, y) =
    // (p % 3 - 1, 1 - p / 3); at cube angle theta the detector labels the
    // quad at the nearest quarter turn k = round(theta / 90) with residual
    // in-plane pose q_obs = Rz(theta - 90k), so observed cell o shows the
    // physical sticker at R(-90k)(x_o, y_o). theta = 0 anchors the map to
    // the identity, so constancy here IS correctness everywhere.
    const auto rz = [](float deg) {
        const float half = deg * std::numbers::pi_v<float> / 360.0F;
        return Quat{std::cos(half), 0.0F, 0.0F, std::sin(half)};
    };
    for (int theta = 0; theta < 360; ++theta) {
        CAPTURE(theta);
        const int nearest_quarter = (theta + 45) / 90;
        const int k = nearest_quarter % 4;
        const float phi = static_cast<float>(theta) - 90.0F * static_cast<float>(nearest_quarter);
        const auto res = resolve_alignment(rz(phi), rz(static_cast<float>(theta)));
        const FaceCellMap& map = kFaceObsToBody[static_cast<std::size_t>(res.lattice_index)];
        REQUIRE(map.body_face == Face::F);
        for (int o = 0; o < 9; ++o) {
            int x = o % 3 - 1;
            int y = 1 - o / 3;
            for (int turn = 0; turn < k; ++turn) {
                const int rotated_x = y;  // R(-90): (x, y) -> (y, -x)
                const int rotated_y = -x;
                x = rotated_x;
                y = rotated_y;
            }
            const int physical = 3 * (1 - y) + (x + 1);
            REQUIRE(map.cell[static_cast<std::size_t>(o)] == physical);
        }
    }
}

TEST_CASE("kBodyRelabel maps stickers between two body labellings", "[lattice]") {
    // The static_asserts in orientation_lattice.cpp already lock the
    // structural invariants (permutation, centre fixed, agreement with
    // kFacePermutation, identity is identity, and all 24 distinct per
    // source face). What they cannot check is that the table means what
    // epoch re-registration reads it as, so pin that here: one
    // hand-derived element, and a round-trip that no single-formula error
    // can survive.

    SECTION("one screen-clockwise quarter turn about the F axis") {
        // Element 4*Face::F + 1 - F stays the near face, the cube turns a
        // quarter clockwise as seen on screen. F's top-left sticker must
        // land top-right, top-right at bottom-right, and so on.
        constexpr int kQuarterAboutF = 4 * 2 + 1;
        const auto& map = kBodyRelabel[kQuarterAboutF][static_cast<std::size_t>(Face::F)];
        REQUIRE(map.body_face == Face::F);
        const std::array<int, 9> expected{2, 5, 8, 1, 4, 7, 0, 3, 6};
        for (std::size_t k = 0; k < expected.size(); ++k) {
            CHECK(map.cell[k] == expected[k]);
        }
    }

    SECTION("relabelling by an element and by its inverse is the identity") {
        // The face permutations are a faithful action, so the inverse
        // element is the unique one undoing g on all six faces. Composing
        // the two cell maps must return every sticker to its own index -
        // the property epoch registration relies on when it compares a
        // stored slot against a re-shown face under a candidate gauge.
        for (std::size_t g = 0; g < kLatticeSize; ++g) {
            std::optional<std::size_t> inverse;
            for (std::size_t h = 0; h < kLatticeSize; ++h) {
                bool undoes = true;
                for (std::size_t f = 0; f < 6; ++f) {
                    if (kFacePermutation[h][static_cast<std::size_t>(kFacePermutation[g][f])] !=
                        static_cast<Face>(f)) {
                        undoes = false;
                        break;
                    }
                }
                if (undoes) {
                    inverse = h;
                    break;
                }
            }
            REQUIRE(inverse.has_value());
            for (std::size_t f = 0; f < 6; ++f) {
                const auto& forward = kBodyRelabel[g][f];
                const auto& back =
                    kBodyRelabel[*inverse][static_cast<std::size_t>(forward.body_face)];
                REQUIRE(back.body_face == static_cast<Face>(f));
                for (std::size_t k = 0; k < 9; ++k) {
                    REQUIRE(back.cell[static_cast<std::size_t>(forward.cell[k])] ==
                            static_cast<int>(k));
                }
            }
        }
    }
}

TEST_CASE("kLatticeCompose folds two relabellings into one, stickers included", "[lattice]") {
    // THE property epoch bridging stands on. The router holds a gauge in the
    // dying epoch's body frame and the tracker hands it a relabelling from
    // the new epoch's frame into that one; bridging folds them with
    // kLatticeCompose and then routes the capture through the SINGLE folded
    // element. That is only correct if folding and applying-in-sequence are
    // the same operation all the way down to the nine stickers - the face
    // permutation agreeing is not enough, because relabel_capture() writes
    // cells, and a fold that got the face right and the roll wrong would
    // store a rotated lie in a correctly labelled slot (exactly the
    // corruption kBodyRelabel was put on the write path to end).
    //
    // The face-permutation half of this is static_asserted in
    // orientation_lattice.cpp; the CELL half is checked only here, and it is
    // the half the write path actually consumes.
    for (std::size_t a = 0; a < kLatticeSize; ++a) {
        for (std::size_t b = 0; b < kLatticeSize; ++b) {
            const auto folded = static_cast<std::size_t>(kLatticeCompose[a][b]);
            for (std::size_t f = 0; f < 6; ++f) {
                // Sequential: relabel f by b, then relabel the result by a.
                const auto& first = kBodyRelabel[b][f];
                const auto& second = kBodyRelabel[a][static_cast<std::size_t>(first.body_face)];
                // Folded: relabel f by the composite in one step.
                const auto& once = kBodyRelabel[folded][f];
                REQUIRE(once.body_face == second.body_face);
                for (std::size_t k = 0; k < 9; ++k) {
                    // Sticker at source cell k lands at first.cell[k] after
                    // b, and at second.cell[that] after a.
                    REQUIRE(once.cell[k] == second.cell[static_cast<std::size_t>(first.cell[k])]);
                }
            }
        }
    }
}

TEST_CASE("kLatticeInverse is the relabelling that undoes an element", "[lattice]") {
    // Bridging derives its relabelling from resolve_alignment, which returns
    // the element S with q_obs * S == the body orientation - so the
    // model->body gauge is S INVERSE, and a bridge that used S directly
    // would transpose the relabelling. This locks the table that makes the
    // direction explicit at the call site.
    for (std::size_t i = 0; i < kLatticeSize; ++i) {
        const auto inv = static_cast<std::size_t>(kLatticeInverse[i]);
        REQUIRE(kLatticeCompose[i][inv] == kIdentityLatticeIndex);
        REQUIRE(kLatticeCompose[inv][i] == kIdentityLatticeIndex);
        for (std::size_t f = 0; f < 6; ++f) {
            const auto& forward = kBodyRelabel[i][f];
            const auto& back = kBodyRelabel[inv][static_cast<std::size_t>(forward.body_face)];
            REQUIRE(back.body_face == static_cast<Face>(f));
            for (std::size_t k = 0; k < 9; ++k) {
                REQUIRE(back.cell[static_cast<std::size_t>(forward.cell[k])] ==
                        static_cast<int>(k));
            }
        }
    }
}
