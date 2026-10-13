#pragma once

#include <cube/solver/hold.hpp>

#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QVariantMap>

namespace cube::app {

// How the cube is being held while it is solved, for the solve screen.
//
// A move names a side of the cube; where that side is in the hand depends on
// how the cube is held. The solve screen draws its cube turned the same way
// and words each move from here, so "the side on your left" is the side on
// the left.
//
// Until something says otherwise the cube is taken to be held the way the
// screen asks: yellow on top, green facing you.
class HeldCube : public QObject {
    Q_OBJECT

    // The side on top and the side facing the holder, as side letters
    // ("U", "D", "L", "R", "F", "B").
    Q_PROPERTY(QString up READ up NOTIFY holdChanged)
    Q_PROPERTY(QString front READ front NOTIFY holdChanged)
    // Where every side is for the holder: side letter -> "top", "bottom",
    // "left", "right", "front" or "back".
    Q_PROPERTY(QVariantMap places READ places NOTIFY holdChanged)
    // The cube's orientation, for Cube3DView.holdRotation: identity when
    // held as stated.
    Q_PROPERTY(QQuaternion rotation READ rotation NOTIFY holdChanged)

public:
    explicit HeldCube(QObject* parent = nullptr);

    [[nodiscard]] cube::solver::Hold hold() const noexcept { return hold_; }
    // Ignored unless the hold is one a cube can be in.
    void setHold(const cube::solver::Hold& hold);

    [[nodiscard]] QString up() const;
    [[nodiscard]] QString front() const;
    [[nodiscard]] QVariantMap places() const;
    [[nodiscard]] QQuaternion rotation() const;

signals:
    void holdChanged();

private:
    cube::solver::Hold hold_ = cube::solver::kStatedHold;
};

}  // namespace cube::app
