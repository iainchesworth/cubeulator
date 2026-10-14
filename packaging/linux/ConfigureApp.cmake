# Linux: makes <target> the `cubeulator` executable that finds the Qt it
# carries, and declares the install rules the .deb, .rpm and .tar.gz package:
# a private directory, a launcher, the desktop entry, AppStream metadata and
# icons.
#
# Included by cube_platform_configure_desktop_app() in
# src/app/platform/linux/platform.cmake, inside that function, so `target` is
# the app and the helpers of cmake/DesktopPackaging.cmake are available.
include(GNUInstallDirs)

set_target_properties(${target} PROPERTIES OUTPUT_NAME "cubeulator")

cube_embed_window_icon(${target})

# Find the bundled Qt next to the install.
set_target_properties(${target} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
    INSTALL_RPATH_USE_LINK_PATH FALSE)

# --- install rules ------------------------------------------------------------
# The app and the Qt it carries live in one private directory
# (/usr/lib/cubeulator when packaged under /usr), so that the Qt libraries
# never sit beside - or collide with - the distribution's own. /usr/bin gets a
# symlink; the desktop entry, AppStream metadata and icons go to the standard
# share/ places.
set(_app_dir "lib/cubeulator")
set(_bindir "${_app_dir}/bin")

# Where Qt's deployment puts things, read by its script at install time.
install(CODE "
    set(QT_DEPLOY_BIN_DIR \"${_app_dir}/bin\")
    set(QT_DEPLOY_LIB_DIR \"${_app_dir}/lib\")
    set(QT_DEPLOY_PLUGINS_DIR \"${_app_dir}/plugins\")
    set(QT_DEPLOY_QML_DIR \"${_app_dir}/qml\")
    set(QT_DEPLOY_TRANSLATIONS_DIR \"${_app_dir}/translations\")
")

cube_install_desktop_executable(${target} "${_bindir}")

# Qt's runtime, plugins and the QML modules the app imports: the libraries and
# plugins are copied beside the executable.
cube_deploy_qt_on_install(${target})

# Two things Qt's deployment script does not do the way a package needs.
#
# 1. qt.conf. The script writes one with the prefix and the four directories it
#    was given, but never a Translations entry, so Qt would look for its own
#    catalogues (LanguageManager loads qt_<language>.qm from
#    QLibraryInfo::TranslationsPath) in <prefix>/translations, which is
#    /usr/translations: a directory in /usr that no other package would
#    expect, and one that collides with any other Qt application packaged the
#    same way. They are deployed into the private directory above, and
#    qt.conf says so.
# 2. Plugins that cannot be given honest dependencies. The deployment copies
#    every platform theme and the Wayland integrations Qt knows. The GTK 3
#    theme (libqgtk3.so) links GTK, Cairo, Pango and ATK, which a KDE or a
#    minimal desktop does not have and the package should not require; Qt
#    falls back to the desktop-portal theme, which is bundled. The Wayland
#    client libraries and shell integrations belong to a Wayland platform
#    plugin the package does not carry (xcb only: a Wayland session runs the
#    app through XWayland, docs/packaging.md), so they would be unused weight
#    with dependencies of their own. The QML debugger and profiler servers
#    (plugins/qmltooling) are not a thing a release carries.
#    packaging/check-package.sh holds all three.
install(CODE "
    set(_cube_root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${_app_dir}\")
    file(APPEND \"\${_cube_root}/bin/qt.conf\" \"Translations = ${_app_dir}/translations\\n\")
    file(REMOVE \"\${_cube_root}/plugins/platformthemes/libqgtk3.so\")
    file(REMOVE_RECURSE \"\${_cube_root}/plugins/qmltooling\")
    file(GLOB _cube_wayland_dirs LIST_DIRECTORIES true \"\${_cube_root}/plugins/wayland-*\")
    file(GLOB _cube_wayland_libs \"\${_cube_root}/lib/libQt6Wayland*\"
                                \"\${_cube_root}/lib/libQt6WlShellIntegration*\")
    if(_cube_wayland_dirs)
        file(REMOVE_RECURSE \${_cube_wayland_dirs})
    endif()
    if(_cube_wayland_libs)
        file(REMOVE \${_cube_wayland_libs})
    endif()
")

# The licence and the third-party notices are installed by
# cube_add_third_party_notices() (cmake/ThirdPartyNotices.cmake), under
# share/doc/cubeulator.

# --- launcher, desktop entry, AppStream metadata, icons -----------------------
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    file(CREATE_LINK \"../${_app_dir}/bin/cubeulator\"
         \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/cubeulator\" SYMBOLIC)
")

# The wording comes from the string inventory, not from here.
cube_string_from_inventory("store.summary" CUBEULATOR_STORE_SUMMARY)
cube_string_from_inventory("about.what" CUBEULATOR_ABOUT_WHAT)
# Honours SOURCE_DATE_EPOCH, so a reproducible build stays reproducible.
string(TIMESTAMP CUBEULATOR_RELEASE_DATE "%Y-%m-%d" UTC)
# A pre-release is a development release to a software centre.
if(CUBEULATOR_VERSION_PRERELEASE STREQUAL "")
    set(CUBEULATOR_APPSTREAM_RELEASE_TYPE "stable")
else()
    set(CUBEULATOR_APPSTREAM_RELEASE_TYPE "development")
endif()
# AppStream orders versions the way Debian does: "~" sorts before the release.
string(REPLACE "-" "~" CUBEULATOR_APPSTREAM_VERSION "${CUBEULATOR_VERSION_FULL}")

configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/cubeulator.desktop.in"
    "${CUBEULATOR_PACKAGING_DIR}/${CUBEULATOR_APP_ID}.desktop" @ONLY)
configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/cubeulator.metainfo.xml.in"
    "${CUBEULATOR_PACKAGING_DIR}/${CUBEULATOR_APP_ID}.metainfo.xml" @ONLY)

install(FILES "${CUBEULATOR_PACKAGING_DIR}/${CUBEULATOR_APP_ID}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${CUBEULATOR_PACKAGING_DIR}/${CUBEULATOR_APP_ID}.metainfo.xml"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")

if(CUBEULATOR_ICONS_AVAILABLE)
    install(FILES "${CUBEULATOR_ICON_DIR}/hicolor/scalable/apps/${CUBEULATOR_APP_ID}.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
    foreach(_size IN LISTS CUBEULATOR_HICOLOR_SIZES)
        install(FILES
            "${CUBEULATOR_ICON_DIR}/hicolor/${_size}x${_size}/apps/${CUBEULATOR_APP_ID}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps")
    endforeach()
endif()
