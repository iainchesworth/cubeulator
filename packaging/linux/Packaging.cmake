# The Linux CPack generators: a .tar.gz, and a .deb and an .rpm where their
# tools are installed.
#
# Included by cube_platform_configure_packaging() in
# src/app/platform/linux/platform.cmake, in cmake/Packaging.cmake's scope, so
# it sets CPACK_* variables there.
list(APPEND CPACK_GENERATOR "TGZ")

# The packages put the app under /usr: its own directory in /usr/lib/cubeulator
# (with the Qt it carries), a launcher in /usr/bin, and the desktop entry,
# AppStream metadata and icons in /usr/share.
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Qt and OpenCV ship inside the package, so the dependencies named here are
# only the system libraries Qt's xcb and OpenGL plumbing loads.
# They are listed rather than discovered because dpkg-shlibdeps and rpm's own
# scan would also try to resolve the bundled libraries, and would either fail
# or ask the distribution for libQt6*.so it does not have.
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

find_program(CUBEULATOR_DPKG_DEB_EXECUTABLE dpkg-deb)
if(CUBEULATOR_DPKG_DEB_EXECUTABLE)
    list(APPEND CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libgcc-s1, libgl1, libegl1, libfontconfig1, libfreetype6, libglib2.0-0, libdbus-1-3, libx11-6, libx11-xcb1, libxcb1, libxkbcommon0, libxkbcommon-x11-0, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-render0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xkb1, libxcb-xinerama0")
    # Japanese and Chinese text is drawn with the system's font
    # (docs/design/platforms.md, "Fonts"); a minimal install has none.
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fonts-noto-cjk")
endif()

find_program(CUBEULATOR_RPMBUILD_EXECUTABLE rpmbuild)
if(CUBEULATOR_RPMBUILD_EXECUTABLE)
    list(APPEND CPACK_GENERATOR "RPM")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    # Bundled libraries must not satisfy, or be required of, the distribution:
    # no automatic requires or provides, and an explicit list instead.
    set(CPACK_RPM_PACKAGE_AUTOREQPROV OFF)
    set(CPACK_RPM_PACKAGE_REQUIRES
        "libxcb, libxkbcommon, libxkbcommon-x11, mesa-libGL, mesa-libEGL, fontconfig, freetype, dbus-libs, glib2, libX11, libX11-xcb, xcb-util-cursor, xcb-util-image, xcb-util-keysyms, xcb-util-renderutil, xcb-util-wm")
    # RPM has no "recommends" field in CPack; a soft suggestion.
    set(CPACK_RPM_PACKAGE_SUGGESTS "google-noto-sans-cjk-fonts")
    set(CPACK_RPM_DEBUGINFO_PACKAGE OFF)
    # The directories the desktop owns are not ours to claim.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        "/usr/share/applications"
        "/usr/share/metainfo"
        "/usr/share/icons"
        "/usr/share/icons/hicolor"
        "/usr/share/icons/hicolor/scalable"
        "/usr/share/icons/hicolor/scalable/apps"
        "/usr/share/icons/hicolor/48x48"
        "/usr/share/icons/hicolor/48x48/apps"
        "/usr/share/icons/hicolor/64x64"
        "/usr/share/icons/hicolor/64x64/apps"
        "/usr/share/icons/hicolor/128x128"
        "/usr/share/icons/hicolor/128x128/apps"
        "/usr/share/icons/hicolor/256x256"
        "/usr/share/icons/hicolor/256x256/apps")
endif()
