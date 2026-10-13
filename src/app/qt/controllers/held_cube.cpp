#include "controllers/held_cube.hpp"

#include "controllers/direct_scan.hpp"
#include "solve_controller.hpp"

#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QMatrix3x3>
#include <QTextStream>
#include <QtGlobal>

#include <array>
#include <cstddef>

namespace cube::app {

namespace {

using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::Hold;
using cube::solver::Place;
using cube::vision::HoldReading;

[[nodiscard]] QString side_letter(Face face) {
    switch (face) {
        case Face::U:
            return QStringLiteral("U");
        case Face::D:
            return QStringLiteral("D");
        case Face::L:
            return QStringLiteral("L");
        case Face::R:
            return QStringLiteral("R");
        case Face::F:
            return QStringLiteral("F");
        case Face::B:
            return QStringLiteral("B");
    }
    return QStringLiteral("U");
}

[[nodiscard]] QString place_name(Place place) {
    switch (place) {
        case Place::Top:
            return QStringLiteral("top");
        case Place::Bottom:
            return QStringLiteral("bottom");
        case Place::Left:
            return QStringLiteral("left");
        case Place::Right:
            return QStringLiteral("right");
        case Place::Front:
            return QStringLiteral("front");
        case Place::Back:
            return QStringLiteral("back");
    }
    return QStringLiteral("top");
}

}  // namespace

HeldCube::HeldCube(DirectScan& scan, SolveController& solve, QObject* parent)
    : QObject(parent), scan_(scan), solve_(solve) {
    connect(&scan_, &DirectScan::frameWatched, this, &HeldCube::onFrameWatched);
    connect(&scan_, &DirectScan::runningChanged, this, [this]() {
        if (!scan_.watching()) {
            // Nobody is looking any more. The hold stands; what the camera
            // made of the last frame does not.
            forget();
            matched_this_move_ = false;
            setSight(Sight::None);
        }
    });
    connect(&solve_, &SolveController::stateChanged, this, &HeldCube::onSolveChanged);
    connect(&solve_, &SolveController::solutionChanged, this, [this]() {
        forget();
        matched_this_move_ = false;
        if (solve_.solutionContinuesCube()) {
            // The same cube, still in the same hands: the method changed and
            // the way to solve it with it. How it is held has not, so the
            // hold stands. What was counted towards was about the move the
            // old solution expected, which is not this one.
            move_announced_ = false;
            setSight(Sight::None);
            return;
        }
        // A new cube to solve starts from the hold the screen asks for.
        seen_ = false;
        hold_ = cube::solver::kStatedHold;
        emit holdChanged();
        setSight(Sight::None);
    });
}

void HeldCube::setHold(const Hold& hold) {
    if (!cube::solver::is_valid(hold) || hold == hold_) {
        return;
    }
    hold_ = hold;
    emit holdChanged();
}

QString HeldCube::up() const {
    return side_letter(hold_.up);
}
QString HeldCube::front() const {
    return side_letter(hold_.front);
}

QVariantMap HeldCube::places() const {
    QVariantMap places;
    for (const Face side : cube::solver::kAllFaces) {
        places.insert(side_letter(side), place_name(cube::solver::place_of(hold_, side)));
    }
    return places;
}

QQuaternion HeldCube::rotation() const {
    const std::array<int, 9> turn = cube::solver::rotation(hold_);
    std::array<float, 9> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<float>(turn[i]);
    }
    return QQuaternion::fromRotationMatrix(QMatrix3x3(values.data()));
}

QString HeldCube::sight() const {
    switch (sight_) {
        case Sight::None:
            return QStringLiteral("none");
        case Sight::Matched:
            return QStringLiteral("matched");
        case Sight::Unsure:
            return QStringLiteral("unsure");
        case Sight::Wrong:
            return QStringLiteral("wrong");
        case Sight::Unknown:
            return QStringLiteral("unknown");
    }
    return QStringLiteral("none");
}

QString HeldCube::wrongSide() const {
    return sight_ == Sight::Wrong ? side_letter(wrong_side_) : QString();
}

void HeldCube::setSight(Sight sight, Face wrong_side) {
    if (sight == sight_ && (sight != Sight::Wrong || wrong_side == wrong_side_)) {
        return;
    }
    sight_ = sight;
    wrong_side_ = wrong_side;
    emit sightChanged();
}

std::optional<Face> HeldCube::side_turned_instead(const cube::vision::SideRead& read,
                                                  const cube::vision::SessionColours& colours,
                                                  float unmoved_distance) const {
    // Only with a hold that has been seen: if the cube might be held some
    // other way than shown, a turn and a different hold cannot be told apart.
    if (!seen_) {
        return std::nullopt;
    }
    const Cube& now = solve_.currentCube();
    const auto asked = solve_.currentMove();
    std::optional<Face> turned;
    for (const Face face : cube::solver::kAllFaces) {
        for (int turns = 1; turns <= 3; ++turns) {
            const cube::solver::Move move{face, turns};
            if (asked.has_value() && move == *asked) {
                continue;
            }
            const HoldReading after = cube::vision::read_hold(read, now.apply(move), colours);
            if (!after.shows(hold_) ||
                unmoved_distance < after.distance + cube::vision::kHoldClearDistance) {
                continue;
            }
            if (turned.has_value() && *turned != face) {
                return std::nullopt;  // turns of two different sides would both explain it
            }
            turned = face;
        }
    }
    return turned;
}

bool HeldCube::half_way_there(const cube::vision::SideRead& read,
                              const cube::vision::SessionColours& colours) const {
    const auto asked = solve_.currentMove();
    if (!asked.has_value() || asked->turns != 2) {
        return false;
    }
    for (const int turns : {1, 3}) {
        const Cube part_way = solve_.currentCube().apply(cube::solver::Move{asked->face, turns});
        if (cube::vision::read_hold(read, part_way, colours).shows(hold_)) {
            return true;
        }
    }
    return false;
}

void HeldCube::note(const cube::vision::SideRead& read, const cube::vision::SessionColours& colours,
                    const Verdict& verdict) const {
    static const QString path = qEnvironmentVariable("CUBE_HELD_LOG");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    static const char* const kKinds[] = {"nothing", "held", "moved", "unsure", "wrong", "unknown"};
    const Cube& now = solve_.currentCube();
    const auto asked = solve_.currentMove();
    const HoldReading here = cube::vision::read_hold(read, now, colours);
    QString line = QStringLiteral("%1 move %2 asked %3 -> %4")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                       .arg(solve_.moveIndex())
                       .arg(solve_.currentMoveNotation(),
                            QString::fromLatin1(kKinds[static_cast<int>(verdict.kind)]));
    line +=
        QStringLiteral(
            " | shown as up %1 front %2 seen %3 | found %4/9 | as it stood %5 "
            "(%6 off)")
            .arg(side_letter(hold_.up), side_letter(hold_.front))
            .arg(seen_)
            .arg(read.stickers_found)
            .arg(static_cast<double>(here.distance), 0, 'f', 2)
            .arg(cube::vision::stickers_off(read, cube::solver::camera_view(now, hold_), colours));
    if (asked.has_value()) {
        const Cube next = now.apply(*asked);
        line += QStringLiteral(" | after the move %1 (%2 off)")
                    .arg(static_cast<double>(cube::vision::read_hold(read, next, colours).distance),
                         0, 'f', 2)
                    .arg(cube::vision::stickers_off(read, cube::solver::camera_view(next, hold_),
                                                    colours));
    }
    if (verdict.kind == Verdict::Kind::Wrong) {
        line += QStringLiteral(" | turned instead: %1").arg(side_letter(verdict.side));
    }
    QTextStream(&file) << line << '\n';
}

void HeldCube::forget() {
    last_ = Verdict{};
    streak_ = 0;
    previous_centre_.reset();
}

void HeldCube::onSolveChanged() {
    if (solve_.currentCube() == judged_cube_) {
        return;
    }
    // A different cube is expected now: whatever was being counted towards
    // was about the one before.
    judged_cube_ = solve_.currentCube();
    move_announced_ = false;
    matched_this_move_ = false;
    last_ = Verdict{};
    streak_ = 0;
}

std::optional<Hold> HeldCube::choose(const std::vector<Hold>& holds) const {
    if (holds.size() == 1) {
        return holds.front();
    }
    for (const Hold& hold : holds) {
        if (hold == hold_) {
            return hold;
        }
    }
    // A side that looks the same turned does not say which way up it is.
    // Someone who turns a cube round to show another side mostly keeps the
    // same side on top, so that is the reading taken - if there is one.
    for (const Hold& hold : holds) {
        if (hold.up == hold_.up) {
            return hold;
        }
    }
    return std::nullopt;
}

HeldCube::Verdict HeldCube::judge(const cube::vision::SideRead& read,
                                  const cube::vision::SessionColours& colours) const {
    using Kind = Verdict::Kind;
    const Cube& now = solve_.currentCube();
    // Every comparison counts only the stickers the reader found: a cube in
    // the hand has fingers on it, and a sticker under a finger is no
    // evidence of anything.
    const HoldReading here = cube::vision::read_hold(read, now, colours);

    // Has the move been made? Asked first, and asked as a comparison: the
    // side in view is on the cube as it stands after the move, and the cube
    // as it stood explains it clearly worse. A move can change as little as
    // one sticker of a side, which the cube before it would still fit
    // loosely - but never as well.
    std::optional<Cube> next;
    std::optional<HoldReading> there;
    if (const auto move = solve_.currentMove(); move.has_value()) {
        next = now.apply(*move);
        there = cube::vision::read_hold(read, *next, colours);
        if (!there->holds.empty() &&
            here.distance >= there->distance + cube::vision::kHoldClearDistance) {
            if (const auto hold = choose(there->holds); hold.has_value()) {
                return say(Kind::Moved, *hold);
            }
            return say(Kind::Unsure);
        }
    }

    if (here.shows(hold_)) {
        return say(Kind::Held, hold_);
    }
    if (there.has_value() && there->shows(hold_)) {
        // It fits both ways about as well: the cube as it stood, held
        // differently, and the cube after the move, held as it was. That is
        // the side that was turned, facing the camera - a turned side and a
        // turned cube look the same from straight on.
        //
        // If this cube was seen held as shown while this very move was being
        // asked for, it is the move: that side was just there the other way
        // round, and turning it is what the screen asked for. Without that
        // sighting neither is assumed, and another side will settle it.
        if (matched_this_move_) {
            return say(Kind::Moved, hold_);
        }
        return say(Kind::Unsure);
    }

    if (!here.holds.empty()) {
        if (const auto hold = choose(here.holds); hold.has_value()) {
            return say(Kind::Held, *hold);
        }
        return say(Kind::Unsure);
    }

    // What is left is a side that is neither the cube as it stood nor the
    // cube after the move. Before calling that a mistake the evidence has to
    // be good, because being told a correct move was wrong is worse than
    // having to press Next:
    //  - the whole side was seen, all nine stickers found, and
    //  - at least two of them are plainly not the colour either cube has
    //    there. One sticker out is a glint, a shadow or a fingertip at least
    //    as often as it is a sticker in the wrong place.
    if (read.stickers_found < 9) {
        return say(Kind::Nothing);
    }
    const int off_as_it_stood =
        cube::vision::stickers_off(read, cube::solver::camera_view(now, hold_), colours);
    const int off_after_move =
        next.has_value()
            ? cube::vision::stickers_off(read, cube::solver::camera_view(*next, hold_), colours)
            : off_as_it_stood;
    if (off_as_it_stood < 2 || off_after_move < 2) {
        return say(Kind::Nothing);
    }
    // A half turn made a quarter at a time passes through a cube that is
    // neither: that is the move under way, not a wrong one.
    if (half_way_there(read, colours)) {
        return say(Kind::Nothing);
    }
    // A turn of some other side - or of the right side the wrong way - is
    // the likeliest thing to have happened, and worth naming if it is the
    // only one that fits.
    if (const auto side = side_turned_instead(read, colours, here.distance); side.has_value()) {
        return say(Kind::Wrong, hold_, *side);
    }
    return say(Kind::Unknown);
}

void HeldCube::onFrameWatched() {
    const std::optional<cube::vision::SideRead>& read = scan_.read();
    const std::optional<cube::vision::SessionColours> colours = scan_.colours();
    if (!read.has_value() || !colours.has_value() || solve_.isSolving()) {
        forget();
        setSight(Sight::None);
        return;
    }

    // Held still since the frame before, as a read has to be for a scan.
    const bool steady =
        previous_centre_.has_value() && cube::vision::held_still(*read, *previous_centre_);
    previous_centre_ = read->centres[4];
    if (!steady) {
        last_ = Verdict{};
        streak_ = 0;
        return;
    }

    onSolveChanged();
    const Verdict verdict = judge(*read, *colours);
    if (!(verdict == last_)) {
        note(*read, *colours, verdict);
    }
    streak_ = verdict == last_ ? streak_ + 1 : 1;
    last_ = verdict;

    using Kind = Verdict::Kind;
    switch (verdict.kind) {
        case Kind::Nothing:
            // A side is in view and there is nothing to say about it, good
            // or bad. Whatever was said of an earlier one no longer stands.
            if (streak_ >= kFramesToTrust) {
                setSight(Sight::None);
            }
            break;
        case Kind::Held:
            if (streak_ >= kFramesToTrust) {
                const bool first_sight = !seen_;
                seen_ = true;
                if (verdict.hold != hold_) {
                    setHold(verdict.hold);
                } else if (first_sight) {
                    emit holdChanged();
                }
                matched_this_move_ = true;
                setSight(Sight::Matched);
            }
            break;
        case Kind::Moved:
            if (streak_ >= kFramesToTrust && !move_announced_) {
                move_announced_ = true;
                const bool first_sight = !seen_;
                seen_ = true;
                if (verdict.hold != hold_) {
                    setHold(verdict.hold);
                } else if (first_sight) {
                    emit holdChanged();
                }
                setSight(Sight::Matched);
                emit moveSeen();
            }
            break;
        case Kind::Unsure:
            if (streak_ >= kFramesToTrust) {
                setSight(Sight::Unsure);
            }
            break;
        case Kind::Wrong:
            if (streak_ >= kFramesToAccuse) {
                setSight(Sight::Wrong, verdict.side);
            }
            break;
        case Kind::Unknown:
            if (streak_ >= kFramesToDoubt) {
                setSight(Sight::Unknown);
            }
            break;
    }
}

}  // namespace cube::app
