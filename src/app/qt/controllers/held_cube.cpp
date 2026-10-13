#include "controllers/held_cube.hpp"

#include "controllers/direct_scan.hpp"
#include "solve_controller.hpp"

#include <QMatrix3x3>

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
            setSight(Sight::None);
        }
    });
    connect(&solve_, &SolveController::stateChanged, this, &HeldCube::onSolveChanged);
    connect(&solve_, &SolveController::solutionChanged, this, [this]() {
        // A new cube to solve starts from the hold the screen asks for.
        forget();
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
        case Sight::Unknown:
            return QStringLiteral("unknown");
    }
    return QStringLiteral("none");
}

void HeldCube::setSight(Sight sight) {
    if (sight == sight_) {
        return;
    }
    sight_ = sight;
    emit sightChanged();
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
    const HoldReading here = cube::vision::read_hold(read.lab, now, colours);

    // Has the move been made? Asked first, and asked as a comparison: the
    // side in view is on the cube as it stands after the move, and the cube
    // as it stood explains it clearly worse. A move can change as little as
    // one sticker of a side, which the cube before it would still fit
    // loosely - but never as well.
    std::optional<HoldReading> there;
    if (const auto move = solve_.currentMove(); move.has_value()) {
        there = cube::vision::read_hold(read.lab, now.apply(*move), colours);
        if (!there->holds.empty() &&
            here.distance >= there->distance + cube::vision::kHoldClearDistance) {
            if (const auto hold = choose(there->holds); hold.has_value()) {
                return {Kind::Moved, *hold};
            }
            return {Kind::Unsure, {}};
        }
    }

    if (here.shows(hold_)) {
        return {Kind::Held, hold_};
    }
    if (there.has_value() && there->shows(hold_)) {
        // It fits both ways about as well: the cube as it stood, held
        // differently, and the cube after the move, held as it was. That is
        // the side that was turned, facing the camera - a turned side and a
        // turned cube look the same from straight on. Neither is assumed;
        // another side will settle it.
        return {Kind::Unsure, {}};
    }

    if (!here.holds.empty()) {
        if (const auto hold = choose(here.holds); hold.has_value()) {
            return {Kind::Held, *hold};
        }
        return {Kind::Unsure, {}};
    }
    return {Kind::Unknown, {}};
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
    streak_ = verdict == last_ ? streak_ + 1 : 1;
    last_ = verdict;

    using Kind = Verdict::Kind;
    switch (verdict.kind) {
        case Kind::Nothing:
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
        case Kind::Unknown:
            if (streak_ >= kFramesToDoubt) {
                setSight(Sight::Unknown);
            }
            break;
    }
}

}  // namespace cube::app
