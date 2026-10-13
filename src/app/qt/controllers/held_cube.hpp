#pragma once

#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/vision/held_cube.hpp>

#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QVariantMap>

#include <opencv2/core.hpp>

#include <optional>
#include <vector>

class SolveController;

namespace cube::app {

class DirectScan;

// How the cube is being held while it is solved, for the solve screen.
//
// A move names a side of the cube; where that side is in the hand depends on
// how the cube is held. The solve screen draws its cube turned the same way
// and words each move from here, so "the side on your left" is the side on
// the left.
//
// Until the camera says otherwise the cube is taken to be held the way the
// screen asks: yellow on top, green facing you. The camera says otherwise
// when a side is held square to it: that side's nine colours, compared with
// the cube as it should now stand, show which side it is and which way up
// (cube/vision/held_cube.hpp). Between such reads nothing is tracked - the
// last hold seen stands until the next one.
//
// The same read can show that the move asked for has been made: the side in
// view is not on the cube as it stood, and is on the cube as it stands after
// that move. moveSeen says so, once, and the solve screen steps on.
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
    // True once the camera has seen how the cube is held during this solve.
    // Until then the hold is the one the screen asks for, not one observed.
    Q_PROPERTY(bool seen READ seen NOTIFY holdChanged)

    // What the camera makes of the cube in the frame just looked at:
    //   "none"     no side is square to the camera
    //   "matched"  the side in view is on the cube as it should stand, held
    //              the way the screen shows it
    //   "unsure"   the side in view is on the cube, but it could be held
    //              more than one way to show it
    //   "unknown"  the side in view is not on the cube as it should stand
    Q_PROPERTY(QString sight READ sight NOTIFY sightChanged)

public:
    // A side has to be seen the same way in this many frames running before
    // it changes the hold: a few frames at camera rate, about a sixth of a
    // second, so a blurred frame on the way to a side never counts.
    static constexpr int kFramesToTrust = 5;
    // A side that fits nothing is only called unknown after this long: a
    // cube being turned in the hands passes through such frames all the time.
    static constexpr int kFramesToDoubt = 12;

    HeldCube(DirectScan& scan, SolveController& solve, QObject* parent = nullptr);

    [[nodiscard]] cube::solver::Hold hold() const noexcept { return hold_; }
    // Ignored unless the hold is one a cube can be in.
    void setHold(const cube::solver::Hold& hold);

    [[nodiscard]] QString up() const;
    [[nodiscard]] QString front() const;
    [[nodiscard]] QVariantMap places() const;
    [[nodiscard]] QQuaternion rotation() const;
    [[nodiscard]] bool seen() const noexcept { return seen_; }
    [[nodiscard]] QString sight() const;

signals:
    void holdChanged();
    void sightChanged();
    // The move the solution expects next has been made on the real cube.
    // Emitted once per move; whoever listens steps the solution on.
    void moveSeen();

private:
    enum class Sight { None, Matched, Unsure, Unknown };
    // What one frame says, before it has been said often enough to act on.
    struct Verdict {
        enum class Kind { Nothing, Held, Moved, Unsure, Unknown } kind = Kind::Nothing;
        cube::solver::Hold hold{};

        friend bool operator==(const Verdict&, const Verdict&) = default;
    };

    void onFrameWatched();
    void onSolveChanged();
    [[nodiscard]] Verdict judge(const cube::vision::SideRead& read,
                                const cube::vision::SessionColours& colours) const;
    // The one of `holds` to take when the side in view could be held several
    // ways: the hold already shown if it is among them, else the one that
    // keeps the same side on top, else none.
    [[nodiscard]] std::optional<cube::solver::Hold> choose(
        const std::vector<cube::solver::Hold>& holds) const;
    void setSight(Sight sight);
    void forget();

    DirectScan& scan_;
    SolveController& solve_;

    cube::solver::Hold hold_ = cube::solver::kStatedHold;
    bool seen_ = false;
    Sight sight_ = Sight::None;

    Verdict last_;
    int streak_ = 0;
    std::optional<cv::Point2f> previous_centre_;
    // The cube the last verdict was about, and whether its move has already
    // been announced.
    cube::solver::Cube judged_cube_ = cube::solver::Cube::solved();
    bool move_announced_ = false;
};

}  // namespace cube::app
