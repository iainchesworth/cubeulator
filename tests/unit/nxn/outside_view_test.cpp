#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/solver/solver.hpp>

namespace {

using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;

}  // namespace

TEST_CASE("at size 3 the outside view is the 3x3's, side for side", "[nxn][view]") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        const cube::solver::Cube cube3 =
            cube::solver::random_scramble(seed, cube::solver::MoveCount{30});
        const NCube cube = NCube::from_cube3(cube3);
        for (const Face face : cube::solver::kAllFaces) {
            const auto theirs = cube::solver::outside_view(cube3, face);
            const std::vector<Color> ours = cube::nxn::outside_view(cube, face);
            INFO("seed " << seed << " face " << static_cast<int>(face));
            CHECK(ours == std::vector<Color>(theirs.begin(), theirs.end()));
        }
    }
}

TEST_CASE("the outside view of a bigger cube mirrors R, B and D as the 3x3's does", "[nxn][view]") {
    for (const int size : {2, 4, 5, 7}) {
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 3U, .moves = 60});
        const auto n = static_cast<std::size_t>(size);
        for (const Face face : cube::solver::kAllFaces) {
            const std::vector<Color> view = cube::nxn::outside_view(cube, face);
            REQUIRE(view.size() == n * n);
            // The same stickers, once each: a view only reorders a side.
            std::vector<Color> sorted_view = view;
            std::vector<Color> sorted_side;
            for (int row = 0; row < size; ++row) {
                for (int col = 0; col < size; ++col) {
                    sorted_side.push_back(cube.at(face, row, col));
                }
            }
            std::ranges::sort(sorted_view);
            std::ranges::sort(sorted_side);
            CHECK(sorted_view == sorted_side);

            for (int row = 0; row < size; ++row) {
                for (int col = 0; col < size; ++col) {
                    const Color seen = view.at((static_cast<std::size_t>(row) * n) +
                                               static_cast<std::size_t>(col));
                    INFO("size " << size << " face " << static_cast<int>(face) << " row " << row
                                 << " col " << col);
                    switch (face) {
                        case Face::U:
                        case Face::F:
                        case Face::L:
                            CHECK(seen == cube.at(face, row, col));
                            break;
                        case Face::R:
                        case Face::B:
                            CHECK(seen == cube.at(face, row, size - 1 - col));
                            break;
                        case Face::D:
                            CHECK(seen == cube.at(face, size - 1 - row, col));
                            break;
                    }
                }
            }
        }
    }
}
