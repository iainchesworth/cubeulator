# macOS: makes <target> Cubeulator.app, with its Info.plist and icon, and
# declares the install rules the .dmg packages.
#
# Included by cube_platform_configure_desktop_app() in
# src/app/platform/macos/platform.cmake, inside that function, so `target` is
# the app and the helpers of cmake/DesktopPackaging.cmake are available.
include(GNUInstallDirs)

set_target_properties(${target} PROPERTIES OUTPUT_NAME "Cubeulator")

# No window icon resource: the Dock icon is the bundle's
# (platform/macos/desktop_shell_macos.cpp says the window does not set one).

# --- the bundle, Info.plist, icns --------------------------------------------
# The reason the system shows when the camera is first used comes from the
# string inventory, so the wording lives in one place.
cube_string_from_inventory("permission.system.camera" CUBEULATOR_CAMERA_USAGE_DESCRIPTION)
set(CUBEULATOR_BUNDLE_ID "${CUBEULATOR_APP_ID}")
set(CUBEULATOR_BUNDLE_NAME "${CUBEULATOR_APP_DISPLAY_NAME}")
set(CUBEULATOR_BUNDLE_VERSION "${PROJECT_VERSION}")
set(CUBEULATOR_ICNS_NAME "Cubeulator.icns")
set(_plist "${CUBEULATOR_PACKAGING_DIR}/Info.plist")
configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in" "${_plist}" @ONLY)

set_target_properties(${target} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${_plist}")

if(CUBEULATOR_ICONS_AVAILABLE)
    set(_icns "${CUBEULATOR_ICON_DIR}/Cubeulator.icns")
    set_source_files_properties("${_icns}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        GENERATED TRUE)
    target_sources(${target} PRIVATE "${_icns}")
    add_dependencies(${target} cube_icons)
endif()

# --- install rules ------------------------------------------------------------
cube_install_desktop_executable(${target} "${CMAKE_INSTALL_BINDIR}")

# Qt's runtime, plugins and the QML modules the app imports: macdeployqt, into
# the bundle.
cube_deploy_qt_on_install(${target})

# macdeployqt copies the QML debugger and profiler servers (PlugIns/qmltooling).
# A release does not carry them, and they go before the bundle is signed, which
# seals what is in it. packaging/check-package.sh holds it.
install(CODE "
    file(REMOVE_RECURSE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Cubeulator.app/Contents/PlugIns/qmltooling\")
")

# Sign the bundle after Qt has been deployed into it. Apple Silicon kills a
# process whose code carries no signature at all, so an ad-hoc one ("-") is the
# floor and the unsigned build still opens (from the Finder's right-click >
# Open, for an app that came off the internet).
# SIGNING: pass a Developer ID identity to sign for real; the hardened runtime
# and a secure timestamp it adds are what notarisation requires.
#   cmake -DCUBEULATOR_MACOS_CODESIGN_IDENTITY="Developer ID Application: ..."
# Notarising the .dmg afterwards (notarytool) is a separate, later step.
set(CUBEULATOR_MACOS_CODESIGN_IDENTITY "-" CACHE STRING
    "codesign identity for Cubeulator.app: \"-\" is ad-hoc (no certificate), or a Developer ID")
install(CODE "
    set(_app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Cubeulator.app\")
    set(_identity \"${CUBEULATOR_MACOS_CODESIGN_IDENTITY}\")
    set(_flags --force --deep)
    if(NOT _identity STREQUAL \"-\")
        list(APPEND _flags --options runtime --timestamp)
    endif()
    execute_process(COMMAND codesign \${_flags} --sign \"\${_identity}\" \"\${_app}\"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR \"codesign failed (\${_rc}) for \${_app}\")
    endif()
")

# The licence and the third-party notices are installed by
# cube_add_third_party_notices() (cmake/ThirdPartyNotices.cmake), to the root of
# the install, beside the .app in the .dmg.
