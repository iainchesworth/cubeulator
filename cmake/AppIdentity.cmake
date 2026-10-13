# ---------------------------------------------------------------------------
# AppIdentity.cmake - the app's names, in one place. Included (not
# include_guard'ed, so every including scope gets the variables) by
# cmake/DesktopPackaging.cmake and cmake/Packaging.cmake.
#
# The identifier is Iain's decision of 2026-10-13: one id on every platform,
# following Countdown Solver's org.iainchesworth.countdownsolver. It is the
# macOS bundle id, the Linux app id and desktop file name, and later the
# Android package and iOS bundle id. It must not change after the first
# store release. The same value is kAppId in
# src/app/desktop/desktop_integration.hpp.
# ---------------------------------------------------------------------------
set(CUBEULATOR_APP_ID "org.iainchesworth.cubeulator")
set(CUBEULATOR_APP_DISPLAY_NAME "Cubeulator")

# Windows only: groups the taskbar button with its Start menu shortcut. The
# app sets it for itself (kAppUserModelId in desktop_integration.hpp) and the
# installer stamps it on the shortcut (packaging/windows/StampShortcut.ps1).
set(CUBEULATOR_WINDOWS_APP_USER_MODEL_ID "IainChesworth.Cubeulator")
