#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>

#include <optional>
#include <vector>

namespace cube3d {

// A cube of any size and the moves to play on it, for the renderer to show.
// Either a real solution to a scrambled cube (--solve) or random layer turns.
// The cube changes only when a move is committed, so what is on screen is the
// cube as it stands and the move in hand is the one about to be made.
class Player : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("made in C++")

    Q_PROPERTY(int size READ size CONSTANT)
    Q_PROPERTY(int moveCount READ moveCount CONSTANT)
    Q_PROPERTY(int moveIndex READ moveIndex NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    // `solve`: scramble the cube and play its solution (all of it when `moves` is
    // 0, else the first `moves`); otherwise play `moves` random layer turns.
    Player(int size, unsigned seed, int moves, bool solve, QObject* parent = nullptr);

    [[nodiscard]] int size() const noexcept { return cube_.size(); }
    [[nodiscard]] int moveCount() const noexcept { return static_cast<int>(moves_.size()); }
    [[nodiscard]] int moveIndex() const noexcept { return index_; }
    [[nodiscard]] int revision() const noexcept { return revision_; }
    [[nodiscard]] const cube::nxn::NCube& cube() const noexcept { return cube_; }

    // The move in hand: axis (0, 1, 2 for x, y, z), layer (counted from the
    // negative end of the axis), degrees about the positive axis in the right-handed
    // sense that Qt Quick 3D turns by, and the notation. Empty at the end.
    Q_INVOKABLE QVariantMap pendingMove() const;
    // Makes the move in hand on the cube and takes the next.
    Q_INVOKABLE void commit();
    // Back to the scrambled cube and the first move.
    Q_INVOKABLE void reset();

    // The colour of a sticker, by facelet number.
    Q_INVOKABLE QColor colourAt(int facelet) const;
    // The cubies that show anything, for a view that builds one object for each
    // (the comparison): [{x, y, z, stickers: [{direction, facelet}]}].
    Q_INVOKABLE QVariantList visibleCubies() const;

signals:
    void changed();

private:
    cube::nxn::NCube start_;
    cube::nxn::NCube cube_;
    std::vector<cube::nxn::LayerMove> moves_;
    int index_ = 0;
    int revision_ = 0;
};

}  // namespace cube3d
