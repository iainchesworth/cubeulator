# ---------------------------------------------------------------------------
# DesktopPackaging.cmake - what turns cube_app into something a person can
# install on Windows, macOS or Linux (workstream E4a of
# docs/app-experience-plan.md; the specification is docs/design/platforms.md).
#
# Included from src/app/CMakeLists.txt, in the block at its end, after the
# cube_app target exists. What it holds is what the three desktops share:
#
#   1. rasterising the identity SVGs into .ico / .icns / PNG / hicolor files
#      (packaging/make_platform_icons.py), at build time, into
#      ${CMAKE_BINARY_DIR}/packaging/icons;
#   2. a few helpers the platforms' own steps use (the window icon, the
#      install rules and Qt deployment, wording from the string inventory).
#
# Everything that differs between the platforms is the platform's own, in
# packaging/<name>/ConfigureApp.cmake, reached through the platform fragment's
# cube_platform_configure_desktop_app() hook (docs/platform-isolation.md):
# the Windows version resource and icon, the macOS bundle and Info.plist, the
# Linux private directory and desktop entry.
#
# Signing is not configured anywhere. It plugs in at two places, both marked
# "SIGNING" in docs/packaging.md.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/AppIdentity.cmake")

set(CUBEULATOR_PACKAGING_DIR "${CMAKE_BINARY_DIR}/packaging")
set(CUBEULATOR_ICON_DIR "${CUBEULATOR_PACKAGING_DIR}/icons")
set(CUBEULATOR_IDENTITY_DIR "${PROJECT_SOURCE_DIR}/docs/design/identity")

# Icon sizes the generator writes (keep in step with make_platform_icons.py).
set(CUBEULATOR_WINDOW_ICON_SIZES 16 20 24 32 48 64 128 256)
set(CUBEULATOR_HICOLOR_SIZES 48 64 128 256)

# ---------------------------------------------------------------------------
# Icons. One custom command, one target; everything else depends on the
# files it writes, never on the target.
# ---------------------------------------------------------------------------
function(cube_generate_platform_icons)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(WARNING
            "Python 3 not found: the app icon will not be generated and the "
            "packages will carry the default icon. (packaging/make_platform_icons.py)")
        set(CUBEULATOR_ICONS_AVAILABLE FALSE PARENT_SCOPE)
        return()
    endif()

    set(_outputs
        "${CUBEULATOR_ICON_DIR}/Cubeulator.ico"
        "${CUBEULATOR_ICON_DIR}/Cubeulator.icns"
        "${CUBEULATOR_ICON_DIR}/hicolor/scalable/apps/${CUBEULATOR_APP_ID}.svg")
    foreach(_size IN LISTS CUBEULATOR_WINDOW_ICON_SIZES)
        list(APPEND _outputs "${CUBEULATOR_ICON_DIR}/png/icon-${_size}.png")
    endforeach()
    foreach(_size IN LISTS CUBEULATOR_HICOLOR_SIZES)
        list(APPEND _outputs
            "${CUBEULATOR_ICON_DIR}/hicolor/${_size}x${_size}/apps/${CUBEULATOR_APP_ID}.png")
    endforeach()

    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND "${Python3_EXECUTABLE}" -I
            "${PROJECT_SOURCE_DIR}/packaging/make_platform_icons.py"
            --identity "${CUBEULATOR_IDENTITY_DIR}"
            --out "${CUBEULATOR_ICON_DIR}"
            --app-id "${CUBEULATOR_APP_ID}"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/packaging/make_platform_icons.py"
            "${CUBEULATOR_IDENTITY_DIR}/icon.svg"
            "${CUBEULATOR_IDENTITY_DIR}/icon-small.svg"
        COMMENT "Rasterising the app icon from docs/design/identity"
        VERBATIM)

    add_custom_target(cube_icons DEPENDS ${_outputs})
    set(CUBEULATOR_ICONS_AVAILABLE TRUE PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# cube_embed_window_icon(<target>)
#
# The window icon: the PNGs embedded as a Qt resource, which
# desktop_integration.cpp loads. For platforms whose Traits::sets_window_icon
# is true; macOS takes the Dock icon from the bundle instead.
# ---------------------------------------------------------------------------
function(cube_embed_window_icon target)
    if(NOT CUBEULATOR_ICONS_AVAILABLE)
        return()
    endif()
    set(_icon_files "")
    foreach(_size IN LISTS CUBEULATOR_WINDOW_ICON_SIZES)
        list(APPEND _icon_files "${CUBEULATOR_ICON_DIR}/png/icon-${_size}.png")
    endforeach()
    qt_add_resources(${target} "cube_app_icons"
        PREFIX "/cubeulator/icons"
        BASE "${CUBEULATOR_ICON_DIR}/png"
        FILES ${_icon_files})
    add_dependencies(${target} cube_icons)
endfunction()

# ---------------------------------------------------------------------------
# cube_string_from_inventory(<key> <out_var>)
#
# The first-language wording of one entry of docs/design/strings.json, escaped
# for XML, so that text the packages carry (the camera usage string, the store
# summary) is written in one place. Re-configures when the inventory changes.
# ---------------------------------------------------------------------------
function(cube_string_from_inventory key out_var)
    file(READ "${PROJECT_SOURCE_DIR}/docs/design/strings.json" _strings)
    string(JSON _entry GET "${_strings}" "${key}")
    string(JSON _text GET "${_entry}" 0)
    string(REPLACE "&" "&amp;" _text "${_text}")
    string(REPLACE "<" "&lt;" _text "${_text}")
    string(REPLACE ">" "&gt;" _text "${_text}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/docs/design/strings.json")
    set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# cube_install_desktop_executable(<target> <bindir>)
# cube_deploy_qt_on_install(<target> [<qt_generate_deploy_qml_app_script options>...])
#
# The two install rules every desktop shares: the executable (or, on macOS,
# the bundle), and Qt's own deployment script - the runtime, plugins and the
# QML modules the app imports - which Qt's deploy tooling generates for the
# platform it is configured for. They are separate so that a platform can put
# its own rules between them (the Windows dependency scan, the Linux launcher).
#
# SIGNING (macOS): macdeployqt can sign as it deploys
#   (DEPLOY_TOOL_OPTIONS -codesign=<identity>); see docs/packaging.md.
# ---------------------------------------------------------------------------
function(cube_install_desktop_executable target bindir)
    install(TARGETS ${target}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION "${bindir}")
endfunction()

function(cube_deploy_qt_on_install target)
    qt_generate_deploy_qml_app_script(
        TARGET ${target}
        OUTPUT_SCRIPT _cube_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        ${ARGN})
    install(SCRIPT "${_cube_deploy_script}")
endfunction()

# ---------------------------------------------------------------------------
# cube_configure_desktop_app(<target>)
#
# The icons, then the platform's own configuration of the app
# (cube_platform_configure_desktop_app, in its fragment).
# ---------------------------------------------------------------------------
function(cube_configure_desktop_app target)
    cube_generate_platform_icons()
    cube_platform_configure_desktop_app(${target})
endfunction()
