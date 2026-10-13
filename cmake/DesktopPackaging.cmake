# ---------------------------------------------------------------------------
# DesktopPackaging.cmake - what turns cube_app into something a person can
# install on Windows, macOS or Linux (workstream E4a of
# docs/app-experience-plan.md; the specification is docs/design/platforms.md).
#
# Included from src/app/CMakeLists.txt, in the block at its end, after the
# cube_app target exists. It does three things:
#
#   1. rasterises the identity SVGs into .ico / .icns / PNG / hicolor files
#      (packaging/make_platform_icons.py), at build time, into
#      ${CMAKE_BINARY_DIR}/packaging/icons;
#   2. gives the executable its platform identity: the Windows version
#      resource and icon, the macOS bundle and Info.plist, the Linux RPATH
#      and file name;
#   3. declares the install rules and Qt deployment the CPack generators
#      (cmake/Packaging.cmake) package.
#
# Signing is not configured anywhere. It plugs in at two places, both marked
# "SIGNING" below and in docs/packaging.md.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/AppIdentity.cmake")

set(CUBEULATOR_PACKAGING_DIR "${CMAKE_BINARY_DIR}/packaging")
set(CUBEULATOR_ICON_DIR "${CUBEULATOR_PACKAGING_DIR}/icons")
set(CUBEULATOR_IDENTITY_DIR "${PROJECT_SOURCE_DIR}/docs/design/identity")

# Icon sizes the generator writes (keep in step with make_platform_icons.py).
set(CUBEULATOR_WINDOW_ICON_SIZES 16 20 24 32 48 64 128 256)
set(CUBEULATOR_HICOLOR_SIZES 48 64 128 256)

if(WIN32)
    # The project declares LANGUAGES CXX only; resource files need RC.
    enable_language(RC)
endif()

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
# cube_configure_desktop_app(<target> <desktop_library>)
# ---------------------------------------------------------------------------
function(cube_configure_desktop_app target)
    cube_generate_platform_icons()

    # --- executable name: Cubeulator.exe, Cubeulator.app, cubeulator --------
    if(WIN32 OR APPLE)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "Cubeulator")
    else()
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "cubeulator")
    endif()

    # --- the window icon: PNGs embedded as a Qt resource --------------------
    # (macOS takes the Dock icon from the bundle instead.)
    if(CUBEULATOR_ICONS_AVAILABLE AND NOT APPLE)
        set(_icon_files "")
        foreach(_size IN LISTS CUBEULATOR_WINDOW_ICON_SIZES)
            list(APPEND _icon_files "${CUBEULATOR_ICON_DIR}/png/icon-${_size}.png")
        endforeach()
        qt_add_resources(${target} "cube_app_icons"
            PREFIX "/cubeulator/icons"
            BASE "${CUBEULATOR_ICON_DIR}/png"
            FILES ${_icon_files})
        add_dependencies(${target} cube_icons)
    endif()

    # --- Windows: version resource, icon -----------------------------------
    if(WIN32)
        # The version and git description come from the same script that
        # stamps cube/version.hpp, run again each build so the resource
        # tracks new commits. configure_file only rewrites it when the text
        # changes, so the resource is not recompiled needlessly.
        find_package(Git QUIET)
        set(_rc_template "${PROJECT_SOURCE_DIR}/packaging/windows/Cubeulator.rc.in")
        set(_rc_file "${CUBEULATOR_PACKAGING_DIR}/Cubeulator.rc")
        set(_ico_file "${CUBEULATOR_ICON_DIR}/Cubeulator.ico")
        file(MAKE_DIRECTORY "${CUBEULATOR_PACKAGING_DIR}")

        # Once at configure time so the file exists for the first build.
        function(_cube_stamp_rc)
            set(CUBEULATOR_VERSION "${PROJECT_VERSION}")
            set(SRC "${_rc_template}")
            set(DST "${_rc_file}")
            set(WORKDIR "${PROJECT_SOURCE_DIR}")
            set(CUBEULATOR_ICON_FILE "${_ico_file}")
            set(CUBEULATOR_PUBLISHER "${CUBEULATOR_PUBLISHER}")
            set(CUBEULATOR_COPYRIGHT "${CUBEULATOR_COPYRIGHT}")
            include("${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake")
        endfunction()
        _cube_stamp_rc()

        add_custom_target(cube_app_resource ALL
            BYPRODUCTS "${_rc_file}"
            COMMAND "${CMAKE_COMMAND}"
                "-DCUBEULATOR_VERSION=${PROJECT_VERSION}"
                "-DSRC=${_rc_template}"
                "-DDST=${_rc_file}"
                "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
                "-DWORKDIR=${PROJECT_SOURCE_DIR}"
                "-DCUBEULATOR_ICON_FILE=${_ico_file}"
                "-DCUBEULATOR_PUBLISHER=${CUBEULATOR_PUBLISHER}"
                "-DCUBEULATOR_COPYRIGHT=${CUBEULATOR_COPYRIGHT}"
                -P "${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake"
            COMMENT "Stamping the Windows version resource"
            VERBATIM)
        add_dependencies(${target} cube_app_resource)
        if(CUBEULATOR_ICONS_AVAILABLE)
            add_dependencies(cube_app_resource cube_icons)
        endif()

        target_sources(${target} PRIVATE "${_rc_file}")
        set_source_files_properties("${_rc_file}" PROPERTIES
            GENERATED TRUE
            OBJECT_DEPENDS "${_ico_file}")
    endif()

    # --- macOS: bundle, Info.plist, icns ------------------------------------
    if(APPLE)
        # The reason the system shows when the camera is first used comes from
        # the string inventory, so the wording lives in one place.
        file(READ "${PROJECT_SOURCE_DIR}/docs/design/strings.json" _strings)
        string(JSON _camera_entry GET "${_strings}" "permission.system.camera")
        string(JSON _camera_text GET "${_camera_entry}" 0)
        string(REPLACE "&" "&amp;" _camera_text "${_camera_text}")
        string(REPLACE "<" "&lt;" _camera_text "${_camera_text}")
        string(REPLACE ">" "&gt;" _camera_text "${_camera_text}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${PROJECT_SOURCE_DIR}/docs/design/strings.json")

        set(CUBEULATOR_CAMERA_USAGE_DESCRIPTION "${_camera_text}")
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
    endif()

    # --- Linux: find the bundled Qt next to the install ---------------------
    if(UNIX AND NOT APPLE)
        set_target_properties(${target} PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
            INSTALL_RPATH_USE_LINK_PATH FALSE)
    endif()

    # --- install rules -------------------------------------------------------
    include(GNUInstallDirs)

    # Linux: the app and the Qt it carries live in one private directory
    # (/usr/lib/cubeulator when packaged under /usr), so that the Qt libraries
    # never sit beside - or collide with - the distribution's own. /usr/bin
    # gets a symlink; the desktop entry, AppStream metadata and icons go to
    # the standard share/ places.
    if(UNIX AND NOT APPLE)
        set(_app_dir "lib/cubeulator")
        set(_bindir "${_app_dir}/bin")
    else()
        set(_bindir "${CMAKE_INSTALL_BINDIR}")
    endif()

    install(TARGETS ${target}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION "${_bindir}")

    set(_deploy_options "")
    if(WIN32)
        # Dependencies that came from vcpkg: OpenCV, ONNX Runtime and, under
        # them, everything they load. Resolved transitively at install time
        # (cmake/InstallRuntimeDependencies.cmake); Qt's own are deployed by
        # windeployqt in the script below.
        set(_dep_dirs "$<TARGET_FILE_DIR:${target}>")
        if(DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            list(INSERT _dep_dirs 0 "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
        install(CODE "
            set(CUBE_EXE \"$<TARGET_FILE:${target}>\")
            set(CUBE_DEP_DIRS \"${_dep_dirs}\")
            set(CUBE_DEST_DIR \"${_bindir}\")
        ")
        install(SCRIPT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/InstallRuntimeDependencies.cmake")

        # The C++ runtime goes beside the executable (app-local), rather than
        # windeployqt's default of bundling the 25 MB vc_redist installer,
        # which nothing in the installer would run.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${_bindir}")
        set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
        include(InstallRequiredSystemLibraries)
        set(_deploy_options NO_COMPILER_RUNTIME)
    endif()

    if(UNIX AND NOT APPLE)
        # Where Qt's deployment puts things, read by its script at install time.
        install(CODE "
            set(QT_DEPLOY_BIN_DIR \"${_app_dir}/bin\")
            set(QT_DEPLOY_LIB_DIR \"${_app_dir}/lib\")
            set(QT_DEPLOY_PLUGINS_DIR \"${_app_dir}/plugins\")
            set(QT_DEPLOY_QML_DIR \"${_app_dir}/qml\")
        ")
    endif()

    # Qt's runtime, plugins and the QML modules the app imports.
    #   Windows: windeployqt. macOS: macdeployqt, into the bundle.
    #   Linux:   the libraries and plugins are copied beside the executable.
    # SIGNING (macOS): macdeployqt can sign as it deploys
    #   (DEPLOY_TOOL_OPTIONS -codesign=<identity>); see docs/packaging.md.
    qt_generate_deploy_qml_app_script(
        TARGET ${target}
        OUTPUT_SCRIPT _cube_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        ${_deploy_options})
    install(SCRIPT "${_cube_deploy_script}")

    # macOS: sign the bundle after Qt has been deployed into it. Apple Silicon
    # kills a process whose code carries no signature at all, so an ad-hoc one
    # ("-") is the floor and the unsigned build still opens (from the
    # Finder's right-click > Open, for an app that came off the internet).
    # SIGNING: pass a Developer ID identity to sign for real; the hardened
    # runtime and a secure timestamp it adds are what notarisation requires.
    #   cmake -DCUBEULATOR_MACOS_CODESIGN_IDENTITY="Developer ID Application: ..."
    # Notarising the .dmg afterwards (notarytool) is a separate, later step.
    if(APPLE)
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
    endif()

    # The licence and the third-party notices are installed by
    # cube_add_third_party_notices() (cmake/ThirdPartyNotices.cmake), to the
    # root of the install, or share/doc/cubeulator on Linux.

    # --- Linux: launcher, desktop entry, AppStream metadata, icons -----------
    if(UNIX AND NOT APPLE)
        install(CODE "
            file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
            file(CREATE_LINK \"../${_app_dir}/bin/cubeulator\"
                 \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/cubeulator\" SYMBOLIC)
        ")

        # The wording comes from the string inventory, not from here.
        file(READ "${PROJECT_SOURCE_DIR}/docs/design/strings.json" _strings)
        string(JSON _entry GET "${_strings}" "store.summary")
        string(JSON CUBEULATOR_STORE_SUMMARY GET "${_entry}" 0)
        string(JSON _entry GET "${_strings}" "about.what")
        string(JSON CUBEULATOR_ABOUT_WHAT GET "${_entry}" 0)
        foreach(_var CUBEULATOR_STORE_SUMMARY CUBEULATOR_ABOUT_WHAT)
            string(REPLACE "&" "&amp;" ${_var} "${${_var}}")
            string(REPLACE "<" "&lt;" ${_var} "${${_var}}")
            string(REPLACE ">" "&gt;" ${_var} "${${_var}}")
        endforeach()
        # Honours SOURCE_DATE_EPOCH, so a reproducible build stays reproducible.
        string(TIMESTAMP CUBEULATOR_RELEASE_DATE "%Y-%m-%d" UTC)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${PROJECT_SOURCE_DIR}/docs/design/strings.json")

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
    endif()
endfunction()
