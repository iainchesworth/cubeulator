# vcpkg triplet for the threaded WebAssembly build (preset web-emscripten-*).
#
# vcpkg's own wasm32-emscripten is single-threaded. The app is built against
# Qt's wasm_multithread kit, whose objects are compiled with -pthread
# (atomics + bulk-memory, shared memory), and wasm-ld refuses to link an
# object that was not: "--shared-memory is disallowed by <object> because it
# was not compiled with 'atomics' or 'bulk-memory' features". So every port
# is built with -pthread too. Nothing else differs from the stock triplet,
# which is included as is.
include("${VCPKG_ROOT_DIR}/triplets/community/wasm32-emscripten.cmake")

# Release only. The web app links the release libraries whatever its own
# configuration (CMake falls back to the one configuration an imported target
# has), and a second, debug, build of every port is the same work again: on
# WebAssembly, where each configure step is an Emscripten link, that is the
# larger part of what vcpkg spends.
set(VCPKG_BUILD_TYPE release)

string(APPEND VCPKG_C_FLAGS " -pthread")
string(APPEND VCPKG_CXX_FLAGS " -pthread")
string(APPEND VCPKG_LINKER_FLAGS " -pthread")

# C++ exceptions, in the mode Qt's kit uses (JavaScript-based, -fexceptions).
# Emscripten compiles them out by default: a `throw` becomes a call that aborts
# the program and a `catch` is never entered, so the app's own handlers (a
# degenerate camera frame is to drop one frame, not the scan; the solver's table
# build joins its workers behind a catch) would not be there, an OpenCV error
# would end the page rather than the call, and Catch2 - which reports a failed
# check by throwing - would abort on the first one. The mode has to be Qt's: the
# kit is built with -fexceptions, and an object built with -fwasm-exceptions
# cannot be linked with it (the two use different C++ ABIs for unwinding:
# "undefined symbol: __resumeException"). Every object that throws or catches
# must be built with it, which is why this is here as well as in the platform
# fragment.
string(APPEND VCPKG_C_FLAGS " -fexceptions")
string(APPEND VCPKG_CXX_FLAGS " -fexceptions")
string(APPEND VCPKG_LINKER_FLAGS " -fexceptions")

# Two options only OpenCV reads (every other port ignores them):
#
#  WITH_PTHREADS_PF=OFF   OpenCV's own thread pool. Left on (its default under
#      Emscripten with threads), the first cv::cvtColor/resize of a frame would
#      split the work over a pool of Web Workers - and the vision reader runs
#      on the GUI thread, which is the browser's main thread. A Web Worker is
#      created by the main thread's event loop, so a main thread that waits
#      for the workers it just asked for waits for ever. A 640 px frame is
#      small enough to be worked on serially, and the app has its threads (the
#      solver, the camera) where it wants them.
#
#  BUILD_LIST   the modules the app uses - core, imgproc, imgcodecs and videoio
#      (whose headers the platform layer includes) - and nothing they do not
#      need: no calib3d/features2d/flann and none of the rest of what OpenCV
#      builds by default. Each is minutes of compiling in WebAssembly for code
#      the app never calls.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    -DWITH_PTHREADS_PF=OFF
    -DBUILD_LIST=core,imgproc,imgcodecs,videoio)
