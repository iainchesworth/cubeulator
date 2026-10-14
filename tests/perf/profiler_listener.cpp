// Time for a profiler to attach before the work starts and to drain after it
// ends, for tools/perf/tracy_check.py. Nothing happens unless the environment
// asks, so a plain run - ctest, a developer's terminal, CI - is unchanged.
//
//   CUBE_PERF_PROFILER_SETTLE_MS  sleep this long before the first case, so
//                                 that a Tracy capture has attached: the
//                                 client is on-demand and records nothing
//                                 until one has, and a count that depends on
//                                 that race is no baseline.
//   CUBE_PERF_PROFILER_DRAIN_MS   sleep this long after the last case. The
//                                 client answers the capture's queries for
//                                 zone names and sources from a thread of its
//                                 own, a few at a time; a process that exits
//                                 when the work does leaves the ones still
//                                 queued unanswered, and the capture shows
//                                 those zones as "???".

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace {

void sleep_for_environment(const char* variable) {
    const char* value = std::getenv(variable);
    if (value == nullptr) {
        return;
    }
    const long milliseconds = std::strtol(value, nullptr, 10);
    if (milliseconds > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

class ProfilerListener final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
        sleep_for_environment("CUBE_PERF_PROFILER_SETTLE_MS");
    }

    void testRunEnded(const Catch::TestRunStats& /*stats*/) override {
        sleep_for_environment("CUBE_PERF_PROFILER_DRAIN_MS");
    }
};

}  // namespace

CATCH_REGISTER_LISTENER(ProfilerListener)
