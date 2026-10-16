#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <span>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/nxn/validate.hpp>
#include <cube/solver/cube.hpp>

#include "../deterministic_rng.hpp"

// cube::nxn::validate against the solver, which is the ground truth: a cube the
// solver can solve (and whose solution it has replayed to a solved cube) is
// one turns can produce, and a cube it refuses is not. The solver's side is a
// constructive proof of reachability; the oracle's rules are checked to be
// neither too strict (every solvable cube is valid) nor too loose (every
// invalid cube is unsolvable) over random cubes and over cubes damaged in each
// way a person could damage one by hand-entering a cube.

namespace {

using cube::nxn::Fault;
using cube::nxn::Geometry;
using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::nxn::Role;
using cube::nxn::Verdict;
using cube::solver::Color;
using cube::solver::Face;

using Facelets = std::vector<Color>;

constexpr std::array<int, 8> kSizes = {2, 3, 4, 5, 6, 7, 8, 9};

Facelets stickers_of(const NCube& cube) {
    const auto facelets = cube.facelets();
    return {facelets.begin(), facelets.end()};
}

Facelets scrambled(int size, unsigned seed, int moves) {
    return stickers_of(cube::nxn::random_scramble({.size = size, .seed = seed, .moves = moves}));
}

Facelets solved(int size) {
    return stickers_of(*NCube::solved(size));
}

Verdict verdict(int size, const Facelets& facelets) {
    return cube::nxn::validate(size, facelets);
}

// Whether the solver can solve these stickers: the ground truth.
bool solver_accepts(int size, const Facelets& facelets) {
    const auto cube = NCube::from_facelets(size, facelets);
    if (!cube) {
        return false;
    }
    return cube::nxn::solve(*cube).has_value();
}

// The same cube coloured another way: colour c is called permutation[c].
Facelets relabelled(Facelets facelets, const std::array<std::uint8_t, 6>& permutation) {
    for (Color& colour : facelets) {
        colour = static_cast<Color>(permutation.at(static_cast<std::size_t>(colour)));
    }
    return facelets;
}

std::array<std::uint8_t, 6> random_relabelling(std::mt19937& rng) {
    std::array<std::uint8_t, 6> permutation = {0, 1, 2, 3, 4, 5};
    std::ranges::shuffle(permutation, rng);
    return permutation;
}

void swap_at(Facelets& facelets, std::size_t a, std::size_t b) {
    std::swap(facelets.at(a), facelets.at(b));
}

// A corner's stickers (by facelet number: up-down, left-right, front-back).
const std::array<std::uint16_t, 3>& corner_stickers(int size, std::size_t corner) {
    return Geometry::of(size).corners().at(corner);
}

// Turns a corner in place: its stickers move one place round.
void twist_corner(Facelets& facelets, int size, std::size_t corner) {
    const auto& stickers = corner_stickers(size, corner);
    const Color kept = facelets.at(stickers.at(0));
    facelets.at(stickers.at(0)) = facelets.at(stickers.at(1));
    facelets.at(stickers.at(1)) = facelets.at(stickers.at(2));
    facelets.at(stickers.at(2)) = kept;
}

// Two corners trade places, each sticker going to the same-axis sticker of the
// other. Whether the result is a piece or a mirror of one depends on the two
// corners' handedness; either is a thing a person can get wrong.
void swap_corners(Facelets& facelets, int size, std::size_t a, std::size_t b) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        swap_at(facelets, corner_stickers(size, a).at(axis), corner_stickers(size, b).at(axis));
    }
}

}  // namespace

TEST_CASE("a solved cube of every size is valid, held any way and coloured any way",
          "[nxn][validate]") {
    auto rng = cube::test::deterministic_rng(1U);
    for (const int size : kSizes) {
        INFO("size " << size);
        CHECK(verdict(size, solved(size)).ok());
        for (int i = 0; i < 8; ++i) {
            CHECK(verdict(size, relabelled(solved(size), random_relabelling(rng))).ok());
        }
    }
}

TEST_CASE("every cube turns can produce is valid, at every size", "[nxn][validate]") {
    auto rng = cube::test::deterministic_rng(2U);
    for (const int size : kSizes) {
        for (unsigned seed = 1; seed <= 12; ++seed) {
            // From one move to far more than it takes to mix a cube.
            const int moves = 1 + static_cast<int>(seed * 9 * static_cast<unsigned>(size) / 2);
            const Facelets facelets = scrambled(size, seed, moves);
            INFO("size " << size << " seed " << seed << " moves " << moves);
            CHECK(verdict(size, facelets).ok());
            // The colour scheme is the cube's own: any other naming is the same cube.
            CHECK(verdict(size, relabelled(facelets, random_relabelling(rng))).ok());
        }
    }
}

TEST_CASE("every single turn of every layer is valid", "[nxn][validate]") {
    for (const int size : kSizes) {
        for (int axis = 0; axis < 3; ++axis) {
            for (int layer = 0; layer < size; ++layer) {
                for (int turns = 1; turns <= 3; ++turns) {
                    NCube cube = *NCube::solved(size);
                    cube.apply(cube::nxn::SliceTurn{
                        .axis = static_cast<cube::nxn::Axis>(axis),
                        .layer = layer,
                        .turns = turns,
                    });
                    INFO("size " << size << " axis " << axis << " layer " << layer << " turns "
                                 << turns);
                    CHECK(cube::nxn::validate(cube).ok());
                }
            }
        }
    }
}

TEST_CASE("a cube with a corner turned in place is refused", "[nxn][validate]") {
    for (const int size : kSizes) {
        INFO("size " << size);
        for (std::size_t corner = 0; corner < 8; ++corner) {
            Facelets facelets = scrambled(size, 5U, 30 + (4 * size));
            twist_corner(facelets, size, corner);
            const Verdict v = verdict(size, facelets);
            CHECK(v.fault == Fault::TwistedCorner);
            CHECK(v.piece == Role::Corner);
        }
    }
}

TEST_CASE("corner twists add up to a multiple of three, whichever corners they are on",
          "[nxn][validate]") {
    // Which way a corner's stickers must turn to add a third depends on which
    // corner it is (a mirror image of a corner turns the other way). So for any
    // pair of corners exactly one of "A once, B once" and "A once, B twice" is
    // a cube, and the other is a twist the cube cannot have.
    for (const int size : {2, 3, 4, 5, 6}) {
        for (std::size_t a = 0; a < 8; ++a) {
            for (std::size_t b = a + 1; b < 8; ++b) {
                Facelets once = scrambled(size, 6U, 40 + (3 * size));
                twist_corner(once, size, a);
                twist_corner(once, size, b);
                Facelets twice = scrambled(size, 6U, 40 + (3 * size));
                twist_corner(twice, size, a);
                twist_corner(twice, size, b);
                twist_corner(twice, size, b);
                INFO("size " << size << " corners " << a << " " << b);
                const Verdict first = verdict(size, once);
                const Verdict second = verdict(size, twice);
                CHECK(first.ok() != second.ok());
                CHECK((first.ok() || first.fault == Fault::TwistedCorner));
                CHECK((second.ok() || second.fault == Fault::TwistedCorner));
            }
        }
    }
}

TEST_CASE("a flipped middle edge is refused on an odd size only", "[nxn][validate]") {
    for (const int size : kSizes) {
        const Geometry& geometry = Geometry::of(size);
        Facelets facelets = scrambled(size, 7U, 50 + (5 * size));
        if (size % 2 == 0) {
            continue;
        }
        for (std::size_t edge = 0; edge < Geometry::kEdgeLocations; ++edge) {
            Facelets damaged = facelets;
            swap_at(damaged, geometry.mid_edge(edge).at(0), geometry.mid_edge(edge).at(1));
            INFO("size " << size << " edge " << edge);
            const Verdict v = verdict(size, damaged);
            // One flip is odd; the same flip on a cube that already has one
            // would not be, but a scramble has none.
            CHECK(v.fault == Fault::FlippedEdge);
            CHECK(v.piece == Role::MidEdge);
        }
    }
}

TEST_CASE("two swapped corners are refused on an odd size and fine on an even one",
          "[nxn][validate]") {
    // Of the pairs of corners that swap into pieces (those of the same
    // handedness), the first will do.
    for (const int size : kSizes) {
        bool found = false;
        for (std::size_t a = 0; a < 8 && !found; ++a) {
            for (std::size_t b = a + 1; b < 8 && !found; ++b) {
                Facelets facelets = scrambled(size, 9U, 60 + (4 * size));
                swap_corners(facelets, size, a, b);
                const Verdict v = verdict(size, facelets);
                if (v.fault == Fault::UnknownPiece) {
                    continue;  // the corners are of opposite handedness: a mirror image
                }
                found = true;
                INFO("size " << size << " corners " << a << " " << b);
                if (size % 2 == 1) {
                    CHECK(v.fault == Fault::SwappedPieces);
                } else {
                    // An even size has no middle edge to hold the corners'
                    // parity to, and its wings take any parity. The solver
                    // agrees; see the agreement test below.
                    CHECK(v.ok());
                    CHECK(solver_accepts(size, facelets));
                }
            }
        }
        CHECK(found);
    }
}

TEST_CASE("two swapped wings are fine at every size that has wings", "[nxn][validate]") {
    for (int size = 4; size <= 9; ++size) {
        const Geometry& geometry = Geometry::of(size);
        for (std::size_t ring = 0; ring < geometry.wing_orbits().size(); ++ring) {
            const auto& slots = geometry.wing_orbits().at(ring);
            Facelets facelets = scrambled(size, 10U, 60 + (6 * size));
            swap_at(facelets, slots.at(0).primary, slots.at(5).primary);
            swap_at(facelets, slots.at(0).secondary, slots.at(5).secondary);
            INFO("size " << size << " ring " << ring);
            CHECK(verdict(size, facelets).ok());
        }
    }
}

TEST_CASE("a wing with its stickers swapped is another wing, which the cube already has",
          "[nxn][validate]") {
    for (int size = 4; size <= 9; ++size) {
        const Geometry& geometry = Geometry::of(size);
        Facelets facelets = scrambled(size, 11U, 60 + (6 * size));
        const auto& slot = geometry.wing_orbits().at(0).at(3);
        swap_at(facelets, slot.primary, slot.secondary);
        INFO("size " << size);
        const Verdict v = verdict(size, facelets);
        CHECK_FALSE(v.ok());
        CHECK(v.piece == Role::Wing);
    }
}

TEST_CASE("centres swapped within a ring are fine and across rings are refused",
          "[nxn][validate]") {
    for (int size = 4; size <= 9; ++size) {
        const Geometry& geometry = Geometry::of(size);
        INFO("size " << size);
        // Two centre stickers of the first ring on different faces.
        const auto& ring = geometry.orbit_facelets(geometry.centre_orbits().at(0));
        std::size_t other = 1;
        while (geometry.face_of(ring.at(other)) == geometry.face_of(ring.at(0))) {
            ++other;
        }
        Facelets facelets = scrambled(size, 12U, 60 + (6 * size));
        Facelets inside = facelets;
        swap_at(inside, ring.at(0), ring.at(other));
        CHECK(verdict(size, inside).ok());

        if (geometry.centre_orbits().size() > 1) {
            // A centre sticker of the second ring in place of one of the first.
            const auto& second = geometry.orbit_facelets(geometry.centre_orbits().at(1));
            Facelets across = facelets;
            std::size_t pick = 0;
            while (across.at(second.at(pick)) == across.at(ring.at(0))) {
                ++pick;
            }
            swap_at(across, ring.at(0), second.at(pick));
            const Verdict v = verdict(size, across);
            CHECK(v.fault == Fault::CentreColours);
            CHECK(v.piece == Role::Centre);
        }
    }
}

TEST_CASE("stickers that are not a cube are refused with the reason", "[nxn][validate]") {
    // A cube of any size, of colours that do not add up.
    Facelets facelets = solved(5);
    facelets.at(0) = Color::Blue;
    CHECK(verdict(5, facelets).fault == Fault::ColourCounts);

    // Not the right number of stickers for the size, or not a size at all.
    CHECK(verdict(5, solved(4)).fault == Fault::NotACube);
    CHECK(verdict(1, solved(3)).fault == Fault::NotACube);
    CHECK(verdict(cube::nxn::kMaxSize + 1, solved(3)).fault == Fault::NotACube);
    Facelets not_a_colour = solved(3);
    not_a_colour.at(4) = static_cast<Color>(9);
    CHECK(verdict(3, not_a_colour).fault == Fault::NotACube);

    // Six centres that are not six colours: on an odd size nothing names the faces.
    Facelets same_centres = solved(3);
    const std::size_t up = Geometry::of(3).face_centre(Face::U);
    const std::size_t down = Geometry::of(3).face_centre(Face::D);
    same_centres.at(down) = same_centres.at(up);
    // Counts still add up if the colour the centre lost turns up elsewhere.
    same_centres.at(Geometry::of(3).index(Face::U, 0, 0)) = Color::White;
    CHECK(verdict(3, same_centres).fault == Fault::NoScheme);

    // A corner's sticker painted over by the colour opposite one on the same corner.
    Facelets bad_corner = solved(3);
    const auto& corner = corner_stickers(3, 0);
    std::swap(bad_corner.at(corner.at(0)), bad_corner.at(corner.at(1)));
    CHECK_FALSE(verdict(3, bad_corner).ok());
}

TEST_CASE("the verdict is the same for a cube held and coloured any way", "[nxn][validate]") {
    auto rng = cube::test::deterministic_rng(3U);
    for (const int size : {2, 3, 4, 5}) {
        for (int i = 0; i < 40; ++i) {
            Facelets facelets = scrambled(size, 100U + static_cast<unsigned>(i), 40);
            // Damage it roughly half the time.
            if (i % 2 == 0) {
                swap_at(facelets, rng() % facelets.size(), rng() % facelets.size());
            }
            const Verdict plain = verdict(size, facelets);
            for (int k = 0; k < 4; ++k) {
                const Verdict other = verdict(size, relabelled(facelets, random_relabelling(rng)));
                INFO("size " << size << " sample " << i);
                CHECK(other.ok() == plain.ok());
            }
        }
    }
}

namespace {

// One way to damage a cube by hand, drawn at random.
void damage(int size, Facelets& facelets, std::mt19937& rng) {
    const Geometry& geometry = Geometry::of(size);
    const auto pick = [&rng](std::size_t below) { return static_cast<std::size_t>(rng() % below); };
    switch (rng() % 10U) {
        case 0:  // any two stickers
            swap_at(facelets, pick(facelets.size()), pick(facelets.size()));
            break;
        case 1: {  // two stickers of one orbit: stays among the same kind of place
            const int orbit = geometry.orbit(pick(facelets.size()));
            const auto& places = geometry.orbit_facelets(orbit);
            swap_at(facelets, places.at(pick(places.size())), places.at(pick(places.size())));
            break;
        }
        case 2:
            twist_corner(facelets, size, pick(8));
            break;
        case 3:
            swap_corners(facelets, size, pick(8), pick(8));
            break;
        case 4: {  // a wing, or a middle edge, flipped
            if (size % 2 == 1) {
                const auto& stickers = geometry.mid_edge(pick(Geometry::kEdgeLocations));
                swap_at(facelets, stickers.at(0), stickers.at(1));
            } else if (size >= 4) {
                const auto& orbit = geometry.wing_orbits().at(pick(geometry.wing_orbits().size()));
                const auto& slot = orbit.at(pick(orbit.size()));
                swap_at(facelets, slot.primary, slot.secondary);
            }
            break;
        }
        case 5: {  // two wings trade places
            if (size >= 4) {
                const auto& orbit = geometry.wing_orbits().at(pick(geometry.wing_orbits().size()));
                const auto& a = orbit.at(pick(orbit.size()));
                const auto& b = orbit.at(pick(orbit.size()));
                swap_at(facelets, a.primary, b.primary);
                swap_at(facelets, a.secondary, b.secondary);
            }
            break;
        }
        case 6: {  // two middle edges trade places, keeping or losing their ends
            if (size % 2 == 1) {
                const auto& a = geometry.mid_edge(pick(Geometry::kEdgeLocations));
                const auto& b = geometry.mid_edge(pick(Geometry::kEdgeLocations));
                const std::size_t crossed = pick(2);
                swap_at(facelets, a.at(0), b.at(crossed));
                swap_at(facelets, a.at(1), b.at(1 - crossed));
            }
            break;
        }
        case 7: {  // two centre stickers of one ring
            if (size >= 4) {
                const auto& ring = geometry.orbit_facelets(
                    geometry.centre_orbits().at(pick(geometry.centre_orbits().size())));
                swap_at(facelets, ring.at(pick(ring.size())), ring.at(pick(ring.size())));
            }
            break;
        }
        case 8: {  // three stickers of one orbit move round
            const int orbit = geometry.orbit(pick(facelets.size()));
            const auto& places = geometry.orbit_facelets(orbit);
            const std::size_t a = places.at(pick(places.size()));
            const std::size_t b = places.at(pick(places.size()));
            const std::size_t c = places.at(pick(places.size()));
            swap_at(facelets, a, b);
            swap_at(facelets, b, c);
            break;
        }
        default:  // two corners twisted, which only sometimes adds up
            twist_corner(facelets, size, pick(8));
            twist_corner(facelets, size, pick(8));
            break;
    }
}

}  // namespace

TEST_CASE("the oracle and the solver agree on cubes damaged by hand", "[nxn][validate][slow]") {
    auto rng = cube::test::deterministic_rng(4U);
    struct Budget {
        int size;
        int samples;
    };
    // The solver's side of a valid cube is a whole solve, which costs more as
    // the cube grows, so the big sizes get fewer samples.
    const std::array<Budget, 8> budget = {
        {
            {.size = 2, .samples = 40},
            {.size = 3, .samples = 300},
            {.size = 4, .samples = 200},
            {.size = 5, .samples = 150},
            {.size = 6, .samples = 60},
            {.size = 7, .samples = 40},
            {.size = 8, .samples = 12},
            {.size = 9, .samples = 8},
        },
    };
    for (const auto& [size, samples] : budget) {
        int accepted = 0;
        int refused = 0;
        for (int i = 0; i < samples; ++i) {
            Facelets facelets = scrambled(size, 500U + static_cast<unsigned>(i), 30 + (8 * size));
            const int hits = 1 + static_cast<int>(rng() % 2U);
            for (int h = 0; h < hits; ++h) {
                damage(size, facelets, rng);
            }
            const Verdict v = verdict(size, facelets);
            INFO("size " << size << " sample " << i << " fault " << cube::nxn::to_string(v.fault));
            CHECK(v.ok() == solver_accepts(size, facelets));
            (v.ok() ? accepted : refused) += 1;
        }
        // Both answers are exercised, or the test proves nothing.
        INFO("size " << size << " accepted " << accepted << " refused " << refused);
        CHECK(accepted > 0);
        CHECK(refused > 0);
    }
}

TEST_CASE(
    "at size 3 the oracle is the solver's verdict, and Cube::is_solvable's where that is exact",
    "[nxn][validate]") {
    auto rng = cube::test::deterministic_rng(5U);
    int accepted = 0;
    int refused = 0;
    int stricter = 0;
    for (int i = 0; i < 1500; ++i) {
        Facelets facelets = scrambled(3, 900U + static_cast<unsigned>(i), 25);
        const int hits = static_cast<int>(rng() % 3U);
        for (int h = 0; h < hits; ++h) {
            damage(3, facelets, rng);
        }
        const auto cube = NCube::from_facelets(3, facelets);
        REQUIRE(cube.has_value());
        const Verdict v = verdict(3, facelets);
        // The 3x3 solvers read a cube with its centres renamed to their faces;
        // the oracle accepts any naming, so the 3x3 test is applied to that reading.
        const auto cube3 = cube->to_cube3();
        const bool is_solvable = cube3.has_value() && cube3->is_solvable();
        INFO("sample " << i << " fault " << cube::nxn::to_string(v.fault));
        // The solver is the ground truth, and costs a solve: ask it where the
        // two disagree, and of one cube in ten besides.
        if (v.ok() != is_solvable || i % 10 == 0) {
            CHECK(v.ok() == solver_accepts(3, facelets));
        }
        // A cube the oracle accepts is one is_solvable accepts...
        CHECK((!v.ok() || is_solvable));
        // ...and the reverse holds except for cubes with a piece no cube has,
        // which Cube::is_solvable does not look for: a corner that is the
        // mirror image of a real one passes it, and is refused by the solver.
        if (v.fault != Fault::UnknownPiece) {
            CHECK(v.ok() == is_solvable);
        } else if (is_solvable) {
            ++stricter;
        }
        (v.ok() ? accepted : refused) += 1;
    }
    CHECK(accepted > 0);
    CHECK(refused > 0);
    // Not a coincidence of the sample: the oracle really is the stricter test.
    CHECK(stricter > 0);
}

namespace {

// Every position of a 2x2, enumerated by breadth-first search on its facelets
// and nothing else: an answer to "is this a 2x2 turns can produce" that shares
// no code and no rule with the oracle. R, U and F never move the corner at
// down-back-left, so the 3,674,160 positions they reach are the whole cube's
// 3,674,160 x 24 divided by the 24 ways to hold it, each in one frame.
class EveryTwoByTwo {
public:
    EveryTwoByTwo() {
        find_fixed_corner();
        choose_moves();
        search();
    }

    [[nodiscard]] std::size_t size() const { return positions_.size(); }
    [[nodiscard]] const std::vector<std::uint64_t>& positions() const { return positions_; }

    // The position as stickers, in the frame its search ran in.
    [[nodiscard]] Facelets unpack(std::uint64_t key) const {
        Facelets state = solved(2);
        for (std::size_t i = 0; i < free_.size(); ++i) {
            state.at(free_.at(i)) = static_cast<Color>((key >> (3 * i)) & 7U);
        }
        return state;
    }

    // Whether the stickers are a position, in any naming of the six colours:
    // the three colours of the down-back-left corner say what down, left and
    // back are called, and the other three may go to up, right and front in any
    // order.
    [[nodiscard]] bool contains_in_some_naming(const Facelets& stickers) const {
        std::array<std::uint8_t, 6> naming{};
        std::array<bool, 6> named{};
        for (const Face face : kFixedFaces) {
            const auto colour = static_cast<std::size_t>(stickers.at(fixed_.at(face_index(face))));
            if (named.at(colour)) {
                return false;  // the corner shows one colour twice
            }
            naming.at(colour) = static_cast<std::uint8_t>(cube::solver::canonical_color(face));
            named.at(colour) = true;
        }
        std::vector<std::size_t> others;
        for (std::size_t colour = 0; colour < 6; ++colour) {
            if (!named.at(colour)) {
                others.push_back(colour);
            }
        }
        const std::array<std::uint8_t, 3> up_right_front = {
            static_cast<std::uint8_t>(cube::solver::canonical_color(Face::U)),
            static_cast<std::uint8_t>(cube::solver::canonical_color(Face::R)),
            static_cast<std::uint8_t>(cube::solver::canonical_color(Face::F)),
        };
        // The three colours left may be called up, right and front in any order.
        constexpr std::array<std::array<std::size_t, 3>, 6> kOrders = {
            {
                {0, 1, 2},
                {0, 2, 1},
                {1, 0, 2},
                {1, 2, 0},
                {2, 0, 1},
                {2, 1, 0},
            },
        };
        for (const auto& order : kOrders) {
            for (std::size_t i = 0; i < 3; ++i) {
                naming.at(others.at(i)) = up_right_front.at(order.at(i));
            }
            if (std::ranges::binary_search(positions_, pack(relabelled(stickers, naming)))) {
                return true;
            }
        }
        return false;
    }

private:
    static constexpr std::array<Face, 3> kFixedFaces = {Face::D, Face::L, Face::B};

    [[nodiscard]] static std::size_t face_index(Face face) {
        return static_cast<std::size_t>(face);
    }

    // The facelets of the corner at down-back-left, by the face each is on.
    void find_fixed_corner() {
        const Geometry& geometry = Geometry::of(2);
        for (const auto& corner : geometry.corners()) {
            const auto is_on = [&](Face face) {
                return std::ranges::any_of(corner, [&](std::uint16_t facelet) {
                    return geometry.face_of(facelet) == face;
                });
            };
            if (!std::ranges::all_of(kFixedFaces, is_on)) {
                continue;
            }
            for (const std::uint16_t facelet : corner) {
                fixed_.at(face_index(geometry.face_of(facelet))) = facelet;
            }
        }
        for (std::uint16_t facelet = 0; facelet < 24; ++facelet) {
            const auto is_fixed = [&](Face face) { return fixed_.at(face_index(face)) == facelet; };
            if (!std::ranges::any_of(kFixedFaces, is_fixed)) {
                free_.push_back(facelet);
            }
        }
        REQUIRE(free_.size() == 21);
    }

    // R, U and F, by one, two and three quarter turns.
    void choose_moves() {
        const Geometry& geometry = Geometry::of(2);
        for (const Face face : {Face::R, Face::U, Face::F}) {
            for (int turns = 1; turns <= 3; ++turns) {
                moves_.push_back(geometry.turn(cube::nxn::to_slice_turn(
                    LayerMove{.face = face, .depth = 0, .turns = turns}, 2)));
            }
        }
        // The corner they leave alone is the one the search holds fixed.
        for (const auto& move : moves_) {
            for (const Face face : kFixedFaces) {
                const std::size_t facelet = fixed_.at(face_index(face));
                REQUIRE(move.at(facelet) == facelet);
            }
        }
    }

    // Breadth first, a level at a time, over sorted vectors of packed positions.
    void search() {
        std::vector<std::uint64_t> frontier{pack(solved(2))};
        positions_ = frontier;
        while (!frontier.empty()) {
            std::vector<std::uint64_t> next = successors(frontier);
            std::ranges::sort(next);
            const auto [tail, end] = std::ranges::unique(next);
            next.erase(tail, end);
            std::vector<std::uint64_t> fresh;
            std::ranges::set_difference(next, positions_, std::back_inserter(fresh));
            std::vector<std::uint64_t> merged;
            merged.reserve(positions_.size() + fresh.size());
            std::ranges::merge(positions_, fresh, std::back_inserter(merged));
            positions_ = std::move(merged);
            frontier = std::move(fresh);
        }
    }

    [[nodiscard]] std::vector<std::uint64_t> successors(
        const std::vector<std::uint64_t>& frontier) const {
        std::vector<std::uint64_t> next;
        next.reserve(frontier.size() * moves_.size());
        for (const std::uint64_t key : frontier) {
            const Facelets state = unpack(key);
            for (const auto& move : moves_) {
                Facelets moved(24);
                for (std::size_t i = 0; i < 24; ++i) {
                    moved.at(i) = state.at(move.at(i));
                }
                next.push_back(pack(moved));
            }
        }
        return next;
    }

    [[nodiscard]] std::uint64_t pack(const Facelets& stickers) const {
        std::uint64_t key = 0;
        for (std::size_t i = 0; i < free_.size(); ++i) {
            key |= static_cast<std::uint64_t>(stickers.at(free_.at(i))) << (3 * i);
        }
        return key;
    }

    std::array<std::size_t, 6> fixed_{};
    std::vector<std::uint16_t> free_;
    std::vector<cube::nxn::Permutation> moves_;
    std::vector<std::uint64_t> positions_;
};

const EveryTwoByTwo& every_two_by_two() {
    static const EveryTwoByTwo all;
    return all;
}

}  // namespace

TEST_CASE("at size 2 the oracle accepts exactly the positions turns reach",
          "[nxn][validate][slow]") {
    const EveryTwoByTwo& all = every_two_by_two();
    // The search is itself right: the size of the 2x2 group, fixing a corner.
    REQUIRE(all.size() == 3674160U);

    // No position is refused...
    std::size_t refused = 0;
    for (const std::uint64_t key : all.positions()) {
        refused += cube::nxn::validate(2, all.unpack(key)).ok() ? 0U : 1U;
    }
    CHECK(refused == 0);

    // ...and nothing else is accepted: positions damaged by hand, shuffled
    // stickers, and every one of them held another way and coloured another way.
    auto rng = cube::test::deterministic_rng(6U);
    std::size_t accepted_outside = 0;
    std::size_t rejected_inside = 0;
    std::size_t outside = 0;
    for (int i = 0; i < 60000; ++i) {
        Facelets stickers = all.unpack(all.positions().at(rng() % all.size()));
        switch (i % 4) {
            case 0:  // damaged once
                damage(2, stickers, rng);
                break;
            case 1:  // damaged twice
                damage(2, stickers, rng);
                damage(2, stickers, rng);
                break;
            case 2:  // shuffled
                std::ranges::shuffle(stickers, rng);
                break;
            default:  // untouched
                break;
        }
        // Held another way and named another way: the answer is the same.
        stickers = relabelled(std::move(stickers), random_relabelling(rng));
        auto cube = NCube::from_facelets(2, stickers);
        REQUIRE(cube.has_value());
        const auto axis = static_cast<cube::nxn::Axis>(rng() % 3U);
        for (int layer = 0; layer < 2; ++layer) {
            cube->apply(cube::nxn::SliceTurn{.axis = axis, .layer = layer, .turns = 1});
        }
        const Facelets held = stickers_of(*cube);
        const bool inside = all.contains_in_some_naming(held);
        const bool accepted = cube::nxn::validate(2, held).ok();
        outside += inside ? 0U : 1U;
        accepted_outside += (accepted && !inside) ? 1U : 0U;
        rejected_inside += (!accepted && inside) ? 1U : 0U;
    }
    CHECK(accepted_outside == 0);
    CHECK(rejected_inside == 0);
    CHECK(outside > 10000);
}
