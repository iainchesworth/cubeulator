#include <catch2/catch_test_macros.hpp>

#include <random>

#include <cube/solver/cube.hpp>

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;

TEST_CASE("a solved cube is solved and physically valid", "[cube]") {
    const Cube solved = Cube::solved();
    REQUIRE(solved.is_solved());
    REQUIRE(solved.is_physically_valid());
}

TEST_CASE("the six faces of a solved cube are pairwise distinct colours", "[cube]") {
    const Cube solved = Cube::solved();
    std::array<Color, 6> centers{};
    for (Face face : kAllFaces) {
        centers[static_cast<std::size_t>(face)] = solved.at(face, 4);
    }
    for (std::size_t i = 0; i < centers.size(); ++i) {
        for (std::size_t j = i + 1; j < centers.size(); ++j) {
            REQUIRE(centers[i] != centers[j]);
        }
    }
}

TEST_CASE("applying a quarter turn four times is the identity", "[cube]") {
    for (Face face : kAllFaces) {
        Cube c = Cube::solved();
        for (int i = 0; i < 4; ++i) {
            c = c.apply(Move{.face = face, .turns = 1});
        }
        REQUIRE(c == Cube::solved());
        REQUIRE(c.is_solved());
    }
}

TEST_CASE("applying a half turn twice is the identity", "[cube]") {
    for (Face face : kAllFaces) {
        Cube c = Cube::solved()
                     .apply(Move{.face = face, .turns = 2})
                     .apply(Move{.face = face, .turns = 2});
        REQUIRE(c == Cube::solved());
    }
}

TEST_CASE("a move followed by its inverse round-trips, from solved and from a scramble", "[cube]") {
    std::mt19937 rng{12345};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};

    // A fixed pseudo-random scramble, applied once up front, so the
    // round-trip check below runs against a non-trivial starting state too.
    Cube scrambled = Cube::solved();
    for (int i = 0; i < 20; ++i) {
        scrambled = scrambled.apply(Move{.face = kAllFaces[face_dist(rng)], .turns = 1 + (i % 3)});
    }

    for (const Cube& start : {Cube::solved(), scrambled}) {
        for (Face face : kAllFaces) {
            for (int turns = 1; turns <= 3; ++turns) {
                const Move move{.face = face, .turns = turns};
                const Cube after = start.apply(move).apply(move.inverse());
                REQUIRE(after == start);
            }
        }
    }
}

TEST_CASE("R turn cycles U/F/D/B's right-hand columns in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise R turn (viewed from the right) cycles the R-adjacent
    // stickers U -> B -> D -> F -> U.
    const Cube c = Cube::solved().apply(Move{.face = Face::R, .turns = 1});

    for (int row = 0; row < 3; ++row) {
        CAPTURE(row);
        REQUIRE(c.at(Face::U, row, 2) == Color::Green);   // received from F
        REQUIRE(c.at(Face::B, row, 2) == Color::Yellow);  // received from U
        REQUIRE(c.at(Face::D, row, 2) == Color::Blue);    // received from B
        REQUIRE(c.at(Face::F, row, 2) == Color::White);   // received from D
    }
    for (int i = 0; i < 9; ++i) {
        REQUIRE(c.at(Face::R, i) == Color::Orange);
        REQUIRE(c.at(Face::L, i) == Color::Red);
    }
}

TEST_CASE("U turn cycles F/R/B/L's top rows in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise U turn (viewed from above) cycles the U-adjacent stickers
    // F -> L -> B -> R -> F.
    const Cube c = Cube::solved().apply(Move{.face = Face::U, .turns = 1});

    for (int col = 0; col < 3; ++col) {
        CAPTURE(col);
        REQUIRE(c.at(Face::F, 0, col) == Color::Orange);  // received from R
        REQUIRE(c.at(Face::L, 0, col) == Color::Green);   // received from F
        REQUIRE(c.at(Face::B, 0, col) == Color::Red);     // received from L
        REQUIRE(c.at(Face::R, 0, col) == Color::Blue);    // received from B
    }
    for (int i = 0; i < 9; ++i) {
        REQUIRE(c.at(Face::U, i) == Color::Yellow);
        REQUIRE(c.at(Face::D, i) == Color::White);
    }
}

TEST_CASE("F turn cycles U/R/D/L's F-adjacent lines in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise F turn (viewed from the front) cycles the F-adjacent
    // stickers U -> R -> D -> L -> U.
    const Cube c = Cube::solved().apply(Move{.face = Face::F, .turns = 1});

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        REQUIRE(c.at(Face::R, i, 2) == Color::Yellow);  // received from U
        REQUIRE(c.at(Face::D, 2, i) == Color::Orange);  // received from R
        REQUIRE(c.at(Face::L, i, 2) == Color::White);   // received from D
        REQUIRE(c.at(Face::U, 2, i) == Color::Red);     // received from L
    }
}

TEST_CASE("L turn cycles U/F/D/B's L-adjacent lines in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise L turn (viewed from the left) cycles the L-adjacent
    // stickers U -> F -> D -> B -> U.
    const Cube c = Cube::solved().apply(Move{.face = Face::L, .turns = 1});

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        REQUIRE(c.at(Face::F, i, 0) == Color::Yellow);  // received from U
        REQUIRE(c.at(Face::D, i, 0) == Color::Green);   // received from F
        REQUIRE(c.at(Face::B, i, 0) == Color::White);   // received from D
        REQUIRE(c.at(Face::U, i, 0) == Color::Blue);    // received from B
    }
}

TEST_CASE("D turn cycles F/R/B/L's bottom rows in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise D turn (viewed from below) cycles the D-adjacent stickers
    // F -> R -> B -> L -> F.
    const Cube c = Cube::solved().apply(Move{.face = Face::D, .turns = 1});

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        REQUIRE(c.at(Face::R, 2, i) == Color::Green);   // received from F
        REQUIRE(c.at(Face::B, 2, i) == Color::Orange);  // received from R
        REQUIRE(c.at(Face::L, 2, i) == Color::Blue);    // received from B
        REQUIRE(c.at(Face::F, 2, i) == Color::Red);     // received from L
    }
}

TEST_CASE("B turn cycles U/L/D/R's B-adjacent lines in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise B turn (viewed from the back) cycles the B-adjacent
    // stickers U -> L -> D -> R -> U.
    const Cube c = Cube::solved().apply(Move{.face = Face::B, .turns = 1});

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        REQUIRE(c.at(Face::L, i, 0) == Color::Yellow);  // received from U
        REQUIRE(c.at(Face::D, 0, i) == Color::Red);     // received from L
        REQUIRE(c.at(Face::R, i, 0) == Color::White);   // received from D
        REQUIRE(c.at(Face::U, 0, i) == Color::Orange);  // received from R
    }
}

TEST_CASE("half and prime turns equal composed quarter turns, for every face", "[cube]") {
    // Pins each face's turns=2 and turns=3 behaviour to compositions of
    // its own quarter turn - together with the per-face direction tests
    // above, this anchors all 18 (face, turns) combinations to physical
    // reality, so a regression in any one of them (e.g. a mirrored
    // rotation or swapped turn variant in a future implementation change)
    // fails here rather than slipping through the direction-symmetric
    // identity/round-trip tests.
    std::mt19937 rng{24680};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    Cube scrambled = Cube::solved();
    for (int i = 0; i < 20; ++i) {
        scrambled = scrambled.apply(Move{.face = kAllFaces[face_dist(rng)], .turns = 1 + (i % 3)});
    }

    for (const Cube& start : {Cube::solved(), scrambled}) {
        for (Face face : kAllFaces) {
            const Cube quarter = start.apply(Move{.face = face, .turns = 1});
            REQUIRE(start.apply(Move{face, 2}) == quarter.apply(Move{face, 1}));
            REQUIRE(start.apply(Move{face, 3}) ==
                    quarter.apply(Move{face, 1}).apply(Move{face, 1}));
        }
    }
}

TEST_CASE("random move sequences stay physically valid at every step", "[cube]") {
    std::mt19937 rng{987654};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};

    Cube c = Cube::solved();
    for (int i = 0; i < 200; ++i) {
        c = c.apply(Move{.face = kAllFaces[face_dist(rng)], .turns = turn_dist(rng)});
        CAPTURE(i);
        REQUIRE(c.is_physically_valid());
    }
}

TEST_CASE("locate_edge finds every edge of a solved cube on its own two faces", "[cube]") {
    const Cube solved = Cube::solved();
    struct Case {
        Color a;
        Color b;
        Face expected_face_a;
        Face expected_face_b;
    };
    // One representative per axis-pair combination, not all 12 - the point
    // is to exercise locate_edge's face-pair enumeration, not re-derive the
    // full edge list.
    for (auto [a, b, face_a, face_b] : {
             Case{.a = Color::Yellow,
                  .b = Color::Green,
                  .expected_face_a = Face::U,
                  .expected_face_b = Face::F},
             Case{.a = Color::White,
                  .b = Color::Orange,
                  .expected_face_a = Face::D,
                  .expected_face_b = Face::R},
             Case{.a = Color::Green,
                  .b = Color::Orange,
                  .expected_face_a = Face::F,
                  .expected_face_b = Face::R},
         }) {
        const auto loc = solved.locate_edge(a, b);
        REQUIRE(loc.face_a == face_a);
        REQUIRE(loc.face_b == face_b);
        REQUIRE(solved.at(loc.face_a, loc.index_a) == a);
        REQUIRE(solved.at(loc.face_b, loc.index_b) == b);

        // Order-independence: swapping the query colours swaps the result.
        const auto swapped = solved.locate_edge(b, a);
        REQUIRE(swapped.face_a == face_b);
        REQUIRE(swapped.face_b == face_a);
    }
}

TEST_CASE("locate_edge tracks a piece through a move", "[cube]") {
    // Before R: the White/Orange edge sits at DR (White on D, Orange on
    // R). A single R turn carries the DR position to FR (see the golden
    // R-turn test above: R's layer stays on R, and D's content becomes
    // F's), with White now on F and Orange still on R.
    const Cube c = Cube::solved().apply(Move{.face = Face::R, .turns = 1});
    const auto loc = c.locate_edge(Color::White, Color::Orange);
    REQUIRE(loc.face_a == Face::F);
    REQUIRE(loc.face_b == Face::R);
}

TEST_CASE("from_facelets round-trips a physically valid cube's own facelets", "[cube]") {
    const Cube original = Cube::solved()
                              .apply(Move{.face = Face::R, .turns = 1})
                              .apply(Move{.face = Face::U, .turns = 2});
    std::array<std::array<Color, 9>, 6> facelets{};
    for (Face face : kAllFaces) {
        facelets[static_cast<std::size_t>(face)] = original.face(face);
    }

    const auto result = Cube::from_facelets(facelets);
    REQUIRE(result.has_value());
    REQUIRE(*result == original);
}

TEST_CASE("from_facelets rejects a malformed facelet arrangement", "[cube]") {
    std::array<std::array<Color, 9>, 6> facelets{};
    for (auto& face_facelets : facelets) {
        face_facelets.fill(Color::White);  // 54 White stickers - not a real cube
    }

    const auto result = Cube::from_facelets(facelets);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == cube::CubeError::invalid_cube_state);
}

TEST_CASE("locate_corner finds a solved cube's corners on all three of their faces", "[cube]") {
    const Cube solved = Cube::solved();
    const auto loc = solved.locate_corner(Color::Yellow, Color::Green, Color::Orange);
    REQUIRE(loc.face_a == Face::U);
    REQUIRE(loc.face_b == Face::F);
    REQUIRE(loc.face_c == Face::R);
    REQUIRE(solved.at(loc.face_a, loc.index_a) == Color::Yellow);
    REQUIRE(solved.at(loc.face_b, loc.index_b) == Color::Green);
    REQUIRE(solved.at(loc.face_c, loc.index_c) == Color::Orange);
}

namespace {

// A solved cube's raw facelets, as a mutable starting point for the
// is_solvable tests below that hand-tamper with individual stickers.
std::array<std::array<Color, 9>, 6> solved_facelets() {
    std::array<std::array<Color, 9>, 6> facelets{};
    for (Face face : kAllFaces) {
        facelets[static_cast<std::size_t>(face)] = Cube::solved().face(face);
    }
    return facelets;
}

}  // namespace

TEST_CASE("solved and randomly scrambled cubes are solvable", "[cube]") {
    REQUIRE(Cube::solved().is_solvable());

    std::mt19937 rng{424242};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};

    Cube c = Cube::solved();
    for (int i = 0; i < 200; ++i) {
        c = c.apply(Move{.face = kAllFaces[face_dist(rng)], .turns = turn_dist(rng)});
        CAPTURE(i);
        REQUIRE(c.is_solvable());
    }
}

TEST_CASE("a single twisted corner is physically valid but not solvable", "[cube]") {
    // The URF corner sits at (1, 1, 1); its three stickers are U index 8
    // (= (z+1)*3 + (x+1)), R index 2 (= (1-y)*3 + (z+1)) and F index 2
    // (= (1-y)*3 + (x+1)), holding Yellow/Orange/Green on a solved cube.
    // 3-cycling the three colours around those fixed positions is exactly
    // a physical twist of that one cubie in place: the colour set stays
    // {Yellow, Orange, Green}, so the colour-count and axis-pair checks
    // still pass, but the corner twist sum becomes 1 or 2 (mod 3).
    for (bool clockwise : {false, true}) {
        CAPTURE(clockwise);
        auto facelets = solved_facelets();
        auto& u = facelets[static_cast<std::size_t>(Face::U)];
        auto& r = facelets[static_cast<std::size_t>(Face::R)];
        auto& f = facelets[static_cast<std::size_t>(Face::F)];
        if (clockwise) {
            u[8] = Color::Green;
            r[2] = Color::Yellow;
            f[2] = Color::Orange;
        } else {
            u[8] = Color::Orange;
            r[2] = Color::Green;
            f[2] = Color::Yellow;
        }

        const auto result = Cube::from_facelets(facelets);
        REQUIRE(result.has_value());           // passes every physical check...
        REQUIRE_FALSE(result->is_solvable());  // ...but breaks the twist invariant
    }
}

TEST_CASE("a single flipped edge is physically valid but not solvable", "[cube]") {
    // The UF edge sits at (0, 1, 1): U index 7, F index 1 (same index
    // formulas as above), Yellow/Green on a solved cube. Swapping the two
    // stickers is a pure flip of that one cubie in place - colour counts
    // and cubie colour-sets untouched - but the flip sum becomes 1 (mod 2).
    auto facelets = solved_facelets();
    facelets[static_cast<std::size_t>(Face::U)][7] = Color::Green;
    facelets[static_cast<std::size_t>(Face::F)][1] = Color::Yellow;

    const auto result = Cube::from_facelets(facelets);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->is_solvable());
}

TEST_CASE("two swapped corners are not solvable but a single quarter turn is", "[cube]") {
    // Exchanging the URF and ULF corner cubies while preserving their
    // orientations touches only two stickers: the two cubies share Yellow
    // on their U facets and Green on their F facets, so the whole swap
    // reduces to exchanging the x-axis-facet stickers, R index 2 (Orange)
    // and L index 2 (Red). That is a single corner transposition (odd
    // permutation) with the edge permutation untouched (even) - unequal
    // parities, unreachable.
    auto facelets = solved_facelets();
    facelets[static_cast<std::size_t>(Face::R)][2] = Color::Red;
    facelets[static_cast<std::size_t>(Face::L)][2] = Color::Orange;

    const auto result = Cube::from_facelets(facelets);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->is_solvable());

    // Whereas a quarter turn 4-cycles four corners AND four edges - both
    // parities go odd together - so it must remain solvable.
    for (Face face : kAllFaces) {
        REQUIRE(Cube::solved().apply(Move{face, 1}).is_solvable());
    }
}

TEST_CASE("centres permuted from canonical are not solvable", "[cube]") {
    // A solved cube rotated bodily about the L-R axis: F's Green ends up
    // on U, U's Yellow on B, B's Blue on D and D's White on F. Every face
    // is still a perfect one-colour square - this is exactly what a
    // free-form scan produces when correct faces are assembled into the
    // wrong slots - so it sails through every physical check. But centres
    // never move under face turns, so no move sequence reaches it from
    // solved(): is_solvable() must reject it.
    std::array<std::array<Color, 9>, 6> facelets{};
    const auto fill = [&](Face face, Color color) {
        facelets[static_cast<std::size_t>(face)].fill(color);
    };
    fill(Face::U, Color::Green);
    fill(Face::B, Color::Yellow);
    fill(Face::D, Color::Blue);
    fill(Face::F, Color::White);
    fill(Face::L, Color::Red);
    fill(Face::R, Color::Orange);

    const auto result = Cube::from_facelets(facelets);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->is_solvable());
}
