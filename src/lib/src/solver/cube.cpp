#include <cube/solver/cube.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>

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
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept {
    return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}
constexpr Vec3 operator*(Vec3 v, int s) noexcept {
    return {.x = v.x * s, .y = v.y * s, .z = v.z * s};
}
constexpr int dot(Vec3 a, Vec3 b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {
        .x = (a.y * b.z) - (a.z * b.y),
        .y = (a.z * b.x) - (a.x * b.z),
        .z = (a.x * b.y) - (a.y * b.x),
    };
}

// The outward normal of `face`: R/L along X (L->R), U/D along Y (D->U), F/B
// along Z (B->F) - the same axis convention CubeView.qml's projection uses
// (see cube.hpp's layout comment).
constexpr Vec3 face_normal(Face face) noexcept {
    switch (face) {
        case Face::R:
            return {.x = 1, .y = 0, .z = 0};
        case Face::L:
            return {.x = -1, .y = 0, .z = 0};
        case Face::U:
            return {.x = 0, .y = 1, .z = 0};
        case Face::D:
            return {.x = 0, .y = -1, .z = 0};
        case Face::F:
            return {.x = 0, .y = 0, .z = 1};
        case Face::B:
            return {.x = 0, .y = 0, .z = -1};
    }
    return {};
}

constexpr Face face_from_normal(Vec3 n) noexcept {
    if (n.x == 1) {
        return Face::R;
    }
    if (n.x == -1) {
        return Face::L;
    }
    if (n.y == 1) {
        return Face::U;
    }
    if (n.y == -1) {
        return Face::D;
    }
    if (n.z == 1) {
        return Face::F;
    }
    return Face::B;
}

// One clockwise-from-outside quarter turn of vector `v` about face normal
// `n`. Derived from first principles (decompose v into the component along
// n, which a rotation about n leaves fixed, plus the perpendicular
// component, which cross(v, n) rotates by exactly 90 degrees in the
// clockwise-viewed-from-+n sense) and cross-checked against three
// well-known physical turns: F sends U's F-adjacent row to R, R sends the
// URF corner to URB, U sends the URF corner to ULF.
constexpr Vec3 rotate_quarter(Vec3 v, Vec3 n) noexcept {
    return n * dot(v, n) + cross(v, n);
}

// Position (in [-1,0,1]^3) of the sticker at local (row, col) on `face`, per
// the layout documented in cube.hpp.
constexpr Vec3 position_of(Face face, int row, int col) noexcept {
    switch (face) {
        case Face::U:
            return {.x = col - 1, .y = 1, .z = row - 1};
        case Face::D:
            return {.x = col - 1, .y = -1, .z = row - 1};
        case Face::R:
            return {.x = 1, .y = 1 - row, .z = col - 1};
        case Face::L:
            return {.x = -1, .y = 1 - row, .z = col - 1};
        case Face::F:
            return {.x = col - 1, .y = 1 - row, .z = 1};
        case Face::B:
            return {.x = col - 1, .y = 1 - row, .z = -1};
    }
    return {};
}

constexpr Vec3 position_of(Face face, int index) noexcept {
    return position_of(face, index / 3, index % 3);
}

// Inverse of position_of: the (row, col) on `face` for a sticker whose
// position (restricted to that face's plane) is `pos`.
constexpr int index_on_face(Face face, Vec3 pos) noexcept {
    switch (face) {
        case Face::U:
        case Face::D:
            return ((pos.z + 1) * 3) + (pos.x + 1);
        case Face::R:
        case Face::L:
            return ((1 - pos.y) * 3) + (pos.z + 1);
        case Face::F:
        case Face::B:
            return ((1 - pos.y) * 3) + (pos.x + 1);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Precomputed tables.
//
// The geometric derivation above stays the single source of truth, but
// re-running it per sticker per call made Cube::apply ~272ns and
// locate_edge ~125ns (measured, MSVC /O2) - and the solver's F2L searches
// execute those primitives millions of times per solve, which was the
// dominant cost of the whole pipeline once the last layer went
// algorithmic (see last_layer.hpp; solve() must stay interactive on a
// phone). Everything below is evaluated at compile time FROM the same
// constexpr geometry, so the tables cannot drift from the derivation, and
// the runtime functions become small table scans with byte-identical
// results.
// ---------------------------------------------------------------------------

struct StickerMove {
    Face dst_face;
    std::uint8_t dst_index;
    Face src_face;
    std::uint8_t src_index;
};

// A single-face turn (any number of quarter turns) moves exactly 20
// stickers: the 21 in the turned layer minus that face's own centre, which
// a rotation about its own normal fixes.
using MoveTable = std::array<StickerMove, 20>;

constexpr MoveTable make_move_table(Face turned, int turns) {
    MoveTable table{};
    std::size_t count = 0;
    const Vec3 n = face_normal(turned);
    for (Face face : kAllFaces) {
        for (int index = 0; index < 9; ++index) {
            const Vec3 pos = position_of(face, index);
            if (dot(pos, n) != 1) {
                continue;  // not in the turned layer
            }
            Vec3 new_pos = pos;
            Vec3 new_normal = face_normal(face);
            for (int t = 0; t < turns; ++t) {
                new_pos = rotate_quarter(new_pos, n);
                new_normal = rotate_quarter(new_normal, n);
            }
            const Face new_face = face_from_normal(new_normal);
            const int new_index = index_on_face(new_face, new_pos);
            if (new_face == face && new_index == index) {
                continue;  // the turned face's own centre
            }
            table.at(count++) = StickerMove{
                .dst_face = new_face,
                .dst_index = static_cast<std::uint8_t>(new_index),
                .src_face = face,
                .src_index = static_cast<std::uint8_t>(index),
            };
        }
    }
    if (count != table.size()) {
        // constexpr: a compile error if ever hit
        throw std::logic_error("a face turn must move exactly 20 stickers");
    }
    return table;
}

// [face][turns - 1] for turns in 1..3.
constexpr auto kMoveTables = [] {
    std::array<std::array<MoveTable, 3>, 6> tables{};
    for (Face face : kAllFaces) {
        for (int turns = 1; turns <= 3; ++turns) {
            tables.at(static_cast<std::size_t>(face)).at(static_cast<std::size_t>(turns - 1)) =
                make_move_table(face, turns);
        }
    }
    return tables;
}();

// The same tables seen from the stickers' side: facelet (face * 9 + index) to
// the facelet it moves to. Built from kMoveTables, so it cannot disagree with
// apply().
constexpr auto kFaceletMaps = [] {
    std::array<std::array<Cube::FaceletMap, 3>, 6> maps{};
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t turns = 0; turns < 3; ++turns) {
            Cube::FaceletMap& map = maps.at(face).at(turns);
            for (std::size_t i = 0; i < map.size(); ++i) {
                map.at(i) = static_cast<std::uint8_t>(i);
            }
            for (const StickerMove& sticker : kMoveTables.at(face).at(turns)) {
                map.at((static_cast<std::size_t>(sticker.src_face) * 9) + sticker.src_index) =
                    static_cast<std::uint8_t>((static_cast<std::size_t>(sticker.dst_face) * 9) +
                                              sticker.dst_index);
            }
        }
    }
    return maps;
}();

constexpr Cube::FaceletMap kIdentityFaceletMap = [] {
    Cube::FaceletMap map{};
    for (std::size_t i = 0; i < map.size(); ++i) {
        map.at(i) = static_cast<std::uint8_t>(i);
    }
    return map;
}();

struct EdgePosition {
    Face face_a;
    std::uint8_t index_a;
    Face face_b;
    std::uint8_t index_b;
};

struct CornerPosition {
    std::array<Face, 3> faces;
    std::array<std::uint8_t, 3> indices;
};

// The 12 edge and 8 corner positions, enumerated in EXACTLY the order the
// original locate_edge/locate_corner loops visited them (axis pairs then
// signs for edges; corner octants for corners; faces in kAllFaces order
// within each) - locate results must stay byte-identical, including which
// sticker lands in face_a vs face_b.
constexpr auto kEdgePositions = [] {
    std::array<EdgePosition, 12> positions{};
    std::size_t count = 0;
    for (int axis0 = 0; axis0 < 3; ++axis0) {
        for (int axis1 = axis0 + 1; axis1 < 3; ++axis1) {
            for (int s0 : {-1, 1}) {
                for (int s1 : {-1, 1}) {
                    Vec3 pos{};
                    const std::array<int*, 3> comps{&pos.x, &pos.y, &pos.z};
                    *comps.at(static_cast<std::size_t>(axis0)) = s0;
                    *comps.at(static_cast<std::size_t>(axis1)) = s1;
                    std::array<Face, 2> faces{};
                    std::array<std::uint8_t, 2> indices{};
                    std::size_t n = 0;
                    for (Face face : kAllFaces) {
                        if (dot(face_normal(face), pos) != 1) {
                            continue;
                        }
                        faces.at(n) = face;
                        indices.at(n) = static_cast<std::uint8_t>(index_on_face(face, pos));
                        ++n;
                    }
                    positions.at(count++) = EdgePosition{
                        .face_a = faces.at(0),
                        .index_a = indices.at(0),
                        .face_b = faces.at(1),
                        .index_b = indices.at(1),
                    };
                }
            }
        }
    }
    return positions;
}();

constexpr auto kCornerPositions = [] {
    std::array<CornerPosition, 8> positions{};
    std::size_t count = 0;
    for (int cx : {-1, 1}) {
        for (int cy : {-1, 1}) {
            for (int cz : {-1, 1}) {
                CornerPosition corner{};
                int n = 0;
                for (Face face : kAllFaces) {
                    if (dot(face_normal(face), Vec3{.x = cx, .y = cy, .z = cz}) != 1) {
                        continue;
                    }
                    corner.faces.at(static_cast<std::size_t>(n)) = face;
                    corner.indices.at(static_cast<std::size_t>(n)) = static_cast<std::uint8_t>(
                        index_on_face(face, Vec3{.x = cx, .y = cy, .z = cz}));
                    ++n;
                }
                positions.at(count++) = corner;
            }
        }
    }
    return positions;
}();

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

Result<Cube> Cube::from_facelets(const std::array<std::array<Color, 9>, 6>& facelets) noexcept {
    Cube c;
    c.facelets_ = facelets;
    if (!c.is_physically_valid()) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return c;
}

Color Cube::at(Face face, int index) const noexcept {
    return facelets_.at(static_cast<std::size_t>(face)).at(static_cast<std::size_t>(index));
}

std::array<Color, 9> Cube::face(Face face) const noexcept {
    return facelets_.at(static_cast<std::size_t>(face));
}

Cube Cube::apply(Move move) const noexcept {
    Cube result = *this;
    // Move documents turns in 1..3 and Face's six enumerators, and every
    // constructor in the codebase honours both; guard anyway so a malformed
    // value (e.g. a hypothetical future vision-pipeline read of a raw byte,
    // the same class of input is_physically_valid guards against) degrades
    // to a no-op copy - as the old geometric implementation did - instead
    // of indexing out of the tables.
    if (move.turns < 1 || move.turns > 3 ||
        static_cast<std::size_t>(move.face) >= kMoveTables.size()) {
        return result;
    }
    for (const StickerMove& sticker : kMoveTables.at(static_cast<std::size_t>(move.face))
                                          .at(static_cast<std::size_t>(move.turns - 1))) {
        result.set(sticker.dst_face, sticker.dst_index, at(sticker.src_face, sticker.src_index));
    }
    return result;
}

const Cube::FaceletMap& Cube::facelet_map(Move move) noexcept {
    if (move.turns < 1 || move.turns > 3 ||
        static_cast<std::size_t>(move.face) >= kFaceletMaps.size()) {
        return kIdentityFaceletMap;
    }
    return kFaceletMaps.at(static_cast<std::size_t>(move.face))
        .at(static_cast<std::size_t>(move.turns - 1));
}

bool Cube::is_solved() const noexcept {
    for (Face face : kAllFaces) {
        const auto& stickers = facelets_.at(static_cast<std::size_t>(face));
        const Color center = stickers.at(4);
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

namespace {

bool colour_counts_are_nine(const Cube& cube) noexcept {
    std::array<int, 6> color_counts{};
    for (Face face : kAllFaces) {
        for (int index = 0; index < 9; ++index) {
            ++color_counts.at(static_cast<std::size_t>(cube.at(face, index)));
        }
    }
    return std::ranges::all_of(color_counts, [](int count) { return count == 9; });
}

// The edge position holding the two non-zero coordinates: `sign_a` on `axis_a`
// and `sign_b` on `axis_b`, zero on the third axis.
struct EdgeSpec {
    std::array<int, 2> axes{};   // the two non-zero axes, lower first
    std::array<int, 2> signs{};  // their signs, +1 or -1
};

constexpr Vec3 edge_position(const EdgeSpec& spec) noexcept {
    Vec3 pos{};
    const std::array<int*, 3> comps{&pos.x, &pos.y, &pos.z};
    *comps.at(static_cast<std::size_t>(spec.axes.at(0))) = spec.signs.at(0);
    *comps.at(static_cast<std::size_t>(spec.axes.at(1))) = spec.signs.at(1);
    return pos;
}

// The eight corner diagonals (every coordinate +-1).
constexpr std::array<Vec3, 8> corner_diagonals() noexcept {
    std::array<Vec3, 8> out{};
    std::size_t n = 0;
    for (int cx : {-1, 1}) {
        for (int cy : {-1, 1}) {
            for (int cz : {-1, 1}) {
                out.at(n++) = Vec3{.x = cx, .y = cy, .z = cz};
            }
        }
    }
    return out;
}

// The twelve edge positions (exactly one coordinate 0).
constexpr std::array<Vec3, 12> edge_midpoints() noexcept {
    std::array<Vec3, 12> out{};
    std::size_t n = 0;
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            for (int sa : {-1, 1}) {
                for (int sb : {-1, 1}) {
                    out.at(n++) = edge_position({.axes = {a, b}, .signs = {sa, sb}});
                }
            }
        }
    }
    return out;
}

// The colours on the facets touching a cubie, in Face order, and how many.
template <std::size_t N>
struct Facets {
    std::array<Color, N> colors{};
    std::size_t count = 0;
};

template <std::size_t N>
Facets<N> facets_at(const Cube& cube, Vec3 pos) noexcept {
    Facets<N> out;
    for (Face face : kAllFaces) {
        if (dot(face_normal(face), pos) != 1) {
            continue;  // this face doesn't touch this cubie
        }
        out.colors.at(out.count++) = cube.at(face, index_on_face(face, pos));
    }
    return out;
}

// No two stickers of one cubie share an axis pair, and none is a colour the
// scheme does not name: `colors` are one per facet.
template <std::size_t N>
bool axis_pairs_are_distinct(const std::array<Color, N>& colors) noexcept {
    std::array<bool, 3> seen_pair{false, false, false};
    for (const Color color : colors) {
        const int pair = axis_pair_of(color);
        if (pair < 0 || seen_pair.at(static_cast<std::size_t>(pair))) {
            return false;
        }
        seen_pair.at(static_cast<std::size_t>(pair)) = true;
    }
    return true;
}

// No corner carries two stickers of one colour pair (axis-pair uniqueness).
bool corners_have_distinct_axis_pairs(const Cube& cube) noexcept {
    return std::ranges::all_of(corner_diagonals(), [&cube](Vec3 pos) {
        return axis_pairs_are_distinct(facets_at<3>(cube, pos).colors);
    });
}

// No edge carries two stickers of one axis pair, or one the scheme does not
// name.
bool edges_have_distinct_axis_pairs(const Cube& cube) noexcept {
    return std::ranges::all_of(edge_midpoints(), [&cube](Vec3 pos) {
        const Facets<2> facets = facets_at<2>(cube, pos);
        if (facets.count != 2) {
            return true;  // this axis pair is the edge's own zero coordinate
        }
        return axis_pairs_are_distinct(facets.colors);
    });
}

}  // namespace

bool Cube::is_physically_valid() const noexcept {
    if (!colour_counts_are_nine(*this)) {
        return false;
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
    return corners_have_distinct_axis_pairs(*this) && edges_have_distinct_axis_pairs(*this);
}

namespace {

// Inverse of canonical_color: the face a colour belongs to under the fixed
// centre scheme. Only meaningful once the caller has verified the centres
// actually follow that scheme - is_solvable() checks this before anything
// else for precisely that reason.
constexpr Face canonical_face_of(Color color) noexcept {
    for (Face face : kAllFaces) {
        if (canonical_color(face) == color) {
            return face;
        }
    }
    return Face::U;  // unreachable: canonical_color covers all six colours
}

// Dense 0..7 index for a corner position (all coordinates in {-1, 1}).
constexpr int corner_slot(Vec3 pos) noexcept {
    return (((pos.x + 1) / 2) * 4) + (((pos.y + 1) / 2) * 2) + ((pos.z + 1) / 2);
}

// Dense 0..11 index for an edge position (exactly one coordinate 0),
// grouped by which axis holds the zero.
constexpr int edge_slot(Vec3 pos) noexcept {
    if (pos.x == 0) {
        return (((pos.y + 1) / 2) * 2) + ((pos.z + 1) / 2);
    }
    if (pos.y == 0) {
        return 4 + (((pos.x + 1) / 2) * 2) + ((pos.z + 1) / 2);
    }
    return 8 + (((pos.x + 1) / 2) * 2) + ((pos.y + 1) / 2);
}

// Parity (0 = even, 1 = odd) of a permutation given as slot -> home-slot,
// by cycle decomposition: a k-cycle equals k-1 transpositions, so the
// parity is the XOR of (k - 1) over all cycles.
template <std::size_t N>
constexpr int permutation_parity(const std::array<int, N>& perm) noexcept {
    std::array<bool, N> visited{};
    int parity = 0;
    for (std::size_t i = 0; i < N; ++i) {
        int length = 0;
        for (std::size_t j = i; !visited.at(j); j = static_cast<std::size_t>(perm.at(j))) {
            visited.at(j) = true;
            ++length;
        }
        if (length != 0) {
            parity = (parity + (length - 1)) % 2;  // a k-cycle is k - 1 transpositions
        }
    }
    return parity;
}

}  // namespace

namespace {

// Piece identity below is looked up through canonical_color's fixed centre
// scheme, so that scheme must actually hold. A physically valid cube always
// has one centre of each colour but possibly permuted vs canonical - e.g. a
// free-form scan whose six faces were assembled into the wrong slots.
// Centres never move under face turns, so such a state is unreachable from
// solved() and is rightly "not solvable" - exactly the rejection the
// free-form assembly search relies on.
bool centres_are_canonical(const Cube& cube) noexcept {
    return std::ranges::all_of(
        kAllFaces, [&cube](Face face) { return cube.at(face, 4) == canonical_color(face); });
}

// The orientation sum and the slot -> home-slot permutation of a set of
// cubies; nullopt when one cubie appears twice (no real cube has that).
template <std::size_t N>
struct PieceCensus {
    int orientation_sum = 0;
    std::array<int, N> permutation{};
};

// --- Corners: twist sum and permutation ------------------------------------
//
// Twist convention: the corner at diagonal d (all coordinates +-1) has its
// three stickers on the facets whose outward normals are the three axis
// components of d. Its twist is how many CLOCKWISE 120-degree steps - viewed
// from outside, i.e. looking back along d at the corner - separate the facet
// carrying the cubie's White/Yellow sticker from the U/D (y-axis) facet: 0
// when it is already there. "Facet m is one clockwise step from facet n" is
// detected by the sign of the scalar triple product dot(cross(n, m), d):
// positive means n -> m is a counter-clockwise step about d by the
// right-hand rule, negative means clockwise. Cross-checked by hand at corner
// (1, 1, 1): the 120-degree counter-clockwise rotation about (1,1,1) is the
// cyclic axis map x -> y -> z (its matrix is the cyclic permutation, and the
// initial velocity cross((1,1,1), e_x) = (0,1,-1) does point from e_x
// towards e_y), so U's clockwise neighbour there is R - and indeed
// dot(cross(e_y, e_x), (1,1,1)) = dot(-e_z, (1,1,1)) = -1 < 0.
//
// Invariance: U/D turns keep every White/Yellow sticker on a y facet and
// rotate the other two facets with the piece, preserving each corner's twist
// individually; each of the other faces' quarter turns changes the four
// moved corners' twists by amounts summing to 0 mod 3 (hand-checked for F on
// solved(): URF/ULF/DLF/DRF pick up 2+1+2+1 = 6). So the sum mod 3 is
// invariant, and it is 0 on solved() - the first classical invariant.
// Axis-pair uniqueness (already enforced by is_physically_valid) guarantees
// exactly one White/Yellow sticker per corner, so the lookup is total.
// What the corner at `pos` shows: the facet holding White/Yellow, and the sum
// of its three colours' canonical-face normals.
struct CornerReading {
    Vec3 ud_facet{};  // outward normal of the facet holding White/Yellow
    Vec3 home{};      // sum of the three colours' canonical-face normals
};

CornerReading read_corner(const Cube& cube, Vec3 pos) noexcept {
    CornerReading reading;
    for (Face face : kAllFaces) {
        const Vec3 fn = face_normal(face);
        if (dot(fn, pos) != 1) {
            continue;  // this face doesn't touch this corner
        }
        const Color color = cube.at(face, index_on_face(face, pos));
        reading.home = reading.home + face_normal(canonical_face_of(color));
        if (axis_pair_of(color) == 1) {
            reading.ud_facet = fn;
        }
    }
    return reading;
}

std::optional<PieceCensus<8>> corner_census(const Cube& cube) noexcept {
    PieceCensus<8> census;
    std::array<bool, 8> home_seen{};
    for (const Vec3 pos : corner_diagonals()) {
        const CornerReading reading = read_corner(cube, pos);
        if (reading.ud_facet.y == 0) {  // not already on the U/D facet
            census.orientation_sum +=
                dot(cross(Vec3{.x = 0, .y = pos.y, .z = 0}, reading.ud_facet), pos) < 0 ? 1 : 2;
        }

        // The three canonical normals are mutually orthogonal (axis-pair
        // uniqueness again), so their sum is exactly the corner position
        // this cubie occupies on a solved cube: its home slot.
        const int target = corner_slot(reading.home);
        if (home_seen.at(static_cast<std::size_t>(target))) {
            return std::nullopt;  // the same cubie twice: no real cube has that
        }
        home_seen.at(static_cast<std::size_t>(target)) = true;
        census.permutation.at(static_cast<std::size_t>(corner_slot(pos))) = target;
    }
    return census;
}

// --- Edges: flip sum and permutation ---------------------------------------
//
// Flip convention (the classic ZZ/EO definition, phrased for this
// representation): an edge's "primary" facet is its U/D (y-axis) facet if it
// has one, otherwise its F/B (z-axis) facet (the E-layer case). The edge is
// unflipped iff the primary facet shows a White/Yellow sticker, or shows a
// Green/Blue sticker while the other facet shows Orange/Red - the usual
// "placeable without F/B quarter turns" rule. Invariance was hand-checked
// against one representative edge per interesting move class: F sends good
// UF to bad (Green primary, Yellow secondary at FR) and good FR to bad
// (Orange primary at DF); B sends good UB to bad; L preserves good UL
// (Yellow lands on FL's primary z facet) and good FL (Green lands on DL's
// primary y facet with Red secondary); R preserves good UR; F2 trivially
// preserves (stickers return to same-axis facets). So U/D/L/R turns and
// F2/B2 preserve every edge's flip while F/B quarter turns flip exactly the
// four edges they move: the sum mod 2 is invariant and 0 on solved() - the
// second classical invariant.
struct EdgeReading {
    Color primary{};
    Color secondary{};
    Vec3 home{};  // sum of the two colours' canonical-face normals
};

EdgeReading read_edge(const Cube& cube, Vec3 pos) noexcept {
    EdgeReading reading;
    for (Face face : kAllFaces) {
        const Vec3 fn = face_normal(face);
        if (dot(fn, pos) != 1) {
            continue;
        }
        const Color color = cube.at(face, index_on_face(face, pos));
        reading.home = reading.home + face_normal(canonical_face_of(color));
        const bool is_primary = (pos.y != 0) ? (fn.y != 0) : (fn.z != 0);
        (is_primary ? reading.primary : reading.secondary) = color;
    }
    return reading;
}

std::optional<PieceCensus<12>> edge_census(const Cube& cube) noexcept {
    PieceCensus<12> census;
    std::array<bool, 12> home_seen{};
    for (const Vec3 pos : edge_midpoints()) {
        const EdgeReading reading = read_edge(cube, pos);
        if (axis_pair_of(reading.primary) != 1 && axis_pair_of(reading.secondary) != 0) {
            ++census.orientation_sum;
        }

        // As for corners: the two canonical normals lie on different axes,
        // so their sum is the cubie's solved position.
        const int target = edge_slot(reading.home);
        if (home_seen.at(static_cast<std::size_t>(target))) {
            return std::nullopt;  // duplicated edge cubie
        }
        home_seen.at(static_cast<std::size_t>(target)) = true;
        census.permutation.at(static_cast<std::size_t>(edge_slot(pos))) = target;
    }
    return census;
}

}  // namespace

bool Cube::is_solvable() const noexcept {
    if (!centres_are_canonical(*this) || !is_physically_valid()) {
        return false;
    }

    const std::optional<PieceCensus<8>> corners = corner_census(*this);
    if (!corners || corners->orientation_sum % 3 != 0) {
        return false;
    }
    const std::optional<PieceCensus<12>> edges = edge_census(*this);
    if (!edges || edges->orientation_sum % 2 != 0) {
        return false;
    }

    // --- Permutation parity ------------------------------------------------
    //
    // Every quarter turn 4-cycles four corners and 4-cycles four edges, and
    // a 4-cycle is an odd permutation - so the corner and edge permutation
    // parities always flip together and their EQUALITY is invariant (both
    // are the even identity on solved()). The third classical invariant,
    // and with the previous two it is sufficient: the duplicate checks above
    // ensured all 8 corner colour-triples and all 12 edge colour-pairs are
    // distinct, and since a valid triple/pair draws at most one colour per
    // axis-pair there are exactly 8 and 12 possible ones - every real cubie
    // is present exactly once, which is when the classical theorem applies.
    return permutation_parity(corners->permutation) == permutation_parity(edges->permutation);
}

Cube::EdgeLocation Cube::locate_edge(Color a, Color b) const noexcept {
    for (const EdgePosition& position : kEdgePositions) {
        const Color c0 = at(position.face_a, position.index_a);
        const Color c1 = at(position.face_b, position.index_b);
        if (c0 == a && c1 == b) {
            return {
                .face_a = position.face_a,
                .index_a = position.index_a,
                .face_b = position.face_b,
                .index_b = position.index_b,
            };
        }
        if (c0 == b && c1 == a) {
            return {
                .face_a = position.face_b,
                .index_a = position.index_b,
                .face_b = position.face_a,
                .index_b = position.index_a,
            };
        }
    }
    return {};  // unreachable for a physically valid cube
}

Cube::CornerLocation Cube::locate_corner(Color a, Color b, Color c) const noexcept {
    for (const CornerPosition& position : kCornerPositions) {
        const std::array<Color, 3> colors = {
            at(position.faces.at(0), position.indices.at(0)),
            at(position.faces.at(1), position.indices.at(1)),
            at(position.faces.at(2), position.indices.at(2)),
        };
        auto slot_of = [&](Color target) -> int {
            for (int i = 0; i < 3; ++i) {
                if (colors.at(static_cast<std::size_t>(i)) == target) {
                    return i;
                }
            }
            return -1;
        };
        const int ia = slot_of(a);
        const int ib = slot_of(b);
        const int ic = slot_of(c);
        if (ia >= 0 && ib >= 0 && ic >= 0 && ia != ib && ib != ic && ia != ic) {
            const auto fa = static_cast<std::size_t>(ia);
            const auto fb = static_cast<std::size_t>(ib);
            const auto fc = static_cast<std::size_t>(ic);
            return {
                .face_a = position.faces.at(fa),
                .index_a = position.indices.at(fa),
                .face_b = position.faces.at(fb),
                .index_b = position.indices.at(fb),
                .face_c = position.faces.at(fc),
                .index_c = position.indices.at(fc),
            };
        }
    }
    return {};  // unreachable for a physically valid cube
}

}  // namespace cube::solver
