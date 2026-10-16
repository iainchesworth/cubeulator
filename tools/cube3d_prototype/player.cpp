#include "player.hpp"

#include "cube_mesh.hpp"

#include <cube/nxn/strategy.hpp>

#include <map>
#include <random>
#include <tuple>
#include <utility>

namespace cube3d {

namespace {

using cube::nxn::LayerMove;
using cube::nxn::NCube;

[[nodiscard]] NCube scrambled(int size, unsigned seed) {
    return cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 60 + (10 * size)});
}

}  // namespace

Player::Player(int size, unsigned seed, int moves, bool solve, QObject* parent)
    : QObject(parent), start_(scrambled(size, seed)), cube_(start_) {
    if (solve) {
        const auto solution = cube::nxn::solve(start_);
        if (solution.has_value()) {
            moves_ = solution->moves();
        }
        if (moves > 0 && moves_.size() > static_cast<std::size_t>(moves)) {
            moves_.resize(static_cast<std::size_t>(moves));
        }
        return;
    }
    // Random turns, from the same sequence on every platform.
    std::mt19937 rng{seed + 1000U};
    for (int i = 0; i < moves; ++i) {
        moves_.push_back(LayerMove{
            .face = cube::solver::kAllFaces.at(rng() % 6U),
            .depth = static_cast<int>(rng() % static_cast<unsigned>(size)),
            .turns = 1 + static_cast<int>(rng() % 3U),
        });
    }
}

QVariantMap Player::pendingMove() const {
    if (index_ >= moveCount()) {
        return {};
    }
    const LayerMove move = moves_.at(static_cast<std::size_t>(index_));
    const cube::nxn::SliceTurn turn = cube::nxn::to_slice_turn(move, size());
    // Clockwise seen from the positive end is minus a quarter turn about the
    // positive axis, right-handed; three of them is one the other way.
    const int degrees = turn.turns == 3 ? 90 : -90 * turn.turns;
    return {
        {QStringLiteral("axis"), static_cast<int>(turn.axis)},
        {QStringLiteral("layer"), turn.layer},
        {QStringLiteral("degrees"), degrees},
    };
}

void Player::commit() {
    if (index_ >= moveCount()) {
        return;
    }
    cube_.apply(moves_.at(static_cast<std::size_t>(index_)));
    ++index_;
    ++revision_;
    emit changed();
}

void Player::reset() {
    cube_ = start_;
    index_ = 0;
    ++revision_;
    emit changed();
}

QColor Player::colourAt(int facelet) const {
    static const Palette palette = default_palette();
    const cube::solver::Color colour = cube_.facelet(static_cast<std::size_t>(facelet));
    const Rgba& c = palette.sticker.at(static_cast<std::size_t>(colour));
    return QColor::fromRgbF(c.r, c.g, c.b);
}

QVariantList Player::visibleCubies() const {
    const int n = size();
    struct Key {
        int x, y, z;
        bool operator<(const Key& o) const {
            return std::tie(x, y, z) < std::tie(o.x, o.y, o.z);
        }
    };
    std::map<Key, QVariantList> stickers;
    for (const cube::solver::Face face : cube::solver::kAllFaces) {
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                const StickerPlace at = place_of(n, face, row, col);
                stickers[{at.cubie.at(0), at.cubie.at(1), at.cubie.at(2)}].append(QVariantMap{
                    {QStringLiteral("direction"), at.direction},
                    {QStringLiteral("facelet"), static_cast<int>(cube_.geometry().index(face, row, col))},
                });
            }
        }
    }
    QVariantList cubies;
    for (const auto& [key, list] : stickers) {
        cubies.append(QVariantMap{
            {QStringLiteral("x"), key.x},
            {QStringLiteral("y"), key.y},
            {QStringLiteral("z"), key.z},
            {QStringLiteral("stickers"), list},
        });
    }
    return cubies;
}

}  // namespace cube3d
