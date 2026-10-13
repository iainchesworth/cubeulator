#include <cube/solver/outside_view.hpp>

#include <cstddef>
#include <utility>

namespace cube::solver {

std::array<Color, 9> outside_view(const Cube& cube, Face face) noexcept {
    std::array<Color, 9> stickers = cube.face(face);
    switch (face) {
        case Face::U:
        case Face::F:
        case Face::L:
            break;
        case Face::R:
        case Face::B:
            for (std::size_t row = 0; row < 3; ++row) {
                std::swap(stickers.at(row * 3), stickers.at((row * 3) + 2));
            }
            break;
        case Face::D:
            for (std::size_t column = 0; column < 3; ++column) {
                std::swap(stickers.at(column), stickers.at(6 + column));
            }
            break;
    }
    return stickers;
}

}  // namespace cube::solver
