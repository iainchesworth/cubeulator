#pragma once

#include <cube/diagnostics/json.hpp>
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
class Diagnostics;

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
//
// One move looks like something else: the side facing the camera, turned,
// shows exactly what the whole cube rolled over the same way would show. If
// the cube has been seen as it stands, held as shown, since this move was
// asked for, the turn is taken to be the move - that is what was asked, and
// nobody was asked to roll the cube. Without that sighting it decides nothing.
// The move on the side facing the holder is the one the camera never sees.
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
    //   "wrong"    the side in view is the cube after a turn other than the
    //              one asked for: wrongSide is the side that was turned
    //   "unknown"  the side in view is not on the cube as it should stand,
    //              and no single turn explains it
    Q_PROPERTY(QString sight READ sight NOTIFY sightChanged)
    // While sight is "wrong": the side that looks to have been turned, as a
    // side letter. It can be the side the move was on - turned the other way,
    // or too far. Empty otherwise.
    Q_PROPERTY(QString wrongSide READ wrongSide NOTIFY sightChanged)

public:
    // A side has to be seen the same way in this many frames running before
    // it is acted on: about a quarter of a second at camera rate, the same
    // as a side has to be held to count in a scan. Long enough that a side
    // glimpsed square-on while the cube is being handled changes nothing;
    // short enough that showing one on purpose feels immediate.
    static constexpr int kFramesToTrust = 8;
    // Saying that something is wrong takes more than saying that it is
    // right. A wrong turn has to be seen for this long...
    static constexpr int kFramesToAccuse = 20;
    // ...and a side that fits nothing for this long, a full second: a cube
    // being handled passes through such frames all the time. Both are only
    // ever said of a side seen whole - all nine stickers found - with at
    // least two of them plainly the wrong colour. One sticker out is as
    // likely a glint or a fingertip as a mistake, and is not called one.
    static constexpr int kFramesToDoubt = 30;

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
    [[nodiscard]] QString wrongSide() const;

    // The developer diagnostics channel (docs/diagnostics.md), or none. While
    // it is on, each change in what a frame says is written as a `held`
    // record; this replaces the CUBE_HELD_LOG file.
    void setDiagnostics(Diagnostics* diagnostics) noexcept { diagnostics_ = diagnostics; }
    // The hold, whether it has been seen, and what the camera makes of the
    // side in view now, for a snapshot record.
    [[nodiscard]] cube::diagnostics::Json diagnosticState() const;

signals:
    void holdChanged();
    void sightChanged();
    // The move the solution expects next has been made on the real cube.
    // Emitted once per move; whoever listens steps the solution on.
    void moveSeen();

private:
    enum class Sight { None, Matched, Unsure, Wrong, Unknown };
    // What one frame says, before it has been said often enough to act on.
    struct Verdict {
        enum class Kind { Nothing, Held, Moved, Unsure, Wrong, Unknown } kind = Kind::Nothing;
        cube::solver::Hold hold{};
        // For Wrong: the side that was turned.
        cube::solver::Face side = cube::solver::Face::U;

        friend bool operator==(const Verdict&, const Verdict&) = default;
    };
    [[nodiscard]] static Verdict say(Verdict::Kind kind, cube::solver::Hold hold = {},
                                     cube::solver::Face side = cube::solver::Face::U) {
        return Verdict{kind, hold, side};
    }

    void onFrameWatched();
    void onSolveChanged();
    [[nodiscard]] Verdict judge(const cube::vision::SideRead& read,
                                const cube::vision::SessionColours& colours) const;
    // The one of `holds` to take when the side in view could be held several
    // ways: the hold already shown if it is among them, else the one that
    // keeps the same side on top, else none.
    [[nodiscard]] std::optional<cube::solver::Hold> choose(
        const std::vector<cube::solver::Hold>& holds) const;
    // The one side whose turning - by any amount but the one asked for -
    // would show the camera this read with the cube held as it is.
    [[nodiscard]] std::optional<cube::solver::Face> side_turned_instead(
        const cube::vision::SideRead& read, const cube::vision::SessionColours& colours,
        float unmoved_distance) const;
    // True when the read is the cube part-way through a half turn that was
    // asked for: one quarter of it made, which is progress and not a mistake.
    [[nodiscard]] bool half_way_there(const cube::vision::SideRead& read,
                                      const cube::vision::SessionColours& colours) const;
    // Writes what was just made of a frame to the diagnostics channel, when it
    // is on: the verdict and the numbers it came from. No picture is kept.
    void note(const cube::vision::SideRead& read, const cube::vision::SessionColours& colours,
              const Verdict& verdict) const;
    void setSight(Sight sight, cube::solver::Face wrong_side = cube::solver::Face::U);
    void forget();

    DirectScan& scan_;
    SolveController& solve_;
    Diagnostics* diagnostics_ = nullptr;

    cube::solver::Hold hold_ = cube::solver::kStatedHold;
    bool seen_ = false;
    Sight sight_ = Sight::None;
    cube::solver::Face wrong_side_ = cube::solver::Face::U;

    Verdict last_;
    int streak_ = 0;
    std::optional<cv::Point2f> previous_centre_;
    // The cube the last verdict was about, and whether its move has already
    // been announced.
    cube::solver::Cube judged_cube_ = cube::solver::Cube::solved();
    bool move_announced_ = false;
    // That cube has been seen, held the way the screen shows it, while its
    // move was the one being asked for.
    bool matched_this_move_ = false;
};

}  // namespace cube::app
