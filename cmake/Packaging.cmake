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
if(NOT CUBEULATOR_PLATFORM_DESKTOP)
    message(STATUS
        "Skipping CPack packaging for the ${CUBEULATOR_PLATFORM} target; "
        "use androiddeployqt/Xcode archive+export instead.")
    return()
endif()

set(CPACK_PACKAGE_NAME "Cubeulator")
set(CPACK_PACKAGE_VENDOR "Iain Chesworth Labs")
set(CPACK_PACKAGE_CONTACT "Iain Chesworth Labs")
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
    # The installers this platform can make (NSIS, DragNDrop, DEB/RPM) are the
    # platform fragment's to add: src/app/platform/<name>/platform.cmake.
    cube_platform_configure_packaging()
endif()

include(CPack)
