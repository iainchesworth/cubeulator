#include <cube/solver/corners.hpp>

#include <array>

#include <cube/profiling.hpp>
#include <cube/solver/cross.hpp>

#include "bfs_search.hpp"

namespace cube::solver {

namespace {

struct CornerSlot {
    int d_index;
    Face side_a;
    int side_a_index;
    Face side_b;
    int side_b_index;
};

// D's corner index is (row=z+1)*3+(col=x+1); each side face's D-adjacent
// corner index is (row=1-y)*3+col with y=-1, i.e. always row 2 - see
// cube.cpp's layout derivation.
constexpr std::array<CornerSlot, 4> kCornerSlots = {{
    {.d_index = 6,
     .side_a = Face::F,
     .side_a_index = 6,
     .side_b = Face::L,
     .side_b_index = 8},  // DFL
    {.d_index = 8,
     .side_a = Face::F,
     .side_a_index = 8,
     .side_b = Face::R,
     .side_b_index = 8},  // DFR
    {.d_index = 0,
     .side_a = Face::B,
     .side_a_index = 6,
     .side_b = Face::L,
     .side_b_index = 6},  // DBL
    {.d_index = 2,
     .side_a = Face::B,
     .side_a_index = 8,
     .side_b = Face::R,
     .side_b_index = 6},  // DBR
}};

bool slot_solved(const Cube& cube, const CornerSlot& slot) noexcept {
    return cube.at(Face::D, slot.d_index) == Color::White &&
           cube.at(slot.side_a, slot.side_a_index) == canonical_color(slot.side_a) &&
           cube.at(slot.side_b, slot.side_b_index) == canonical_color(slot.side_b);
}

}  // namespace

bool first_layer_solved(const Cube& cube) noexcept {
    if (!cross_solved(cube)) {
        return false;
    }
    for (const CornerSlot& slot : kCornerSlots) {
        if (!slot_solved(cube, slot)) {
            return false;
        }
    }
    return true;
}

Result<std::vector<Move>> solve_corners(const Cube& start) {
    CUBE_ZONE_SCOPED_N("solver.corners");
    std::vector<Move> solution;
    Cube cube = start;

    for (std::size_t i = 0; i < kCornerSlots.size(); ++i) {
        const CornerSlot& slot = kCornerSlots.at(i);

        auto postcondition = [&](const Cube& c) {
            if (!cross_solved(c)) {
                return false;
            }
            for (std::size_t j = 0; j <= i; ++j) {
                if (!slot_solved(c, kCornerSlots.at(j))) {
                    return false;
                }
            }
            return true;
        };
        auto signature = [&](const Cube& c) {
            std::uint64_t rest = 0;
            for (std::size_t j = 0; j <= i; ++j) {
                const CornerSlot& s = kCornerSlots.at(j);
                rest = detail::push_code(
                    rest, detail::piece_code_corner(c, Color::White, canonical_color(s.side_a),
                                                    canonical_color(s.side_b)));
            }
            return detail::SignatureKey{.cross = detail::cross_key(c), .rest = rest};
        };

        const std::vector<Face> allowed = {Face::U, Face::D, slot.side_a, slot.side_b};
        Result<std::vector<Move>> step =
            detail::bfs_search(cube, allowed, postcondition, signature, 10);
        if (!step.has_value()) {
            return std::unexpected(step.error());
        }
        for (const Move& m : *step) {
            cube = cube.apply(m);
            solution.push_back(m);
        }
    }

    return solution;
}

}  // namespace cube::solver
