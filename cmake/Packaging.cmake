# ---------------------------------------------------------------------------
# CPack packaging. Included once, from the top-level CMakeLists.txt, after
# every target's install() rules have been declared.
#
# A plain ZIP archive is always offered (needs no external tool). Platform-
# native formats are layered on top when the GUI app is being built and the
# packaging tool for that format is actually available.
#
# None of this applies to mobile: real packaging there is androiddeployqt's
# generated APK/AAB and Xcode's archive+export, not CPack.
# ---------------------------------------------------------------------------
if(ANDROID OR IOS)
    message(STATUS
        "Skipping CPack packaging for mobile target (ANDROID/IOS); "
        "use androiddeployqt/Xcode archive+export instead.")
    return()
endif()

set(CPACK_PACKAGE_NAME "Cubeulator")
set(CPACK_PACKAGE_VENDOR "Iain Chesworth")
set(CPACK_PACKAGE_CONTACT "Iain Chesworth")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Cubeulator")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_VERBATIM_VARIABLES ON)

set(CPACK_GENERATOR "ZIP")

if(CUBEULATOR_BUILD_APP)
    if(APPLE)
        list(APPEND CPACK_GENERATOR "DragNDrop")
    elseif(WIN32)
        find_program(CUBEULATOR_MAKENSIS_EXECUTABLE makensis)
        if(CUBEULATOR_MAKENSIS_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "NSIS")
            set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
            set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        endif()
    elseif(UNIX)
        list(APPEND CPACK_GENERATOR "TGZ")
        find_program(CUBEULATOR_DPKG_DEB_EXECUTABLE dpkg-deb)
        if(CUBEULATOR_DPKG_DEB_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "DEB")
            set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
            set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
            set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        endif()
        find_program(CUBEULATOR_RPMBUILD_EXECUTABLE rpmbuild)
        if(CUBEULATOR_RPMBUILD_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "RPM")
            set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
            set(CPACK_RPM_PACKAGE_GROUP "Amusements/Graphics")
            set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
        endif()
    endif()
endif()

include(CPack)
