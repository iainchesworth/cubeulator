#include "BuildInfo.hpp"

#include <QCoreApplication>
#include <QStringList>
#include <QtGlobal>

bool BuildInfo::isDeveloperBuild() {
#if defined(CUBEULATOR_DEVELOPER_BUILD) && CUBEULATOR_DEVELOPER_BUILD
    return true;
#else
    return false;
#endif
}

BuildInfo::BuildInfo(QObject* parent) : QObject(parent) {
    if (!isDeveloperBuild()) {
        return;
    }
    const QStringList arguments = QCoreApplication::arguments();
    m_galleryRequested = arguments.contains(QStringLiteral("--gallery")) ||
                         qEnvironmentVariable("CUBEULATOR_GALLERY") == QStringLiteral("1");
}
