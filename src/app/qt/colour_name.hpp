#pragma once

#include <cube/solver/cube.hpp>

#include <QString>

// A cube colour as the name QML maps to its palette entry ("White", "Yellow",
// "Green", "Blue", "Orange", "Red"). The palette stays with the UI layer; the
// controllers only say which colour.
[[nodiscard]] inline QString colour_name(cube::solver::Color colour) {
    switch (colour) {
        case cube::solver::Color::White:
            return QStringLiteral("White");
        case cube::solver::Color::Yellow:
            return QStringLiteral("Yellow");
        case cube::solver::Color::Green:
            return QStringLiteral("Green");
        case cube::solver::Color::Blue:
            return QStringLiteral("Blue");
        case cube::solver::Color::Orange:
            return QStringLiteral("Orange");
        case cube::solver::Color::Red:
            return QStringLiteral("Red");
    }
    return QStringLiteral("White");
}
