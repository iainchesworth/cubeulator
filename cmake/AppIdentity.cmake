# ---------------------------------------------------------------------------
# AppIdentity.cmake - the app's names, in one place. Included (not
# include_guard'ed, so every including scope gets the variables) by
# cmake/DesktopPackaging.cmake and cmake/Packaging.cmake.
#
# The identifier follows its repository's account (the maintainer, 2026-10-13, after the
# repository moved to iainchesworthlabs): com.iainchesworthlabs.cubeulator,
# like ICL Forge's macOS apps. Until that move it had the "org" prefix of
# Countdown Solver and the old account. It is the macOS bundle id, the Linux app
# id and desktop file name, and later the Android package and iOS bundle id.
# It must not change after the first store release. The same value is kAppId
# in src/app/desktop/desktop_integration.hpp.
set(CUBEULATOR_APP_ID "com.iainchesworthlabs.cubeulator")
set(CUBEULATOR_APP_DISPLAY_NAME "Cubeulator")

# Windows only: groups the taskbar button with its Start menu shortcut. The
# app sets it for itself (kAppUserModelId in desktop_integration.hpp) and the
# installer stamps it on the shortcut (packaging/windows/StampShortcut.ps1).
set(CUBEULATOR_WINDOWS_APP_USER_MODEL_ID "IainChesworthLabs.Cubeulator")

# The publisher: the Windows version resource's CompanyName, the AppStream
# developer, and the CPack vendor and contact (so the installer's "Publisher"
# and the .deb/.rpm maintainer). One string, so that it is one line to change
# (docs/design/family.md, F1).
#
# "Iain Chesworth Labs" follows the repository's account, which moved from
# iainchesworth to iainchesworthlabs on 2026-10-13 (develop, PR #6). Before
# the move it was the person's own name (family.md D1: the publisher follows
# the account).
set(CUBEULATOR_PUBLISHER "Iain Chesworth Labs")
# The copyright line stays in the person's name whatever the publisher is
# (family.md D1/F1).
set(CUBEULATOR_COPYRIGHT_HOLDER "Iain Chesworth")
set(CUBEULATOR_COPYRIGHT "Copyright (C) 2026 ${CUBEULATOR_COPYRIGHT_HOLDER}")
# AppStream's developer id: reverse DNS of the account the project lives under.
set(CUBEULATOR_DEVELOPER_ID "com.github.iainchesworthlabs")
