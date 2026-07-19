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
            c = c.apply(Move{face, 1});
        }
        REQUIRE(c == Cube::solved());
        REQUIRE(c.is_solved());
    }
}

TEST_CASE("applying a half turn twice is the identity", "[cube]") {
    for (Face face : kAllFaces) {
        Cube c = Cube::solved().apply(Move{face, 2}).apply(Move{face, 2});
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
        scrambled = scrambled.apply(Move{kAllFaces[face_dist(rng)], 1 + static_cast<int>(i % 3)});
    }

    for (const Cube& start : {Cube::solved(), scrambled}) {
        for (Face face : kAllFaces) {
            for (int turns = 1; turns <= 3; ++turns) {
                const Move move{face, turns};
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
    const Cube c = Cube::solved().apply(Move{Face::R, 1});

    for (int row = 0; row < 3; ++row) {
        CAPTURE(row);
        REQUIRE(c.at(Face::U, row, 2) == Color::Green);   // received from F
        REQUIRE(c.at(Face::B, row, 2) == Color::Yellow);  // received from U
        REQUIRE(c.at(Face::D, row, 2) == Color::Blue);    // received from B
        REQUIRE(c.at(Face::F, row, 2) == Color::White);   // received from D
    }
    for (int i = 0; i < 9; ++i) {
        REQUIRE(c.at(Face::R, i) == Color::Red);
        REQUIRE(c.at(Face::L, i) == Color::Orange);
    }
}

TEST_CASE("U turn cycles F/R/B/L's top rows in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise U turn (viewed from above) cycles the U-adjacent stickers
    // F -> L -> B -> R -> F.
    const Cube c = Cube::solved().apply(Move{Face::U, 1});

    for (int col = 0; col < 3; ++col) {
        CAPTURE(col);
        REQUIRE(c.at(Face::F, 0, col) == Color::Red);     // received from R
        REQUIRE(c.at(Face::L, 0, col) == Color::Green);   // received from F
        REQUIRE(c.at(Face::B, 0, col) == Color::Orange);  // received from L
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
    const Cube c = Cube::solved().apply(Move{Face::F, 1});

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        REQUIRE(c.at(Face::R, i, 2) == Color::Yellow);  // received from U
        REQUIRE(c.at(Face::D, 2, i) == Color::Red);     // received from R
        REQUIRE(c.at(Face::L, i, 2) == Color::White);   // received from D
        REQUIRE(c.at(Face::U, 2, i) == Color::Orange);  // received from L
    }
}

TEST_CASE("L turn cycles U/F/D/B's L-adjacent lines in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise L turn (viewed from the left) cycles the L-adjacent
    // stickers U -> F -> D -> B -> U.
    const Cube c = Cube::solved().apply(Move{Face::L, 1});

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
    const Cube c = Cube::solved().apply(Move{Face::D, 1});

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        REQUIRE(c.at(Face::R, 2, i) == Color::Green);   // received from F
        REQUIRE(c.at(Face::B, 2, i) == Color::Red);     // received from R
        REQUIRE(c.at(Face::L, 2, i) == Color::Blue);    // received from B
        REQUIRE(c.at(Face::F, 2, i) == Color::Orange);  // received from L
    }
}

TEST_CASE("B turn cycles U/L/D/R's B-adjacent lines in the expected direction", "[cube]") {
    // Verified by hand against the well-known physical fact that a
    // clockwise B turn (viewed from the back) cycles the B-adjacent
    // stickers U -> L -> D -> R -> U.
    const Cube c = Cube::solved().apply(Move{Face::B, 1});

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        REQUIRE(c.at(Face::L, i, 0) == Color::Yellow);  // received from U
        REQUIRE(c.at(Face::D, 0, i) == Color::Orange);  // received from L
        REQUIRE(c.at(Face::R, i, 0) == Color::White);   // received from D
        REQUIRE(c.at(Face::U, 0, i) == Color::Red);     // received from R
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
        scrambled = scrambled.apply(Move{kAllFaces[face_dist(rng)], 1 + static_cast<int>(i % 3)});
    }

    for (const Cube& start : {Cube::solved(), scrambled}) {
        for (Face face : kAllFaces) {
            const Cube quarter = start.apply(Move{face, 1});
            REQUIRE(start.apply(Move{face, 2}) == quarter.apply(Move{face, 1}));
            REQUIRE(start.apply(Move{face, 3}) == quarter.apply(Move{face, 1}).apply(Move{face, 1}));
        }
    }
}

TEST_CASE("random move sequences stay physically valid at every step", "[cube]") {
    std::mt19937 rng{987654};
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};

    Cube c = Cube::solved();
    for (int i = 0; i < 200; ++i) {
        c = c.apply(Move{kAllFaces[face_dist(rng)], turn_dist(rng)});
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
             Case{Color::Yellow, Color::Green, Face::U, Face::F},
             Case{Color::White, Color::Red, Face::D, Face::R},
             Case{Color::Green, Color::Red, Face::F, Face::R},
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
    // Before R: the White/Red edge sits at DR (White on D, Red on R). A
    // single R turn carries the DR position to FR (see the golden R-turn
    // test above: R's layer stays on R, and D's content becomes F's), with
    // White now on F and Red still on R.
    const Cube c = Cube::solved().apply(Move{Face::R, 1});
    const auto loc = c.locate_edge(Color::White, Color::Red);
    REQUIRE(loc.face_a == Face::F);
    REQUIRE(loc.face_b == Face::R);
}

TEST_CASE("locate_corner finds a solved cube's corners on all three of their faces", "[cube]") {
    const Cube solved = Cube::solved();
    const auto loc = solved.locate_corner(Color::Yellow, Color::Green, Color::Red);
    REQUIRE(loc.face_a == Face::U);
    REQUIRE(loc.face_b == Face::F);
    REQUIRE(loc.face_c == Face::R);
    REQUIRE(solved.at(loc.face_a, loc.index_a) == Color::Yellow);
    REQUIRE(solved.at(loc.face_b, loc.index_b) == Color::Green);
    REQUIRE(solved.at(loc.face_c, loc.index_c) == Color::Red);
}
