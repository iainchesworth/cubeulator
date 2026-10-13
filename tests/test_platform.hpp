#pragma once

#include "cube_test_platform_facts.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <string_view>

namespace cube::test {

// Gives a Qt test binary its platform's default Qt platform plugin when
// QT_QPA_PLATFORM is not already set. Which plugin that is, if any, is the
// platform's to say (CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM in
// src/app/platform/<os>/platform.cmake, carried here by
// cube_test_platform_facts.hpp), so the binaries do not ask which OS they
// are on.
inline void apply_default_qpa_platform() {
    constexpr std::string_view qpa = platform::default_qpa_platform;
    if (!qpa.empty() && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray(qpa.data(), static_cast<qsizetype>(qpa.size())));
    }
}

// If a test that draws the real app cannot, because this platform's desktop
// session has not reached the process, why - and what to do about it, with
// `program` named in the advice. Empty when it can, which is always so where
// the platform names no session variable. A QT_QPA_PLATFORM chosen by hand
// picks a plugin that has its own idea of a display, so it is left alone.
[[nodiscard]] inline QString display_problem(const QString& program) {
    constexpr std::string_view variables = platform::display_variables;
    if (variables.empty() || qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        return {};
    }
    const QString names =
        QString::fromUtf8(variables.data(), static_cast<qsizetype>(variables.size()));
    for (const QString& name : names.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (qEnvironmentVariableIsSet(name.toUtf8().constData())) {
            return {};
        }
    }
    QString help = QString::fromUtf8(platform::display_help.data(),
                                     static_cast<qsizetype>(platform::display_help.size()));
    help.replace(QStringLiteral("PROGRAM"), program);
    return QStringLiteral("needs a display, and none of %1 is set: %2")
        .arg(names.split(QLatin1Char(' '), Qt::SkipEmptyParts).join(QStringLiteral(", ")), help);
}

}  // namespace cube::test
