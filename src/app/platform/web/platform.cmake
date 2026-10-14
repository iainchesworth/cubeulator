# Web: the browser, through Emscripten and Qt for WebAssembly. Included by
# cmake/Platform.cmake; the contract it fills in is described at the top of
# that file.
#
# A browser is not a desktop OS and not a phone, so it is described here rather
# than by borrowing a neighbour's fragment:
#   - there is no process of its own, no installer and no developer tools: the
#     output is a directory of static files (index.html, cube_app.js,
#     cube_app.wasm, qtloader.js) that any web server can serve;
#   - the camera is getUserMedia(), reached through the page (camera_web.js),
#     not a device node or an OS API;
#   - threads are Web Workers on shared memory, so the whole build - this
#     project, its vcpkg packages (cmake/triplets/wasm32-emscripten-threads)
#     and Qt's wasm_multithread kit - is compiled with -pthread.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "Web")
set(CUBEULATOR_PLATFORM_DESKTOP OFF)

# The test suites are not built for the browser (CUBEULATOR_BUILD_TESTS is OFF
# in the preset: Catch2 binaries have no page to run in); these are the values
# they would use if they were. A browser is always a session, so there is no
# display variable to ask for.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "wasm")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")
# A page has no top-level window of its own to make invisible.
set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW OFF)

# Qt is a cross-compiled kit here and always comes from CMAKE_PREFIX_PATH (its
# host tools from QT_HOST_PATH), as for Android: the install roots the desktop
# platforms search would find the wrong kit.
set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS "")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS "")

# Not installed by CPack: the output is a directory of static files, not an
# install tree (see the packaging note below).
set(CUBEULATOR_PLATFORM_NOTICES_DIR ".")

set(CUBEULATOR_PLATFORM_SOURCES
    platform/web/camera_web.cpp
    platform/web/paths_web.cpp
    platform/web/platform_info_web.cpp
    platform/posix/capture_thread_scope_posix.cpp
    platform/posix/crash_handler_posix.cpp)

# camera_web.js is the page's half of the camera: an Emscripten JS library, so
# camera_web.cpp calls its functions like any other and Emscripten proxies them
# to the main thread. As a link flag of the static library it reaches the app's
# link line.
set(CUBEULATOR_PLATFORM_LINK_LIBRARIES
    "--js-library=${CUBEULATOR_PLATFORM_DIR}/camera_web.js")
set(CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES "")
set(CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS "")

# Qt starts this many Web Workers up front (4 by default), and a thread asked
# for beyond them is created by the main thread's event loop, not at once. The
# app can have more than four at the same moment - the solver's table build and
# its workers, a camera start and a device listing on Qt's pool - and a thread
# that waits for another that has no worker yet is how a threaded page hangs.
#
# ENV is exported because the page passes environment variables to the app (the
# ?demo= entry in shell/index.html); Qt's loader refuses to start without it.
set(CUBEULATOR_PLATFORM_APP_PROPERTIES
    QT_WASM_PTHREAD_POOL_SIZE 8
    QT_WASM_EXTRA_EXPORTED_METHODS ENV)

function(cube_platform_prepare_dependencies)
endfunction()

# What puts the app in a page: the page itself, the worker that makes the page
# cross-origin isolated (shell/cubeulator-isolation.js says why a static host
# needs one) and the icon, next to the .js/.wasm/qtloader.js Qt writes.
# Together they are the whole site: serve the directory.
#
# And its fonts. A browser has no system fonts and the face Qt bundles has no
# Japanese or Chinese, so the two the app is written in (fonts/, cut down to the
# characters the catalogues use; tools/fonts/subset_cjk.py) are compiled in
# under :/fonts, where main.cpp registers every font it finds.
function(cube_platform_deploy_qt_runtime target qml_dir)
    qt_add_resources(${target} "web_cjk_fonts"
        PREFIX "/fonts"
        BASE "${CUBEULATOR_PLATFORM_DIR}/fonts"
        FILES
            "${CUBEULATOR_PLATFORM_DIR}/fonts/NotoSansJP-Subset.ttf"
            "${CUBEULATOR_PLATFORM_DIR}/fonts/NotoSansSC-Subset.ttf")

    set(shell "${CUBEULATOR_PLATFORM_DIR}/shell")
    set(out "$<TARGET_FILE_DIR:${target}>")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${shell}/index.html" "${out}/index.html"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${shell}/cubeulator-isolation.js" "${out}/cubeulator-isolation.js"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/docs/design/identity/icon.svg" "${out}/icon.svg"
        COMMENT "Staging the page shell next to ${target}.js"
        VERBATIM)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${CUBEULATOR_PLATFORM_DIR}/camera_web.js")
endfunction()

# Qt is linked into the one .wasm module: there is nothing to put beside it.
function(cube_platform_deploy_qt_library target qt_target)
endfunction()

# The "package" is the directory above; hosting it is the publisher's business
# (the docs workflow puts it under the documentation site).
macro(cube_platform_configure_packaging)
endmacro()

# A page has no desktop shell of its own: the browser owns the window and the
# tab's identity. The shared library still builds, with the implementation
# phones use, which does nothing (the screens' keep-awake request is answered
# "not held"). The Screen Wake Lock API is what a web implementation would
# call, from a file of this directory.
function(cube_platform_configure_desktop_shell target)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/app/platform/posix/desktop_shell_mobile.cpp")
endfunction()

# Nothing to install: the app is the directory above, not an installed program
# (and cube_app is not a desktop one, so this hook is not even called).
function(cube_platform_configure_desktop_app target)
endfunction()
