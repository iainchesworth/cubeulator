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
# A native camera device, opened from the calling thread? (cmake/Platform.cmake)
set(CUBEULATOR_PLATFORM_TEST_NATIVE_CAMERA OFF)

# The test suites that need no window - the unit, integration and performance
# binaries, which link the library and the platform layer and no Qt - are built
# for WebAssembly and run under Node (the web-emscripten-tests preset and
# `ctest`, with Emscripten's node as the cross-compiling emulator). Those that
# draw the app are not: the app in a page is tests/web's, driven through a
# browser with Playwright. A browser is always a session, so there is no
# display variable to ask for.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "wasm")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")
# A page has no top-level window of its own to make invisible.
set(CUBEULATOR_PLATFORM_TEST_HIDE_WINDOW OFF)

# What a WebAssembly program is told that a native one is not:
#   - how many threads it may have. They are Web Workers, created by the main
#     thread's event loop, and Node's main thread is blocked in a join while
#     it waits, so a thread that has no worker yet is a deadlock: the pool
#     has to be big enough that none is waited for (the biggest test starts a
#     handful; sixteen is generous and costs about a hundred milliseconds);
#   - how much stack: Emscripten's default is 64 KB, which the solver's
#     recursive search does not fit in;
#   - how much memory, and that it may grow: the two-phase tables and the
#     breadth-first searches' arenas are tens of megabytes;
#   - that it is run by node and its exit status is the test's (without
#     EXIT_RUNTIME the process is left to its workers and ctest hangs);
#   - that its files are the host's (NODERAWFS): ctest has Catch2 write the
#     list of test cases to a file of its own and read it back, and a program
#     on Emscripten's in-memory file system writes to somewhere only it can see.
set(CUBEULATOR_PLATFORM_TEST_LINK_OPTIONS
    -pthread
    -fexceptions
    -sPTHREAD_POOL_SIZE=16
    -sSTACK_SIZE=4MB
    -sDEFAULT_PTHREAD_STACK_SIZE=2MB
    -sINITIAL_MEMORY=256MB
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=2GB
    -sENVIRONMENT=node
    -sNODERAWFS=1
    -sEXIT_RUNTIME=1)

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
# link line. profile_web.js is the page's half of the profiling export
# (profile_web.cpp, compiled into the app itself: see
# cube_platform_deploy_qt_runtime).
#
# A diagnostics build (docs/profiling.md, docs/testing.md) adds to that:
#   - with the recorder behind the zone macros (CUBEULATOR_ENABLE_PROFILE_RECORDER)
#     the function names are kept in the module (--profiling-funcs), so a trace
#     and DevTools show "solver::bfs_search" and not "wasm-function[4171]",
#     without the size of full DWARF;
#   - CUBEULATOR_WEB_TEST_DRIVER links the read-only driver the Playwright suite
#     uses (driver_web.cpp, driver_web.js), which the published page does not
#     carry.
set(CUBEULATOR_PLATFORM_LINK_LIBRARIES
    -fexceptions
    "--js-library=${CUBEULATOR_PLATFORM_DIR}/camera_web.js"
    "--js-library=${CUBEULATOR_PLATFORM_DIR}/profile_web.js")
if(CUBEULATOR_ENABLE_PROFILE_RECORDER)
    list(APPEND CUBEULATOR_PLATFORM_LINK_LIBRARIES "--profiling-funcs")
endif()
if(CUBEULATOR_WEB_TEST_DRIVER)
    list(APPEND CUBEULATOR_PLATFORM_LINK_LIBRARIES
        "--js-library=${CUBEULATOR_PLATFORM_DIR}/driver_web.js")
endif()
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

# The module is one shared-memory image, so every object in it has to be
# compiled with atomics and bulk memory: wasm-ld refuses "--shared-memory"
# otherwise ("disallowed by <object> because it was not compiled with 'atomics'
# or 'bulk-memory' features"), and code compiled without them turns
# std::atomic and friends into plain loads and stores that other threads then
# race on. Qt's targets carry -pthread through the kit and the vcpkg ports get
# it from the triplet; this is the project's own - the library and the platform
# layer link no Qt and had none. A directory property, set here because this
# runs before any of the project's targets exist.
#
# -fexceptions likewise (the triplet says why): the compiler's default is to
# compile `throw` to an abort and `catch` to nothing, and the same flag has to
# be on the link line, which selects the C++ runtime that has them.
function(cube_platform_prepare_dependencies)
    add_compile_options(-pthread -fexceptions)
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
#
# And the profiling export (window.cubeulatorProfile, docs/profiling.md). It
# goes straight onto the application target rather than into the platform
# library: nothing in the app refers to it, and an object file in a static
# library that nothing refers to is not linked.
function(cube_platform_deploy_qt_runtime target qml_dir)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/app/platform/web/profile_web.cpp")
    if(CUBEULATOR_WEB_TEST_DRIVER)
        target_sources(${target} PRIVATE
            "${PROJECT_SOURCE_DIR}/src/app/platform/web/driver_web.cpp")
    endif()

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
        "${CUBEULATOR_PLATFORM_DIR}/camera_web.js"
        "${CUBEULATOR_PLATFORM_DIR}/profile_web.js"
        "${CUBEULATOR_PLATFORM_DIR}/driver_web.js")
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
