#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/notation.hpp>
#include <cube/nxn/strategy.hpp>

#include "../deterministic_rng.hpp"

namespace {

using cube::nxn::Axis;
using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::nxn::ParseFault;
using cube::nxn::WideMove;
using cube::solver::Face;
using Kind = cube::nxn::ParseFault::Kind;

constexpr std::array<int, 8> kSizes = {2, 3, 4, 5, 6, 7, 8, 9};

NCube scrambled(int size, unsigned seed) {
    return cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 40 + (6 * size)});
}

// What a list of moves does to a cube.
NCube after(NCube cube, const std::vector<LayerMove>& moves) {
    cube.apply(moves);
    return cube;
}

WideMove one(std::string_view text, int size) {
    const auto moves = cube::nxn::parse(text, size);
    REQUIRE(moves.has_value());
    REQUIRE(moves->size() == 1);
    return moves->front();
}

// Every block of every size, in every way round: each face, each pair of
// layers, each count of turns.
std::vector<WideMove> every_block(int size) {
    std::vector<WideMove> blocks;
    for (const Face face : cube::solver::kAllFaces) {
        for (int first = 0; first < size; ++first) {
            for (int last = first; last < size; ++last) {
                for (int turns = 1; turns <= 3; ++turns) {
                    blocks.push_back(
                        WideMove{.face = face, .first = first, .last = last, .turns = turns});
                }
            }
        }
    }
    return blocks;
}

WideMove wide(Face face, int first, int last, int turns) {
    return WideMove{.face = face, .first = first, .last = last, .turns = turns};
}

LayerMove layer(Face face, int depth, int turns) {
    return LayerMove{.face = face, .depth = depth, .turns = turns};
}

bool is_whole_axis(const WideMove& move, int size) {
    return move.first == 0 && move.last == size - 1;
}

}  // namespace

TEST_CASE("notation reads the names a person types", "[nxn][notation]") {
    struct Case {
        std::string_view text;
        int size;
        WideMove expected;
    };
    const std::array<Case, 24> cases = {
        {
            {.text = "R", .size = 3, .expected = wide(Face::R, 0, 0, 1)},
            {.text = "R'", .size = 3, .expected = wide(Face::R, 0, 0, 3)},
            {.text = "R2", .size = 3, .expected = wide(Face::R, 0, 0, 2)},
            {.text = "R2'", .size = 3, .expected = wide(Face::R, 0, 0, 2)},
            {.text = "Rw", .size = 4, .expected = wide(Face::R, 0, 1, 1)},
            {.text = "Rw'", .size = 4, .expected = wide(Face::R, 0, 1, 3)},
            {.text = "Uw2", .size = 5, .expected = wide(Face::U, 0, 1, 2)},
            {.text = "3Rw", .size = 5, .expected = wide(Face::R, 0, 2, 1)},
            {.text = "4Fw'", .size = 7, .expected = wide(Face::F, 0, 3, 3)},
            {.text = "2R", .size = 5, .expected = wide(Face::R, 1, 1, 1)},
            {.text = "3L2", .size = 5, .expected = wide(Face::L, 2, 2, 2)},
            {.text = "1R", .size = 5, .expected = wide(Face::R, 0, 0, 1)},
            {.text = "1Rw", .size = 5, .expected = wide(Face::R, 0, 0, 1)},
            {.text = "2-3R", .size = 6, .expected = wide(Face::R, 1, 2, 1)},
            {.text = "2-4D'", .size = 6, .expected = wide(Face::D, 1, 3, 3)},
            {.text = "3-3B", .size = 5, .expected = wide(Face::B, 2, 2, 1)},
            {.text = "x", .size = 4, .expected = wide(Face::R, 0, 3, 1)},
            {.text = "y'", .size = 4, .expected = wide(Face::U, 0, 3, 3)},
            {.text = "z2", .size = 3, .expected = wide(Face::F, 0, 2, 2)},
            {.text = "r", .size = 4, .expected = wide(Face::R, 0, 1, 1)},
            {.text = "u'", .size = 5, .expected = wide(Face::U, 0, 1, 3)},
            {.text = "5Rw", .size = 5, .expected = wide(Face::R, 0, 4, 1)},
            {.text = "Rw", .size = 2, .expected = wide(Face::R, 0, 1, 1)},
            {.text = "1-2R", .size = 2, .expected = wide(Face::R, 0, 1, 1)},
        },
    };
    for (const auto& [text, size, expected] : cases) {
        INFO("text " << text << " size " << size);
        CHECK(one(text, size) == expected);
    }
}

TEST_CASE("notation reads sequences, whatever separates the moves", "[nxn][notation]") {
    const auto moves = cube::nxn::parse("R  U\tR'\n U' 3Rw2 \r\n  x", 5);
    REQUIRE(moves.has_value());
    CHECK(moves->size() == 6);
    CHECK(cube::nxn::parse("", 5)->empty());
    CHECK(cube::nxn::parse("  \n\t ", 5)->empty());
}

TEST_CASE("notation refuses what is not a move, and says where", "[nxn][notation]") {
    struct Case {
        std::string_view text;
        int size;
        ParseFault expected;
    };
    const std::array<Case, 24> cases = {
        {
            {
                .text = "Q",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 1},
            },
            {
                .text = "R U Q U",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 4, .length = 1},
            },
            {
                .text = "R3",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 2},
            },
            {
                .text = "R''",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 3},
            },
            {
                .text = "R'2",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 3},
            },
            {
                .text = "RR",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 2},
            },
            {
                .text = "Rww",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 3},
            },
            {
                .text = "Rw3",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 3},
            },
            {
                .text = "2",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 1},
            },
            {
                .text = "2-",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 2},
            },
            {
                .text = "-2R",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 3},
            },
            {
                .text = "2-3Rw",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 5},
            },
            {
                .text = "2x",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 2},
            },
            {
                .text = "2r",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 2},
            },
            {
                .text = "xw",
                .size = 5,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 2},
            },
            {
                .text = "M",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 1},
            },
            {
                .text = "R,U",
                .size = 3,
                .expected = {.kind = Kind::UnknownMove, .offset = 0, .length = 3},
            },
            {
                .text = "5R",
                .size = 4,
                .expected = {.kind = Kind::LayerOutOfRange, .offset = 0, .length = 2},
            },
            {
                .text = "0R",
                .size = 4,
                .expected = {.kind = Kind::LayerOutOfRange, .offset = 0, .length = 2},
            },
            {
                .text = "4Rw",
                .size = 3,
                .expected = {.kind = Kind::LayerOutOfRange, .offset = 0, .length = 3},
            },
            {
                .text = "R 3-2R",
                .size = 5,
                .expected = {.kind = Kind::LayerOutOfRange, .offset = 2, .length = 4},
            },
            {
                .text = "2-6R",
                .size = 5,
                .expected = {.kind = Kind::LayerOutOfRange, .offset = 0, .length = 4},
            },
            {
                .text = "99999R",
                .size = 9,
                .expected = {.kind = Kind::LayerOutOfRange, .offset = 0, .length = 6},
            },
            {
                .text = "3Rw",
                .size = 2,
                .expected = {.kind = Kind::LayerOutOfRange, .offset = 0, .length = 3},
            },
        },
    };
    for (const auto& [text, size, expected] : cases) {
        INFO("text " << text << " size " << size);
        const auto moves = cube::nxn::parse(text, size);
        REQUIRE_FALSE(moves.has_value());
        CHECK(moves.error() == expected);
    }
    CHECK(cube::nxn::parse("R", 1).error().kind == Kind::UnsupportedSize);
    CHECK(cube::nxn::parse("R", 10).error().kind == Kind::UnsupportedSize);
}

TEST_CASE("notation prints the forms a person writes", "[nxn][notation]") {
    struct Case {
        WideMove move;
        int size;
        std::string_view text;
    };
    const std::array<Case, 20> cases = {
        {
            {.move = wide(Face::R, 0, 0, 1), .size = 3, .text = "R"},
            {.move = wide(Face::R, 0, 0, 2), .size = 3, .text = "R2"},
            {.move = wide(Face::R, 0, 0, 3), .size = 3, .text = "R'"},
            {.move = wide(Face::R, 0, 1, 1), .size = 4, .text = "Rw"},
            {.move = wide(Face::L, 0, 2, 3), .size = 5, .text = "3Lw'"},
            {.move = wide(Face::U, 0, 3, 2), .size = 7, .text = "4Uw2"},
            {.move = wide(Face::R, 1, 1, 1), .size = 5, .text = "2R"},
            {.move = wide(Face::F, 3, 3, 3), .size = 7, .text = "4F'"},
            {.move = wide(Face::D, 1, 2, 1), .size = 6, .text = "2-3D"},
            {.move = wide(Face::B, 2, 4, 2), .size = 7, .text = "3-5B2"},
            {.move = wide(Face::R, 0, 2, 1), .size = 3, .text = "x"},
            {.move = wide(Face::U, 0, 3, 3), .size = 4, .text = "y'"},
            {.move = wide(Face::F, 0, 4, 2), .size = 5, .text = "z2"},
            {.move = wide(Face::L, 0, 2, 1), .size = 3, .text = "x'"},
            {.move = wide(Face::D, 0, 3, 3), .size = 4, .text = "y"},
            {.move = wide(Face::B, 0, 4, 1), .size = 5, .text = "z'"},
            {.move = wide(Face::R, 0, 1, 1), .size = 2, .text = "x"},
            {.move = wide(Face::R, 0, 0, 1), .size = 2, .text = "R"},
            {.move = wide(Face::R, 1, 1, 1), .size = 2, .text = "2R"},
            {.move = wide(Face::R, 0, 7, 3), .size = 9, .text = "8Rw'"},
        },
    };
    for (const auto& [move, size, text] : cases) {
        INFO("size " << size << " text " << text);
        CHECK(cube::nxn::to_string(move, size) == text);
    }
    const std::vector<WideMove> sequence = {
        wide(Face::R, 0, 0, 1),
        wide(Face::U, 0, 0, 3),
        wide(Face::R, 0, 1, 2),
        wide(Face::F, 2, 2, 1),
    };
    CHECK(cube::nxn::to_string(std::span<const WideMove>{sequence}, 5) == "R U' Rw2 3F");
    const std::vector<LayerMove> layers = {
        layer(Face::R, 0, 1),
        layer(Face::R, 1, 1),
        layer(Face::U, 2, 3),
    };
    CHECK(cube::nxn::to_string(std::span<const LayerMove>{layers}, 5) == "R 2R 3U'");
}

TEST_CASE("every block of every size prints and reads back", "[nxn][notation]") {
    for (const int size : kSizes) {
        for (const WideMove& block : every_block(size)) {
            const std::string text = cube::nxn::to_string(block, size);
            INFO("size " << size << " text " << text);
            const auto moves = cube::nxn::parse(text, size);
            REQUIRE(moves.has_value());
            REQUIRE(moves->size() == 1);
            // A block that is every layer of an axis is a turn of the whole
            // cube, which is named for the positive face: the same turn,
            // spelled that way. Any other block comes back as itself.
            if (is_whole_axis(block, size)) {
                CHECK(moves->front() == cube::nxn::canonical(block, size));
            } else {
                CHECK(moves->front() == block);
            }
        }
    }
}

TEST_CASE("a sequence prints and reads back", "[nxn][notation]") {
    auto rng = cube::test::deterministic_rng(31U);
    for (const int size : kSizes) {
        const std::vector<WideMove> blocks = every_block(size);
        std::vector<WideMove> sequence;
        sequence.reserve(60);
        for (int i = 0; i < 60; ++i) {
            sequence.push_back(blocks.at(rng() % blocks.size()));
        }
        const std::string text = cube::nxn::to_string(std::span<const WideMove>{sequence}, size);
        const auto read = cube::nxn::parse(text, size);
        INFO("size " << size << " text " << text);
        REQUIRE(read.has_value());
        REQUIRE(read->size() == sequence.size());
        for (std::size_t i = 0; i < sequence.size(); ++i) {
            const WideMove& wrote = sequence.at(i);
            CHECK(read->at(i) ==
                  (is_whole_axis(wrote, size) ? cube::nxn::canonical(wrote, size) : wrote));
        }
    }
}

TEST_CASE("a name turns the layers it names, the way R turns", "[nxn][notation]") {
    // Rw is R and 2R; 3Rw is those and 3R: layers 0 to n-1 from the R face.
    NCube expected = *NCube::solved(5);
    expected.apply(LayerMove{.face = Face::R, .depth = 0, .turns = 1});
    expected.apply(LayerMove{.face = Face::R, .depth = 1, .turns = 1});
    expected.apply(LayerMove{.face = Face::R, .depth = 2, .turns = 1});
    NCube actual = *NCube::solved(5);
    actual.apply(cube::nxn::layers_of(one("3Rw", 5)));
    CHECK(actual == expected);

    // x is R, the middle layer the way R turns it (M'), and L'.
    NCube rotated = *NCube::solved(3);
    rotated.apply(cube::nxn::layers_of(one("x", 3)));
    NCube by_hand = *NCube::solved(3);
    by_hand.apply(LayerMove{.face = Face::R, .depth = 0, .turns = 1});
    by_hand.apply(LayerMove{.face = Face::R, .depth = 1, .turns = 1});
    by_hand.apply(LayerMove{.face = Face::L, .depth = 0, .turns = 3});
    CHECK(rotated == by_hand);

    // The same layer from the other end is the same layer, turned the other way
    // round: 2R on a 4x4 is the layer 3L, which the L face turns as L'.
    NCube from_right = *NCube::solved(4);
    from_right.apply(cube::nxn::layers_of(one("2R", 4)));
    NCube from_left = *NCube::solved(4);
    from_left.apply(cube::nxn::layers_of(one("3L'", 4)));
    CHECK(from_right == from_left);

    // A wide move is its layers: R2 on a face turned twice, not R then R'.
    NCube half = *NCube::solved(4);
    half.apply(cube::nxn::layers_of(one("Rw2", 4)));
    NCube twice = *NCube::solved(4);
    for (int i = 0; i < 2; ++i) {
        twice.apply(cube::nxn::layers_of(one("Rw", 4)));
    }
    CHECK(half == twice);
    CHECK_FALSE(half.is_solved());
}

TEST_CASE("the same turn spelled from the other end is the same turn", "[nxn][notation]") {
    for (const int size : kSizes) {
        const NCube start = scrambled(size, 5U);
        for (const WideMove& block : every_block(size)) {
            const WideMove spelled = cube::nxn::canonical(block, size);
            INFO("size " << size << " block " << cube::nxn::to_string(block, size) << " as "
                         << cube::nxn::to_string(spelled, size));
            CHECK(after(start, cube::nxn::layers_of(block)) ==
                  after(start, cube::nxn::layers_of(spelled)));
            // Once spelled from the nearer end, it stays.
            CHECK(cube::nxn::canonical(spelled, size) == spelled);
            // Nearer, or as near: the block never starts further in than it ends.
            CHECK(spelled.first <= size - 1 - spelled.last);
        }
    }
}

TEST_CASE("the layers of a block group back into that block", "[nxn][notation]") {
    for (const int size : kSizes) {
        for (const WideMove& block : every_block(size)) {
            const std::vector<WideMove> grouped =
                cube::nxn::group(cube::nxn::layers_of(block), size);
            INFO("size " << size << " block " << cube::nxn::to_string(block, size));
            REQUIRE(grouped.size() == 1);
            CHECK(grouped.front() == cube::nxn::canonical(block, size));
        }
    }
}

TEST_CASE("grouping gathers adjacent layers through one angle and nothing else",
          "[nxn][notation]") {
    const auto layers = [](std::string_view text) {
        const std::vector<WideMove> blocks = *cube::nxn::parse(text, 7);
        return cube::nxn::layers_of(blocks);
    };
    const auto grouped = [&](std::string_view text) {
        const std::vector<LayerMove> moves = layers(text);
        return cube::nxn::to_string(cube::nxn::group(moves, 7), 7);
    };

    // R and 2R are Rw. They need not be next to each other, nor in order.
    CHECK(grouped("R 2R") == "Rw");
    CHECK(grouped("2R R") == "Rw");
    CHECK(grouped("R 2R 3R") == "3Rw");
    CHECK(grouped("R 3R 2R") == "3Rw");
    // Not at a face: layers 2 and 3 are a range. Layers 6 and 7 from R are the
    // two outer layers of L, turned the other way round.
    CHECK(grouped("2R 3R") == "2-3R");
    CHECK(grouped("6R 7R") == "Lw'");
    // The angle must match, or they are two moves.
    CHECK(grouped("R 2R'") == "R 2R'");
    CHECK(grouped("R2 2R") == "R2 2R");
    // A layer between them missing makes two moves.
    CHECK(grouped("R 3R") == "R 3R");
    // Another axis between them breaks the run: turns across axes do not commute.
    CHECK(grouped("R U 2R") == "R U 2R");
    // A layer turned twice through the same angle is two moves of it.
    CHECK(grouped("R R") == "R R");
    // Whole-cube turn.
    CHECK(grouped("R 2R 3R 4R 5R 6R 7R") == "x");
    CHECK(grouped("L 2L 3L 4L 5L 6L 7L") == "x'");
}

TEST_CASE("following the grouped moves arrives where the original turns do", "[nxn][notation]") {
    auto rng = cube::test::deterministic_rng(32U);
    for (const int size : kSizes) {
        const NCube start = scrambled(size, 7U);
        for (int round = 0; round < 40; ++round) {
            // Turns drawn from a few layers and axes, so runs of one axis with
            // adjacent layers through one angle are common.
            std::vector<LayerMove> moves;
            const int length = 3 + static_cast<int>(rng() % 30U);
            moves.reserve(static_cast<std::size_t>(length));
            for (int i = 0; i < length; ++i) {
                moves.push_back(LayerMove{
                    .face = cube::solver::kAllFaces.at(rng() % 3U == 0 ? rng() % 6U : rng() % 2U),
                    .depth = static_cast<int>(rng() %
                                              std::min<unsigned>(3U, static_cast<unsigned>(size))),
                    .turns = 1 + static_cast<int>(rng() % 3U),
                });
            }
            const std::vector<WideMove> grouped = cube::nxn::group(moves, size);
            INFO("size " << size << " round " << round << " moves "
                         << cube::nxn::to_string(std::span<const LayerMove>{moves}, size)
                         << " grouped "
                         << cube::nxn::to_string(std::span<const WideMove>{grouped}, size));
            CHECK(after(start, cube::nxn::layers_of(grouped)) == after(start, moves));
            // Same turns, in another order: the same number of layer turns.
            CHECK(cube::nxn::layers_of(grouped).size() == moves.size());
            CHECK(grouped.size() <= moves.size());
        }
    }
}

TEST_CASE("a solution groups into fewer moves that solve the same cube", "[nxn][notation]") {
    for (const int size : {4, 5, 6, 7}) {
        const NCube scramble = scrambled(size, 9U);
        const auto solution = cube::nxn::solve(scramble);
        REQUIRE(solution.has_value());
        const std::vector<LayerMove> moves = solution->moves();
        const std::vector<WideMove> grouped = cube::nxn::group(moves, size);
        INFO("size " << size << ": " << moves.size() << " layer turns, " << grouped.size()
                     << " moves");
        CHECK(grouped.size() <= moves.size());
        CHECK(after(scramble, cube::nxn::layers_of(grouped)).is_solved());
        // What a person is shown reads back as the same turns.
        const auto read =
            cube::nxn::parse(cube::nxn::to_string(std::span<const WideMove>{grouped}, size), size);
        REQUIRE(read.has_value());
        CHECK(after(scramble, cube::nxn::layers_of(*read)).is_solved());
    }
}
