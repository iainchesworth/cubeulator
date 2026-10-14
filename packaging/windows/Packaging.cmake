# The Windows CPack generator: an NSIS installer.
#
# Included by cube_platform_configure_packaging() in
# src/app/platform/windows/platform.cmake, in cmake/Packaging.cmake's scope, so
# it sets CPACK_* variables there. `_cube_icon_dir` is that file's.
#
# SIGNING is not configured: sign the installer after CPack writes it, with a
# CPACK_POST_BUILD_SCRIPTS script that runs signtool on
# ${CPACK_PACKAGE_FILE_NAME}.exe. SmartScreen warns until it is signed.
find_program(CUBEULATOR_MAKENSIS_EXECUTABLE makensis
    HINTS "$ENV{ProgramFiles}/NSIS" "$ENV{ProgramFiles\(x86\)}/NSIS")

if(NOT CUBEULATOR_MAKENSIS_EXECUTABLE)
    message(STATUS
        "makensis not found: no Windows installer will be built "
        "(install NSIS, or pass -DCUBEULATOR_MAKENSIS_EXECUTABLE=...).")
    return()
endif()

list(APPEND CPACK_GENERATOR "NSIS")

file(TO_NATIVE_PATH "${_cube_icon_dir}/Cubeulator.ico" _cube_ico_native)
file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/packaging/windows/StampShortcut.ps1" _cube_stamp_native)

set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)

# The installer, the uninstaller and Settings > Apps all carry the icon.
set(CPACK_NSIS_MUI_ICON "${_cube_ico_native}")
set(CPACK_NSIS_MUI_UNIICON "${_cube_ico_native}")
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\Cubeulator.exe")
set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
set(CPACK_NSIS_HELP_LINK "${PROJECT_HOMEPAGE_URL}")
set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")

# One Start menu shortcut, to bin\Cubeulator.exe.
set(CPACK_PACKAGE_EXECUTABLES "Cubeulator" "Cubeulator")

# Stamp that shortcut with the AppUserModelID the app sets for itself
# (platform/windows/desktop_shell_windows.cpp), so that the taskbar groups a
# pinned shortcut with the running window. A failure here only costs that
# grouping; the installer carries on.
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    InitPluginsDir
    File \"/oname=$PLUGINSDIR\\StampShortcut.ps1\" \"${_cube_stamp_native}\"
    nsExec::Exec '\"$SYSDIR\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"$PLUGINSDIR\\StampShortcut.ps1\" -Shortcut \"$SMPROGRAMS\\$STARTMENU_FOLDER\\Cubeulator.lnk\" -AppUserModelId \"${CUBEULATOR_WINDOWS_APP_USER_MODEL_ID}\"'
    Pop $0
")
