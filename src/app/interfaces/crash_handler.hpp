#pragma once

namespace cube::platform::crash_handler {

// Installs this platform's last-resort crash reporter. Called once, first
// thing in main(), so a fault during startup is captured too. Where the
// platform has nothing to add to what the OS already records, this does
// nothing.
//
// There is exactly one definition of `install()` per platform, living in
// platform/<os>/crash_handler_<os>.cpp (platform/posix/ for the platforms
// that have no handler of their own). CMake compiles only the file the
// platform's fragment names, so the code contains no preprocessor branching.
void install() noexcept;

}  // namespace cube::platform::crash_handler
