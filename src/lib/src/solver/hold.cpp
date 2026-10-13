#include <cube/solver/hold.hpp>

namespace cube::solver {

namespace {

// A side's nine stickers turned a quarter turn anticlockwise: its right-hand
// column becomes its top row.
[[nodiscard]] std::array<Color, 9> turned_anticlockwise(const std::array<Color, 9>& side) noexcept {
    std::array<Color, 9> turned{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            turned[(row * 3) + column] = side[(column * 3) + (2 - row)];
        }
    }
    return turned;
}

}  // namespace

std::array<Color, 9> camera_view(const Cube& cube, const Hold& hold) noexcept {
    const Face side = side_at(hold, Place::Back);
    std::array<Color, 9> view = outside_view(cube, side);

    // outside_view has top_of(side) along the top. Going clockwise round the
    // side as its viewer sees it, the neighbours run top, right, bottom,
    // left; the one that is the holder's top has to be turned up to the top,
    // which is one anticlockwise quarter turn for each place round it is.
    const std::array<Face, 4> clockwise = {top_of(side), right_of(side), opposite(top_of(side)),
                                           opposite(right_of(side))};
    for (const Face neighbour : clockwise) {
        if (neighbour == hold.up) {
            break;
        }
        view = turned_anticlockwise(view);
    }
    return view;
}

}  // namespace cube::solver
