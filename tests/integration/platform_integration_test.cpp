#include <catch2/catch_test_macros.hpp>

#include <interfaces/camera.hpp>
#include <interfaces/gpu_surface.hpp>
#include <interfaces/inference_env.hpp>
#include <interfaces/paths.hpp>

#include <algorithm>

#ifdef __linux__
#include <cstdlib>
#include <optional>
#include <string>

namespace {

// Saves an environment variable's value on construction and restores it on
// destruction, so tests can exercise both the XDG-set and XDG-unset branches
// of paths_linux.cpp / inference_env_linux.cpp without leaking state into
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
#endif

TEST_CASE("the platform-selected paths and inference_env implementations agree on a build",
          "[integration][platform]") {
    // Exercises the CMake-selected per-OS translation units end to end,
    // rather than any single interface in isolation.
    const auto paths = cube::platform::paths::current();
    const auto env = cube::platform::inference_env::current();

    REQUIRE_FALSE(env.execution_providers.empty());
    // Every platform's provider list includes a CPU fallback.
    REQUIRE(std::ranges::find(env.execution_providers, "CPU") != env.execution_providers.end());

    (void)paths;  // Desktop platforms populate this; mobile stubs may not yet.
}

TEST_CASE("camera enumeration returns a result rather than throwing", "[integration][platform]") {
    const auto devices = cube::platform::camera::enumerate_devices();
    REQUIRE(devices.has_value());
}

TEST_CASE("starting a camera without a real device reports an error, not a crash",
          "[integration][platform]") {
    const auto result = cube::platform::camera::start(
        cube::platform::camera::DeviceInfo{.identifier = "none", .display_name = "none"});
    REQUIRE_FALSE(result.has_value());
    cube::platform::camera::stop();
}

TEST_CASE("gpu_surface::current wraps a native handle without crashing",
          "[integration][platform]") {
    int fake_native_window = 0;
    const auto surface = cube::platform::gpu_surface::current(&fake_native_window);
    REQUIRE(surface.has_value());
    REQUIRE(surface->native_handle == &fake_native_window);
}

#ifdef __linux__
TEST_CASE("paths::current prefers XDG_CONFIG_HOME/XDG_CACHE_HOME when set",
          "[integration][platform][linux]") {
    ScopedEnvVar config_home("XDG_CONFIG_HOME", "/tmp/cubeulator-test-config");
    ScopedEnvVar cache_home("XDG_CACHE_HOME", "/tmp/cubeulator-test-cache");

    const auto paths = cube::platform::paths::current();
    REQUIRE(paths.data_dir == "/tmp/cubeulator-test-config/cubeulator");
    REQUIRE(paths.cache_dir == "/tmp/cubeulator-test-cache/cubeulator");
}

TEST_CASE("inference_env::current prefers XDG_DATA_HOME when set",
          "[integration][platform][linux]") {
    ScopedEnvVar data_home("XDG_DATA_HOME", "/tmp/cubeulator-test-data");

    const auto env = cube::platform::inference_env::current();
    REQUIRE(env.model_search_path == "/tmp/cubeulator-test-data/cubeulator/models");
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

TEST_CASE("inference_env::current falls back to an empty path when neither XDG nor HOME is set",
          "[integration][platform][linux]") {
    ScopedEnvVar data_home("XDG_DATA_HOME", nullptr);
    ScopedEnvVar home("HOME", nullptr);

    const auto env = cube::platform::inference_env::current();
    REQUIRE(env.model_search_path.empty());
}
#endif
