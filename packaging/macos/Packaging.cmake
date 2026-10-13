# The macOS CPack generator: a .dmg holding Cubeulator.app.
#
# Included by cube_platform_configure_packaging() in
# src/app/platform/macos/platform.cmake, in cmake/Packaging.cmake's scope, so
# it sets CPACK_* variables there. `_cube_icon_dir` is that file's.
#
# SIGNING: the bundle is signed ad hoc, which Apple Silicon needs to run at
# all. For a real signature set CUBEULATOR_MACOS_CODESIGN_IDENTITY
# (packaging/macos/ConfigureApp.cmake), then notarise the .dmg with notarytool.
# Until then Gatekeeper needs the user's right-click > Open.
list(APPEND CPACK_GENERATOR "DragNDrop")
set(CPACK_DMG_VOLUME_NAME "Cubeulator")
set(CPACK_DMG_FORMAT "UDZO")
set(CPACK_PACKAGE_ICON "${_cube_icon_dir}/Cubeulator.icns")
