// Progress lines for the walk: which case and section is running, and how long
// each took.
//
// Catch2's console reporter says nothing about a case until it ends, so a walk
// that hits ctest's timeout leaves a log with nothing in it - which is how the
// first Linux run of this suite ended, with no way to tell which of 29 cases
// had eaten the 900 seconds. These go to stderr and are flushed as they are
// written, so a killed run still shows where it was.

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <chrono>
#include <cstdio>

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

const Clock::time_point g_process_start = Clock::now();

class ProgressListener : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo& info) override {
        case_start_ = Clock::now();
        std::fprintf(stderr, "[system +%6.1fs] case: %s\n", seconds_since(g_process_start),
                     info.name.c_str());
        std::fflush(stderr);
    }

    void sectionStarting(const Catch::SectionInfo& info) override {
        std::fprintf(stderr, "[system +%6.1fs]   section: %s\n", seconds_since(g_process_start),
                     info.name.c_str());
        std::fflush(stderr);
    }

    void testCaseEnded(const Catch::TestCaseStats& stats) override {
        std::fprintf(stderr, "[system +%6.1fs] done (%.1fs)%s: %s\n",
                     seconds_since(g_process_start), seconds_since(case_start_),
                     stats.totals.assertions.failed > 0 ? " FAILED" : "",
                     stats.testInfo->name.c_str());
        std::fflush(stderr);
    }

private:
    Clock::time_point case_start_ = Clock::now();
};

}  // namespace

CATCH_REGISTER_LISTENER(ProgressListener)
