#pragma once

// An in-process timing recorder: the profiling macros' (cube/profiling.hpp)
// answer where Tracy cannot be used.
//
// Tracy's client opens a TCP listener and answers UDP discovery, which a
// browser tab cannot do (docs/profiling.md, "In the browser"). This records
// the same events - zones, frames, messages, plots, thread names - into a
// fixed ring of lock-free slots inside the process, and writes them out on
// request as
//
//   * Chrome's Trace Event Format JSON: loads in the DevTools Performance
//     panel ("Load profile"), in Perfetto, and, through Tracy's
//     tracy-import-chrome, in the Tracy GUI;
//   * CSV in tracy-csvexport's columns, so tools/perf/tracy_check.py and any
//     script written against a Tracy export read it unchanged;
//   * a small JSON summary of the same statistics, for a test to read.
//
// It is plain standard C++ and has no platform in it: it is built on every
// platform and unit-tested natively. What selects it as the macros'
// implementation is CMake (CUBEULATOR_ENABLE_PROFILE_RECORDER, the
// profiling-recorder variant of cube/profiling.hpp); the browser build is its
// first user, and the only code that is web-specific is the thin export that
// hands the three outputs to the page (src/app/platform/web/profile_web.cpp).
//
// Cost. Recording a zone is two clock reads and a handful of relaxed atomic
// stores; nothing allocates and nothing locks on the recording path. Reading
// (the three outputs) walks the ring and may be called while other threads
// record: a slot being rewritten is skipped, not read torn. (One writer would
// have to be a whole lap of the ring behind another for two to share a slot,
// which at the default capacity is 131072 events inside one writer's five
// stores; nothing guards against it.)

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cube::profile {

enum class Kind : std::uint8_t { zone, frame, message, plot };

// One recorded event, as read back. `name` points at the literal the caller
// gave (Tracy's rule too: names are string literals, so the pointer is kept,
// not a copy).
struct Event {
    Kind kind = Kind::zone;
    const char* name = nullptr;
    std::uint32_t thread = 0;
    double start_us = 0.0;     // since the recorder's epoch
    double duration_us = 0.0;  // zones
    double value = 0.0;        // a plot's value, or the number attached to a zone
    bool has_value = false;
};

// Per zone name, over the events still in the ring.
struct ZoneStats {
    std::string name;
    std::uint64_t count = 0;
    double total_us = 0.0;
    double mean_us = 0.0;
    double min_us = 0.0;
    double max_us = 0.0;
    double stddev_us = 0.0;
};

class Recorder {
public:
    // `capacity` is rounded up to a power of two. 2^17 slots of 48 bytes is
    // about 6 MB: a quarter of an hour of a scan at 30 frames a second with
    // ten zones a frame, after which the oldest events are overwritten (and
    // counted, see dropped()).
    explicit Recorder(std::size_t capacity = std::size_t{1} << 17U);
    ~Recorder() = default;

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    // The process-wide recorder the macros write to.
    [[nodiscard]] static Recorder& global();

    // Microseconds on a monotonic clock, from a fixed epoch taken the first
    // time anyone asks. The same clock on every thread, so one timeline.
    [[nodiscard]] static double now_us() noexcept;

    void zone(const char* name, double start_us, double end_us) noexcept;
    void zone_with_value(const char* name, double start_us, double end_us,
                         std::uint64_t value) noexcept;
    void frame(const char* set) noexcept;
    void message(const char* text) noexcept;
    void plot(const char* name, double value) noexcept;

    // Names the calling thread in the trace. Copies `name`.
    void set_thread_name(std::string_view name);
    // Free text for the trace's metadata (version, commit). Copies `text`.
    void set_app_info(std::string_view text);

    // The events still in the ring, oldest first.
    [[nodiscard]] std::vector<Event> snapshot() const;
    // Every event ever recorded, and how many of them were overwritten.
    [[nodiscard]] std::uint64_t recorded() const noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept;
    // Forgets everything recorded. Call it when nothing is recording.
    void clear() noexcept;

    [[nodiscard]] std::vector<ZoneStats> zone_stats() const;
    [[nodiscard]] std::string zones_csv() const;
    [[nodiscard]] std::string chrome_trace_json() const;
    [[nodiscard]] std::string summary_json() const;

private:
    // An event is five words and a sequence number, all atomic, so a reader
    // sees either a whole event or none (the sequence is odd while a writer
    // has the slot and holds the event's index when it is whole). The name is
    // the first word, held as the pointer it is.
    struct Slot {
        std::atomic<std::uint64_t> sequence{0};
        std::atomic<const char*> name{nullptr};
        std::array<std::atomic<std::uint64_t>, 4> words{};
    };

    void record(Kind kind, const char* name, double start_us, double duration_us, double value,
                bool has_value) noexcept;
    [[nodiscard]] static std::uint32_t this_thread() noexcept;

    std::size_t mask_;
    std::vector<Slot> slots_;
    std::atomic<std::uint64_t> head_{0};

    mutable std::mutex names_mutex_;
    std::vector<std::pair<std::uint32_t, std::string>> thread_names_;
    std::string app_info_;
};

}  // namespace cube::profile
