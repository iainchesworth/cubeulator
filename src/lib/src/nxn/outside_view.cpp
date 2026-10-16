#include <cube/nxn/outside_view.hpp>

#include <cstddef>

namespace cube::nxn {

std::vector<Color> outside_view(const NCube& cube, Face face) {
    const int n = cube.size();
    std::vector<Color> stickers;
    stickers.reserve(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) {
            switch (face) {
                case Face::U:
                case Face::F:
                case Face::L:
                    stickers.push_back(cube.at(face, row, col));
                    break;
                case Face::R:
                case Face::B:
                    stickers.push_back(cube.at(face, row, n - 1 - col));
                    break;
                case Face::D:
                    stickers.push_back(cube.at(face, n - 1 - row, col));
                    break;
            }
        }
    }
    return stickers;
}

}  // namespace cube::nxn
