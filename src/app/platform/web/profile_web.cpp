#include <cube/profile/recorder.hpp>
#include <cube/profiling.hpp>

#include <emscripten.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>

// The page's way to the in-process recorder (cube/profile/recorder.hpp): a
// browser cannot be connected to by Tracy, so the numbers are read from inside
// the page instead - by a person at the DevTools console, or by a test.
// profile_web.js is the page's half and defines `window.cubeulatorProfile`; the
// functions below are what it calls.
//
// This file is compiled straight into the application target (see platform.cmake,
// cube_platform_deploy_qt_runtime), not into the platform library: the API has
// to exist whether or not anything in the app refers to it, and an object file
// in a static library that nothing refers to is not linked.
//
// Whether anything is recorded depends on the build: the zones go to the
// recorder only when CUBEULATOR_ENABLE_PROFILE_RECORDER selected the recorder
// variant of cube/profiling.hpp (the web-emscripten-profiling preset). In any
// other build the API is still defined, says `recording: false`, and returns
// empty documents, so a page or a test can ask and get a straight answer.

// Provided by profile_web.js.
extern "C" {
void cube_web_profile_install(int recording);
}

namespace {

using cube::profile::Recorder;

// The page reads a string through a pointer the moment it is returned, on the
// main thread, one at a time; the buffer is replaced by the next call.
std::mutex g_text_mutex;
std::string g_text;

const char* hold(std::string text) {
    const std::scoped_lock lock(g_text_mutex);
    g_text = std::move(text);
    return g_text.c_str();
}

// The recorder keeps the pointer it is given, not a copy, because that is what
// makes recording cheap (a zone's name is a literal). A label that comes from
// the page is not a literal, so it is interned: stored once in a node-based set
// whose strings never move, bounded so that a script that mints a name per
// call cannot grow the process without end.
constexpr std::size_t kMaxLabels = 4096;
constexpr std::size_t kLabelBytes = 512;

std::mutex g_label_mutex;
std::set<std::string> g_labels;
char g_label_buffer[kLabelBytes];

const char* intern(std::string_view label) {
    const std::scoped_lock lock(g_label_mutex);
    if (g_labels.size() >= kMaxLabels && !g_labels.contains(std::string(label))) {
        return "(too many labels)";
    }
    return g_labels.emplace(label).first->c_str();
}

// Defines window.cubeulatorProfile when the module starts, from the main
// thread, before main() and so before the page can ask for it.
struct Installer {
    Installer() { cube_web_profile_install(cube::profiling::to_recorder ? 1 : 0); }
};
const Installer installer;

}  // namespace

extern "C" {

// What each returns is a NUL-terminated string valid until the next call of any
// of them; the page copies it out at once.
EMSCRIPTEN_KEEPALIVE const char* cube_web_profile_summary() {
    return hold(Recorder::global().summary_json());
}

EMSCRIPTEN_KEEPALIVE const char* cube_web_profile_trace() {
    return hold(Recorder::global().chrome_trace_json());
}

EMSCRIPTEN_KEEPALIVE const char* cube_web_profile_csv() {
    return hold(Recorder::global().zones_csv());
}

EMSCRIPTEN_KEEPALIVE void cube_web_profile_clear() {
    Recorder::global().clear();
}

// The recorder's clock, in microseconds: the same one every event is stamped
// with, so a page can place its own marks on the timeline.
EMSCRIPTEN_KEEPALIVE double cube_web_profile_now() {
    return Recorder::now_us();
}

// Where the page writes a label before calling cube_web_profile_mark.
EMSCRIPTEN_KEEPALIVE char* cube_web_profile_label_buffer() {
    return g_label_buffer;
}
EMSCRIPTEN_KEEPALIVE int cube_web_profile_label_capacity() {
    return static_cast<int>(kLabelBytes) - 1;
}

// Records the `length` bytes in the label buffer as a message on the timeline.
// A test marks where a journey's steps begin and end this way, so that a trace
// can be read against them.
EMSCRIPTEN_KEEPALIVE void cube_web_profile_mark(int length) {
    const auto size = static_cast<std::size_t>(std::clamp(length, 0, static_cast<int>(kLabelBytes) - 1));
    g_label_buffer[size] = '\0';
    Recorder::global().message(intern(std::string_view(g_label_buffer, size)));
}

}  // extern "C"
