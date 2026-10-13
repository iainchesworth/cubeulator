// Test cases only Linux can run: its XDG directory conventions, which
// paths_linux.cpp implements. Compiled into
// cube_integration_tests only on Linux (tests/CMakeLists.txt picks
// tests/platform/<platform name>/), so this file needs no guard of its own.
#include <catch2/catch_test_macros.hpp>

#include <interfaces/paths.hpp>

#include <cstdlib>
#include <optional>
#include <string>

namespace {

// Saves an environment variable's value on construction and restores it on
// destruction, so tests can exercise both the XDG-set and XDG-unset branches
// of paths_linux.cpp without leaking state into
// other tests. A null value unsets the variable for the scope's duration.
class ScopedEnvVar {
public:
    // name/value are always passed as literals at the call site (the
    // variable being overridden, and its temporary value), so a same-type
    // swap isn't a realistic risk here.
    ScopedEnvVar(const char* name,  // NOLINT(bugprone-easily-swappable-parameters)
                 const char* value)
        : name_(name) {
        if (const char* existing = std::getenv(name)) {
            previous_ = existing;
        }
        if (value != nullptr) {
            setenv(name_.c_str(), value, 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    ~ScopedEnvVar() {
        if (previous_) {
            setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
    ScopedEnvVar(ScopedEnvVar&&) = delete;
    ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

}  // namespace

TEST_CASE("paths::current prefers XDG_CONFIG_HOME/XDG_CACHE_HOME when set",
          "[integration][platform][linux]") {
    ScopedEnvVar config_home("XDG_CONFIG_HOME", "/tmp/cubeulator-test-config");
    ScopedEnvVar cache_home("XDG_CACHE_HOME", "/tmp/cubeulator-test-cache");

    const auto paths = cube::platform::paths::current();
    REQUIRE(paths.data_dir == "/tmp/cubeulator-test-config/cubeulator");
    REQUIRE(paths.cache_dir == "/tmp/cubeulator-test-cache/cubeulator");
}

TEST_CASE("paths::current falls back to an empty path when neither XDG nor HOME is set",
          "[integration][platform][linux]") {
    ScopedEnvVar config_home("XDG_CONFIG_HOME", nullptr);
    ScopedEnvVar cache_home("XDG_CACHE_HOME", nullptr);
    ScopedEnvVar home("HOME", nullptr);

    const auto paths = cube::platform::paths::current();
    REQUIRE(paths.data_dir.empty());
    REQUIRE(paths.cache_dir.empty());
}
