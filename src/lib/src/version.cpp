#include <cube/version.hpp>

#include <fmt/format.h>

namespace cube {

namespace {

std::string build_target() {
#if defined(_WIN32)
    constexpr std::string_view os = "Windows";
#elif defined(__APPLE__)
    constexpr std::string_view os = "macOS";
#elif defined(__ANDROID__)
    constexpr std::string_view os = "Android";
#elif defined(__linux__)
    constexpr std::string_view os = "Linux";
#else
    constexpr std::string_view os = "unknown-os";
#endif

#if defined(_M_X64) || defined(__x86_64__)
    constexpr std::string_view arch = "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    constexpr std::string_view arch = "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    constexpr std::string_view arch = "x86";
#else
    constexpr std::string_view arch = "unknown-arch";
#endif

#if defined(__clang__)
    return fmt::format("{} {} (Clang {}.{}.{})", os, arch, __clang_major__, __clang_minor__,
                        __clang_patchlevel__);
#elif defined(_MSC_VER)
    return fmt::format("{} {} (MSVC {})", os, arch, _MSC_VER);
#elif defined(__GNUC__)
    return fmt::format("{} {} (GCC {}.{}.{})", os, arch, __GNUC__, __GNUC_MINOR__,
                        __GNUC_PATCHLEVEL__);
#else
    return fmt::format("{} {} (unknown compiler)", os, arch);
#endif
}

}  // namespace

std::string version_details() {
    return fmt::format(
        "Cubeulator {}\n"
        "  release: {}\n"
        "  commit:  {}\n"
        "  branch:  {}\n"
        "  target:  {}{}",
        version_string,
        git_describe,
        git_commit_full,
        git_branch,
        build_target(),
        git_dirty ? "\n  state:   dirty (uncommitted changes)" : "");
}

}  // namespace cube
