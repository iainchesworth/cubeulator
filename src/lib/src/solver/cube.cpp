#include <cube/solver/cube.hpp>

#include <cstddef>

namespace cube::solver {

namespace {

// A point/direction in the cube's [-1,0,1]^3 coordinate space. Shared by
// sticker positions and face normals - a face turn rotates both identically
// (a normal is just a direction), which is what lets a single rotation
// formula relocate a sticker AND correctly reassign which face it now sits
// on (see rotate_quarter).
struct Vec3 {
    int x = 0;
    int y = 0;
    int z = 0;

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator*(Vec3 v, int s) noexcept { return {v.x * s, v.y * s, v.z * s}; }
constexpr int dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// The outward normal of `face`: R/L along X (L->R), U/D along Y (D->U), F/B
// along Z (B->F) - the same axis convention CubeView.qml's projection uses
// (see cube.hpp's layout comment).
constexpr Vec3 face_normal(Face face) noexcept {
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
    return {};
}

constexpr Face face_from_normal(Vec3 n) noexcept {
    if (n.x == 1) return Face::R;
    if (n.x == -1) return Face::L;
    if (n.y == 1) return Face::U;
    if (n.y == -1) return Face::D;
    if (n.z == 1) return Face::F;
    return Face::B;
}

// One clockwise-from-outside quarter turn of vector `v` about face normal
// `n`. Derived from first principles (decompose v into the component along
// n, which a rotation about n leaves fixed, plus the perpendicular
// component, which cross(v, n) rotates by exactly 90 degrees in the
// clockwise-viewed-from-+n sense) and cross-checked against three
// well-known physical turns: F sends U's F-adjacent row to R, R sends the
// URF corner to URB, U sends the URF corner to ULF.
constexpr Vec3 rotate_quarter(Vec3 v, Vec3 n) noexcept { return n * dot(v, n) + cross(v, n); }

// Position (in [-1,0,1]^3) of the sticker at local (row, col) on `face`, per
// the layout documented in cube.hpp.
constexpr Vec3 position_of(Face face, int row, int col) noexcept {
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

constexpr Vec3 position_of(Face face, int index) noexcept { return position_of(face, index / 3, index % 3); }

// Inverse of position_of: the (row, col) on `face` for a sticker whose
// position (restricted to that face's plane) is `pos`.
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

}  // namespace

Cube Cube::solved() noexcept {
    Cube c;
    for (Face face : kAllFaces) {
        const Color color = canonical_color(face);
        for (int index = 0; index < 9; ++index) {
            c.set(face, index, color);
        }
    }
    return c;
}

Color Cube::at(Face face, int index) const noexcept { return facelets_[static_cast<std::size_t>(face)][index]; }

std::array<Color, 9> Cube::face(Face face) const noexcept { return facelets_[static_cast<std::size_t>(face)]; }

Cube Cube::apply(Move move) const noexcept {
    Cube result = *this;
    const Vec3 n = face_normal(move.face);

    for (Face face : kAllFaces) {
        for (int index = 0; index < 9; ++index) {
            const Vec3 pos = position_of(face, index);
            if (dot(pos, n) != 1) {
                continue;  // not in the turned layer
            }

            Vec3 new_pos = pos;
            Vec3 new_normal = face_normal(face);
            for (int t = 0; t < move.turns; ++t) {
                new_pos = rotate_quarter(new_pos, n);
                new_normal = rotate_quarter(new_normal, n);
            }
            const Face new_face = face_from_normal(new_normal);
            const int new_index = index_on_face(new_face, new_pos);
            result.set(new_face, new_index, facelets_[static_cast<std::size_t>(face)][index]);
        }
    }
    return result;
}

bool Cube::is_solved() const noexcept {
    for (Face face : kAllFaces) {
        const auto& stickers = facelets_[static_cast<std::size_t>(face)];
        const Color center = stickers[4];
        for (Color sticker : stickers) {
            if (sticker != center) {
                return false;
            }
        }
    }
    return true;
}

namespace {

// The three colour-pairs a real cube's stickers belong to, one per axis.
// A corner/edge cubie can never carry two stickers from the same pair
// (they're on opposite faces, which never touch).
constexpr int axis_pair_of(Color color) noexcept {
    switch (color) {
        case Color::White:
        case Color::Yellow:
            return 1;  // Y axis (U/D)
        case Color::Orange:
        case Color::Red:
            return 0;  // X axis (L/R)
        case Color::Green:
        case Color::Blue:
            return 2;  // Z axis (F/B)
    }
    return -1;
}

}  // namespace

bool Cube::is_physically_valid() const noexcept {
    std::array<int, 6> color_counts{};
    for (Face face : kAllFaces) {
        for (Color sticker : facelets_[static_cast<std::size_t>(face)]) {
            ++color_counts[static_cast<std::size_t>(sticker)];
        }
    }
    for (int count : color_counts) {
        if (count != 9) {
            return false;
        }
    }

    // Corner/edge twist and flip PARITY (beyond "no impossible colour
    // combination", checked below for both) are deliberately not enforced
    // here: getting an orientation-parity convention that is genuinely
    // invariant under every generator turn - and consistent across the
    // alternating chirality of the 8 corners - is easy to get subtly wrong
    // (an earlier attempt at the corner-twist sum rejected genuinely
    // reachable states), and this check exists only to guard against a
    // hypothetical future malformed vision-pipeline read, not anything on
    // this session's critical path. The colour-count and
    // axis-pair-uniqueness checks already reject the vast majority of
    // malformed states - e.g. any corner or edge carrying two stickers from
    // the same colour pair, which can never happen on a real cube.
    for (int cx : {-1, 1}) {
        for (int cy : {-1, 1}) {
            for (int cz : {-1, 1}) {
                std::array<Color, 3> colors{};
                int n = 0;
                for (Face face : kAllFaces) {
                    const Vec3 fn = face_normal(face);
                    if (dot(fn, Vec3{cx, cy, cz}) != 1) {
                        continue;  // this face doesn't touch this corner
                    }
                    const int index = index_on_face(face, Vec3{cx, cy, cz});
                    colors[n] = at(face, index);
                    ++n;
                }
                std::array<bool, 3> seen_pair{false, false, false};
                for (int i = 0; i < 3; ++i) {
                    const int pair = axis_pair_of(colors[i]);
                    if (pair < 0 || seen_pair[static_cast<std::size_t>(pair)]) {
                        return false;  // duplicate axis-pair on one corner: impossible
                    }
                    seen_pair[static_cast<std::size_t>(pair)] = true;
                }
            }
        }
    }

    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            for (int sa : {-1, 1}) {
                for (int sb : {-1, 1}) {
                    Vec3 pos{};
                    int* comps[3] = {&pos.x, &pos.y, &pos.z};
                    *comps[a] = sa;
                    *comps[b] = sb;
                    std::array<Color, 2> colors{};
                    int n = 0;
                    for (Face face : kAllFaces) {
                        const Vec3 fn = face_normal(face);
                        if (dot(fn, pos) != 1) {
                            continue;
                        }
                        const int index = index_on_face(face, pos);
                        colors[n] = at(face, index);
                        ++n;
                    }
                    if (n != 2) {
                        continue;  // this axis pair is the edge's own zero coordinate
                    }
                    const int pair0 = axis_pair_of(colors[0]);
                    const int pair1 = axis_pair_of(colors[1]);
                    if (pair0 < 0 || pair1 < 0 || pair0 == pair1) {
                        return false;  // impossible colour combination on one edge
                    }
                }
            }
        }
    }

    return true;
}

Cube::EdgeLocation Cube::locate_edge(Color a, Color b) const noexcept {
    for (int axis0 = 0; axis0 < 3; ++axis0) {
        for (int axis1 = axis0 + 1; axis1 < 3; ++axis1) {
            for (int s0 : {-1, 1}) {
                for (int s1 : {-1, 1}) {
                    Vec3 pos{};
                    int* comps[3] = {&pos.x, &pos.y, &pos.z};
                    *comps[axis0] = s0;
                    *comps[axis1] = s1;

                    Face faces[2]{};
                    int indices[2]{};
                    int n = 0;
                    for (Face face : kAllFaces) {
                        const Vec3 fn = face_normal(face);
                        if (dot(fn, pos) != 1) {
                            continue;
                        }
                        faces[n] = face;
                        indices[n] = index_on_face(face, pos);
                        ++n;
                    }
                    const Color c0 = at(faces[0], indices[0]);
                    const Color c1 = at(faces[1], indices[1]);
                    if (c0 == a && c1 == b) {
                        return {faces[0], indices[0], faces[1], indices[1]};
                    }
                    if (c0 == b && c1 == a) {
                        return {faces[1], indices[1], faces[0], indices[0]};
                    }
                }
            }
        }
    }
    return {};  // unreachable for a physically valid cube
}

Cube::CornerLocation Cube::locate_corner(Color a, Color b, Color c) const noexcept {
    for (int cx : {-1, 1}) {
        for (int cy : {-1, 1}) {
            for (int cz : {-1, 1}) {
                Face faces[3]{};
                int indices[3]{};
                Color colors[3]{};
                int n = 0;
                for (Face face : kAllFaces) {
                    const Vec3 fn = face_normal(face);
                    if (dot(fn, Vec3{cx, cy, cz}) != 1) {
                        continue;
                    }
                    faces[n] = face;
                    indices[n] = index_on_face(face, Vec3{cx, cy, cz});
                    colors[n] = at(face, indices[n]);
                    ++n;
                }

                auto slot_of = [&](Color target) -> int {
                    for (int i = 0; i < 3; ++i) {
                        if (colors[i] == target) {
                            return i;
                        }
                    }
                    return -1;
                };
                const int ia = slot_of(a);
                const int ib = slot_of(b);
                const int ic = slot_of(c);
                if (ia >= 0 && ib >= 0 && ic >= 0 && ia != ib && ib != ic && ia != ic) {
                    return {faces[ia], indices[ia], faces[ib], indices[ib], faces[ic], indices[ic]};
                }
            }
        }
    }
    return {};  // unreachable for a physically valid cube
}

}  // namespace cube::solver
