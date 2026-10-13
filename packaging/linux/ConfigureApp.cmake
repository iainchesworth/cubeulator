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
")

cube_install_desktop_executable(${target} "${_bindir}")

# Qt's runtime, plugins and the QML modules the app imports: the libraries and
# plugins are copied beside the executable.
cube_deploy_qt_on_install(${target})

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
