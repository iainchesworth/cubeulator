// Test cases only Windows can run: where the per-user directories are, which
// paths_windows.cpp decides. Compiled into cube_integration_tests only on
// Windows (tests/CMakeLists.txt picks tests/platform/<platform name>/), so
// this file needs no guard of its own.
#include <catch2/catch_test_macros.hpp>

#include <interfaces/paths.hpp>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace {

// Sets an environment variable for the length of a scope and puts it back, so
// that the cases can see paths_windows.cpp read each branch without leaking
// into the others. A null value removes the variable.
class ScopedEnvVar {
public:
    // name/value are literals at every call site, so a swap is not a risk.
    ScopedEnvVar(const char* name,  // NOLINT(bugprone-easily-swappable-parameters)
                 const char* value)
        : name_(name) {
        if (const char* existing = std::getenv(name)) {
            previous_ = existing;
        }
        // _putenv_s with an empty value removes the variable.
        _putenv_s(name_.c_str(), value != nullptr ? value : "");
    }

    ~ScopedEnvVar() { _putenv_s(name_.c_str(), previous_ ? previous_->c_str() : ""); }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
    ScopedEnvVar(ScopedEnvVar&&) = delete;
    ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

}  // namespace

TEST_CASE("paths::current puts the diagnostics under the local, not the roaming, profile",
          "[integration][platform][windows]") {
    ScopedEnvVar local("LOCALAPPDATA", "C:/cubeulator-test/local");
    ScopedEnvVar roaming("APPDATA", "C:/cubeulator-test/roaming");

    const auto paths = cube::platform::paths::current();
    CHECK(paths.diagnostics_dir ==
          std::filesystem::path("C:/cubeulator-test/local") / "Cubeulator" / "Diagnostics");
    // Settings roam; a log of this machine's camera does not.
    CHECK(paths.data_dir == std::filesystem::path("C:/cubeulator-test/roaming") / "Cubeulator");
    CHECK(paths.cache_dir ==
          std::filesystem::path("C:/cubeulator-test/local") / "Cubeulator" / "Cache");
}

TEST_CASE("paths::current has no diagnostics directory without a local profile",
          "[integration][platform][windows]") {
    ScopedEnvVar local("LOCALAPPDATA", nullptr);

    const auto paths = cube::platform::paths::current();
    CHECK(paths.diagnostics_dir.empty());
}
