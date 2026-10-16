#include <cube/nxn/validate.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>

namespace cube::nxn {

namespace {

constexpr std::size_t kColours = 6;
constexpr std::size_t kCorners = 8;
constexpr std::size_t kEdges = Geometry::kEdgeLocations;
// The most centre rings a supported size has (a 9x9's seven-by-seven block of
// centres, less its middle, in fours); room to spare.
constexpr std::size_t kMostCentreRings = 16;
// The wings of a ring: one per place a wing can be, in 12 edges.
constexpr std::size_t kWingsPerRing = 24;

[[nodiscard]] constexpr std::size_t slot_of(Color colour) noexcept {
    return static_cast<std::size_t>(colour);
}

// Which axis a cube's faces and colours are ranked on when an orientation has
// to be told: the up-down axis first, then front-back, then left-right. That
// is the convention under which a face turn of U, D, L or R never flips an
// edge and a turn of F or B always does, so "flipped" means what it means
// everywhere else in cubing.
[[nodiscard]] constexpr int axis_rank(Axis axis) noexcept {
    switch (axis) {
        case Axis::Y:
            return 0;
        case Axis::Z:
            return 1;
        case Axis::X:
            return 2;
    }
    return 2;
}

// The rank of the colour a face wears in the canonical scheme.
inline constexpr std::array<int, kColours> kColourRank = [] {
    std::array<int, kColours> rank{};
    for (const Face face : kAllFaces) {
        rank.at(slot_of(solver::canonical_color(face))) = axis_rank(axis_of(face));
    }
    return rank;
}();

// The sticker at `index`. The caller has checked the span is a cube's.
[[nodiscard]] Color colour_at(std::span<const Color> facelets, std::size_t index) noexcept {
    assert(index < facelets.size());
    return *std::next(facelets.begin(), static_cast<std::ptrdiff_t>(index));
}

// What is known about a size's pieces, independent of any cube: where each
// piece's stickers are, and which piece each coloured piece in a slot is.
// Built once per size from the geometry and the canonical scheme.
struct Plan {
    // A corner's stickers in clockwise order as seen from outside, starting at
    // the one on the up-down axis. Chirality is decided here, once: a corner
    // read in this order shows the same cyclic sequence of colours wherever
    // its piece has been carried.
    std::array<std::array<std::uint16_t, 3>, kCorners> corner_stickers{};
    // [(y * 6 + a) * 6 + b] is the corner whose up-down colour is y, followed
    // clockwise by a and b, or -1 when no corner is.
    std::array<std::int8_t, kColours * kColours * kColours> corner_home{};

    struct Wing {
        std::uint16_t primary = 0;
        std::uint16_t secondary = 0;
    };
    // Per wing ring, its 24 slots, and which slot's wing shows the colours
    // [primary * 6 + secondary] (-1 for none).
    std::vector<std::vector<Wing>> wings;
    std::vector<std::array<std::int8_t, kColours * kColours>> wing_home;

    struct Edge {
        std::uint16_t first = 0;
        std::uint16_t second = 0;
        // Whether the first sticker is the one an oriented piece shows its
        // higher-ranked colour on.
        bool first_leads = true;
    };
    // The middle edges of an odd size, by location, and which location's
    // piece shows the colours [first * 6 + second] in either order.
    std::array<Edge, kEdges> edges{};
    std::array<std::int8_t, kColours * kColours> edge_home{};

    struct Centre {
        std::uint16_t facelet = 0;
        std::uint8_t ring = 0;
    };
    std::vector<Centre> centres;
    // How often each colour appears in each ring of centre stickers.
    std::vector<std::uint8_t> ring_share;
};

[[nodiscard]] std::size_t canonical_at(const Geometry& geometry, std::uint16_t facelet) noexcept {
    return slot_of(solver::canonical_color(geometry.face_of(facelet)));
}

[[nodiscard]] std::int8_t as_index(std::size_t value) noexcept {
    return static_cast<std::int8_t>(value);
}

void plan_corners(const Geometry& geometry, Plan& plan) {
    plan.corner_home.fill(-1);
    const auto& corners = geometry.corners();
    assert(corners.size() == kCorners);
    for (std::size_t slot = 0; slot < corners.size(); ++slot) {
        // The geometry lists a corner's stickers by facelet number, which is by
        // face: up or down, then left or right, then front or back. Looked at
        // from outside, those three go one way round when the corner's three
        // faces make a right-handed frame, and the other way when they do not.
        const auto& [vertical, side, depth] = corners.at(slot);
        int handedness = 1;
        for (const std::uint16_t facelet : corners.at(slot)) {
            handedness *= is_positive(geometry.face_of(facelet)) ? 1 : -1;
        }
        const std::array<std::uint16_t, 3> clockwise =
            handedness > 0 ? std::array<std::uint16_t, 3>{vertical, side, depth}
                           : std::array<std::uint16_t, 3>{vertical, depth, side};
        plan.corner_stickers.at(slot) = clockwise;
        const std::size_t key = (((canonical_at(geometry, clockwise.at(0)) * kColours) +
                                  canonical_at(geometry, clockwise.at(1))) *
                                 kColours) +
                                canonical_at(geometry, clockwise.at(2));
        plan.corner_home.at(key) = as_index(slot);
    }
}

void plan_wings(const Geometry& geometry, Plan& plan) {
    for (const std::vector<WingSlot>& orbit : geometry.wing_orbits()) {
        assert(orbit.size() == kWingsPerRing);
        std::vector<Plan::Wing>& slots = plan.wings.emplace_back();
        slots.reserve(kWingsPerRing);
        std::array<std::int8_t, kColours * kColours>& home = plan.wing_home.emplace_back();
        home.fill(-1);
        for (std::size_t slot = 0; slot < orbit.size(); ++slot) {
            const WingSlot& wing = orbit.at(slot);
            slots.push_back(Plan::Wing{.primary = wing.primary, .secondary = wing.secondary});
            home.at((canonical_at(geometry, wing.primary) * kColours) +
                    canonical_at(geometry, wing.secondary)) = as_index(slot);
        }
    }
}

void plan_edges(const Geometry& geometry, Plan& plan) {
    plan.edge_home.fill(-1);
    if (geometry.size() % 2 == 0) {
        return;
    }
    for (std::size_t location = 0; location < kEdges; ++location) {
        const std::array<std::uint16_t, 2>& stickers = geometry.mid_edge(location);
        const int first_rank = axis_rank(axis_of(geometry.face_of(stickers.at(0))));
        const int second_rank = axis_rank(axis_of(geometry.face_of(stickers.at(1))));
        plan.edges.at(location) = Plan::Edge{
            .first = stickers.at(0),
            .second = stickers.at(1),
            .first_leads = first_rank < second_rank,
        };
        const std::size_t one = canonical_at(geometry, stickers.at(0));
        const std::size_t other = canonical_at(geometry, stickers.at(1));
        plan.edge_home.at((one * kColours) + other) = as_index(location);
        plan.edge_home.at((other * kColours) + one) = as_index(location);
    }
}

void plan_centres(const Geometry& geometry, Plan& plan) {
    std::uint8_t ring = 0;
    for (const int orbit : geometry.centre_orbits()) {
        const std::vector<std::uint16_t>& facelets = geometry.orbit_facelets(orbit);
        for (const std::uint16_t facelet : facelets) {
            plan.centres.push_back(Plan::Centre{.facelet = facelet, .ring = ring});
        }
        plan.ring_share.push_back(static_cast<std::uint8_t>(facelets.size() / kColours));
        ++ring;
    }
    assert(plan.ring_share.size() <= kMostCentreRings);
}

[[nodiscard]] Plan build_plan(int size) {
    const Geometry& geometry = Geometry::of(size);
    Plan plan;
    plan_corners(geometry, plan);
    plan_wings(geometry, plan);
    plan_edges(geometry, plan);
    plan_centres(geometry, plan);
    return plan;
}

[[nodiscard]] const Plan& plan_of(int size) {
    static std::array<std::once_flag, static_cast<std::size_t>(kMaxSize) + 1> built;
    static std::array<std::unique_ptr<const Plan>, static_cast<std::size_t>(kMaxSize) + 1> cache;
    const auto slot = static_cast<std::size_t>(size);
    std::call_once(built.at(slot), [slot, size] {
        cache.at(slot) = std::make_unique<const Plan>(build_plan(size));
    });
    return *cache.at(slot);
}

[[nodiscard]] constexpr Verdict fault_of(Fault fault) noexcept {
    return Verdict{.fault = fault};
}

[[nodiscard]] constexpr Verdict fault_at(Fault fault, Role piece, std::uint16_t facelet) noexcept {
    return Verdict{.fault = fault, .piece = piece, .facelet = facelet};
}

// Whether `piece_at` (which piece is in each of the first `count` slots, a
// permutation of 0..count-1) is an odd permutation.
template <std::size_t N>
[[nodiscard]] bool is_odd(const std::array<std::uint8_t, N>& piece_at, std::size_t count) noexcept {
    static_assert(N <= 32);
    std::uint32_t visited = 0;
    std::size_t transpositions = 0;
    for (std::size_t start = 0; start < count; ++start) {
        if (((visited >> start) & 1U) != 0) {
            continue;
        }
        std::size_t length = 0;
        for (std::size_t at = start; ((visited >> at) & 1U) == 0; at = piece_at.at(at)) {
            visited |= std::uint32_t{1} << at;
            ++length;
        }
        transpositions += length - 1;
    }
    return transpositions % 2 == 1;
}

// Which pieces of one kind have been seen, so that each is seen once.
class Roll {
public:
    // Records `piece` as seen. False if it already was.
    [[nodiscard]] bool take(std::uint8_t piece) noexcept {
        const std::uint32_t bit = std::uint32_t{1} << piece;
        const bool fresh = (seen_ & bit) == 0;
        seen_ |= bit;
        return fresh;
    }

private:
    std::uint32_t seen_ = 0;
};

// The stickers of a cube read against the canonical scheme: every colour is
// named for the face it makes in the scheme this cube has.
class Reader {
public:
    Reader(std::span<const Color> facelets, const std::array<Color, kColours>& scheme)
        : facelets_(facelets) {
        for (const Face face : kAllFaces) {
            renamed_.at(slot_of(scheme.at(static_cast<std::size_t>(face)))) =
                solver::canonical_color(face);
        }
    }

    // The canonical colour of the sticker at `facelet`.
    [[nodiscard]] std::size_t colour(std::uint16_t facelet) const noexcept {
        return slot_of(renamed_.at(slot_of(colour_at(facelets_, facelet))));
    }

private:
    std::span<const Color> facelets_;
    std::array<Color, kColours> renamed_{};
};

// What reading one kind of piece found, for the checks that wait until every
// kind has been identified.
struct Corners {
    std::array<std::uint8_t, kCorners> piece_at{};
    int twist = 0;
    std::uint16_t first_twisted = 0;
    bool any_twisted = false;
};

struct Edges {
    std::array<std::uint8_t, kEdges> piece_at{};
    int flips = 0;
    std::uint16_t first_flipped = 0;
};

// Every colour as often as a face has stickers, and every sticker a colour.
[[nodiscard]] Verdict check_stickers(const Geometry& geometry, std::span<const Color> facelets) {
    if (facelets.size() != geometry.facelet_count() ||
        std::ranges::any_of(facelets, [](Color c) { return slot_of(c) >= kColours; })) {
        return fault_of(Fault::NotACube);
    }
    std::array<std::size_t, kColours> counts{};
    for (const Color colour : facelets) {
        ++counts.at(slot_of(colour));
    }
    const auto is_a_face_full = [&geometry](std::size_t n) { return n == geometry.face_area(); };
    return std::ranges::all_of(counts, is_a_face_full) ? Verdict{} : fault_of(Fault::ColourCounts);
}

// Centres have no identity: a ring is right when it holds every colour the
// same number of times, and the pieces within it can be in any order.
[[nodiscard]] Verdict check_centres(const Plan& plan, std::span<const Color> facelets) {
    std::array<std::array<std::uint8_t, kColours>, kMostCentreRings> tally{};
    for (const Plan::Centre& centre : plan.centres) {
        ++tally.at(centre.ring).at(slot_of(colour_at(facelets, centre.facelet)));
    }
    for (const Plan::Centre& centre : plan.centres) {
        const std::uint8_t seen =
            tally.at(centre.ring).at(slot_of(colour_at(facelets, centre.facelet)));
        if (seen > plan.ring_share.at(centre.ring)) {
            return fault_at(Fault::CentreColours, Role::Centre, centre.facelet);
        }
    }
    return Verdict{};
}

// The place, in a corner's clockwise order, of the sticker that shows an
// up-down colour: how far the corner is turned from its home. A real corner has
// exactly one such sticker; anything else is kNotACorner.
constexpr std::size_t kNotACorner = 3;

[[nodiscard]] std::size_t turn_of(const std::array<std::size_t, 3>& shown) noexcept {
    std::size_t turned = kNotACorner;
    for (std::size_t i = 0; i < shown.size(); ++i) {
        if (kColourRank.at(shown.at(i)) != 0) {
            continue;
        }
        if (turned != kNotACorner) {
            return kNotACorner;
        }
        turned = i;
    }
    return turned;
}

// Corners: each of the eight, once, and how far each is turned.
[[nodiscard]] Verdict read_corners(const Plan& plan, const Reader& reader, Corners& corners) {
    Roll roll;
    for (std::size_t slot = 0; slot < kCorners; ++slot) {
        const std::array<std::uint16_t, 3>& stickers = plan.corner_stickers.at(slot);
        const std::array<std::size_t, 3> shown = {
            reader.colour(stickers.at(0)),
            reader.colour(stickers.at(1)),
            reader.colour(stickers.at(2)),
        };
        const std::size_t turned = turn_of(shown);
        if (turned == kNotACorner) {
            return fault_at(Fault::UnknownPiece, Role::Corner, stickers.at(0));
        }
        const std::size_t key =
            (((shown.at(turned) * kColours) + shown.at((turned + 1) % 3)) * kColours) +
            shown.at((turned + 2) % 3);
        const std::int8_t home = plan.corner_home.at(key);
        if (home < 0) {
            return fault_at(Fault::UnknownPiece, Role::Corner, stickers.at(0));
        }
        const auto piece = static_cast<std::uint8_t>(home);
        if (!roll.take(piece)) {
            return fault_at(Fault::RepeatedPiece, Role::Corner, stickers.at(0));
        }
        corners.piece_at.at(slot) = piece;
        corners.twist += static_cast<int>(turned);
        if (turned != 0 && !corners.any_twisted) {
            corners.any_twisted = true;
            corners.first_twisted = stickers.at(0);
        }
    }
    return Verdict{};
}

// Wings: each ring's 24, once. A wing is the ordered pair of colours it shows
// on its primary and secondary stickers, so there is nothing to turn.
[[nodiscard]] Verdict read_wings(const Plan& plan, const Reader& reader) {
    for (std::size_t ring = 0; ring < plan.wings.size(); ++ring) {
        Roll roll;
        for (const Plan::Wing& wing : plan.wings.at(ring)) {
            const std::int8_t home = plan.wing_home.at(ring).at(
                (reader.colour(wing.primary) * kColours) + reader.colour(wing.secondary));
            if (home < 0) {
                return fault_at(Fault::UnknownPiece, Role::Wing, wing.primary);
            }
            if (!roll.take(static_cast<std::uint8_t>(home))) {
                return fault_at(Fault::RepeatedPiece, Role::Wing, wing.primary);
            }
        }
    }
    return Verdict{};
}

// Middle edges: each of the twelve, once, and whether it is flipped. An edge
// is oriented when the colour higher in the up-down, front-back, left-right
// ranking is on the sticker that ranks higher in its place.
[[nodiscard]] Verdict read_edges(const Plan& plan, const Reader& reader, Edges& edges) {
    Roll roll;
    for (std::size_t location = 0; location < kEdges; ++location) {
        const Plan::Edge& edge = plan.edges.at(location);
        const std::size_t first = reader.colour(edge.first);
        const std::size_t second = reader.colour(edge.second);
        const std::int8_t home = plan.edge_home.at((first * kColours) + second);
        if (home < 0) {
            return fault_at(Fault::UnknownPiece, Role::MidEdge, edge.first);
        }
        const auto piece = static_cast<std::uint8_t>(home);
        if (!roll.take(piece)) {
            return fault_at(Fault::RepeatedPiece, Role::MidEdge, edge.first);
        }
        edges.piece_at.at(location) = piece;
        const bool oriented = (kColourRank.at(first) < kColourRank.at(second)) == edge.first_leads;
        if (!oriented) {
            if (edges.flips == 0) {
                edges.first_flipped = edge.first;
            }
            ++edges.flips;
        }
    }
    return Verdict{};
}

// What no single piece shows. Every piece is real by now.
[[nodiscard]] Verdict check_totals(int size, const Corners& corners, const Edges& edges) {
    if (corners.twist % 3 != 0) {
        return fault_at(Fault::TwistedCorner, Role::Corner, corners.first_twisted);
    }
    if (edges.flips % 2 != 0) {
        return fault_at(Fault::FlippedEdge, Role::MidEdge, edges.first_flipped);
    }
    if (size % 2 == 1 && is_odd(corners.piece_at, kCorners) != is_odd(edges.piece_at, kEdges)) {
        return fault_of(Fault::SwappedPieces);
    }
    return Verdict{};
}

}  // namespace

Verdict validate(int size, std::span<const Color> facelets) {
    if (size < kMinSize || size > kMaxSize) {
        return fault_of(Fault::NotACube);
    }
    const Plan& plan = plan_of(size);
    if (const Verdict v = check_stickers(Geometry::of(size), facelets); !v.ok()) {
        return v;
    }

    const Result<std::array<Color, kColours>> scheme = scheme_of(size, facelets);
    if (!scheme) {
        return fault_of(Fault::NoScheme);
    }
    const Reader reader{facelets, *scheme};

    if (const Verdict v = check_centres(plan, facelets); !v.ok()) {
        return v;
    }
    Corners corners;
    if (const Verdict v = read_corners(plan, reader, corners); !v.ok()) {
        return v;
    }
    if (const Verdict v = read_wings(plan, reader); !v.ok()) {
        return v;
    }
    Edges edges;
    if (size % 2 == 1) {
        if (const Verdict v = read_edges(plan, reader, edges); !v.ok()) {
            return v;
        }
    }
    return check_totals(size, corners, edges);
}

}  // namespace cube::nxn
