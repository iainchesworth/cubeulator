#include <catch2/catch_test_macros.hpp>

#include <cube/solver/notation.hpp>

using cube::solver::Face;
using cube::solver::Move;
using cube::solver::parse_algorithm;
using cube::solver::parse_move;

TEST_CASE("parse_move accepts plain, prime, and double tokens", "[notation]") {
    const auto plain = parse_move("R");
    REQUIRE(plain.has_value());
    REQUIRE(*plain == Move{Face::R, 1});

    const auto prime = parse_move("R'");
    REQUIRE(prime.has_value());
    REQUIRE(*prime == Move{Face::R, 3});

    const auto half = parse_move("R2");
    REQUIRE(half.has_value());
    REQUIRE(*half == Move{Face::R, 2});
}

TEST_CASE("parse_move accepts every face letter", "[notation]") {
    struct Case {
        char letter;
        Face face;
    };
    for (auto [letter, face] :
         {Case{.letter = 'U', .face = Face::U}, Case{.letter = 'D', .face = Face::D},
          Case{.letter = 'L', .face = Face::L}, Case{.letter = 'R', .face = Face::R},
          Case{.letter = 'F', .face = Face::F}, Case{.letter = 'B', .face = Face::B}}) {
        const auto move = parse_move(std::string_view(&letter, 1));
        REQUIRE(move.has_value());
        REQUIRE(move->face == face);
    }
}

TEST_CASE("parse_move rejects garbage", "[notation]") {
    for (std::string_view token : {"", "X", "R3", "RR", "R''", "2R", "r"}) {
        CAPTURE(token);
        REQUIRE_FALSE(parse_move(token).has_value());
    }
}

TEST_CASE("parse_algorithm splits on whitespace and preserves order", "[notation]") {
    const auto moves = parse_algorithm("F R U R' U' F'");
    REQUIRE(moves.has_value());
    REQUIRE(*moves == std::vector<Move>{
                          Move{Face::F, 1},
                          Move{Face::R, 1},
                          Move{Face::U, 1},
                          Move{Face::R, 3},
                          Move{Face::U, 3},
                          Move{Face::F, 3},
                      });
}

TEST_CASE("parse_algorithm tolerates repeated/leading/trailing whitespace", "[notation]") {
    const auto moves = parse_algorithm("  R   U'  ");
    REQUIRE(moves.has_value());
    REQUIRE(*moves == std::vector<Move>{Move{Face::R, 1}, Move{Face::U, 3}});
}

TEST_CASE("parse_algorithm propagates the first invalid token's error", "[notation]") {
    const auto moves = parse_algorithm("R U X F");
    REQUIRE_FALSE(moves.has_value());
    REQUIRE(moves.error() == cube::CubeError::invalid_move_sequence);
}

TEST_CASE("to_string round-trips through parse_move for every valid move", "[notation]") {
    for (Face face : cube::solver::kAllFaces) {
        for (int turns = 1; turns <= 3; ++turns) {
            const Move move{.face = face, .turns = turns};
            const auto parsed = parse_move(cube::solver::to_string(move));
            REQUIRE(parsed.has_value());
            REQUIRE(*parsed == move);
        }
    }
}

TEST_CASE("to_string joins an algorithm with single spaces", "[notation]") {
    const std::vector<Move> moves{Move{.face = Face::R, .turns = 1},
                                  Move{.face = Face::U, .turns = 3},
                                  Move{.face = Face::F, .turns = 2}};
    REQUIRE(cube::solver::to_string(moves) == "R U' F2");
}

TEST_CASE("inverse reverses order and inverts each move", "[notation]") {
    const std::vector<Move> moves{Move{.face = Face::F, .turns = 1},
                                  Move{.face = Face::R, .turns = 1},
                                  Move{.face = Face::U, .turns = 1}};
    const std::vector<Move> expected{Move{.face = Face::U, .turns = 3},
                                     Move{.face = Face::R, .turns = 3},
                                     Move{.face = Face::F, .turns = 3}};
    REQUIRE(cube::solver::inverse(moves) == expected);
}

TEST_CASE("applying inverse(moves) after moves round-trips a cube", "[notation]") {
    const auto moves = parse_algorithm("R U R' U' F' D2 L B'");
    REQUIRE(moves.has_value());

    cube::solver::Cube c = cube::solver::Cube::solved();
    for (const Move& m : *moves) {
        c = c.apply(m);
    }
    for (const Move& m : cube::solver::inverse(*moves)) {
        c = c.apply(m);
    }
    REQUIRE(c == cube::solver::Cube::solved());
}
