#pragma once

#include <QObject>
#include <qqmlintegration.h>

// What kind of build this is, for the few things that exist only in
// developer builds: the component gallery today; the settings that moved out
// of the user's Settings screen (graphics backend, cube style, frame rate)
// when E3a lands.
//
// A developer build is one configured with CUBEULATOR_DEVELOPER_BUILD, which
// defaults on for every build type except Release and MinSizeRel.
class BuildInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool developerBuild READ developerBuild CONSTANT)
    Q_PROPERTY(bool galleryRequested READ galleryRequested CONSTANT)

public:
    explicit BuildInfo(QObject* parent = nullptr);

    [[nodiscard]] static bool isDeveloperBuild();
    [[nodiscard]] bool developerBuild() const { return isDeveloperBuild(); }

    // The gallery was asked for on the command line (--gallery) or by
    // CUBEULATOR_GALLERY=1. Always false in a build that is not a developer
    // build, whatever was asked.
    [[nodiscard]] bool galleryRequested() const { return m_galleryRequested; }

private:
    bool m_galleryRequested = false;
};
