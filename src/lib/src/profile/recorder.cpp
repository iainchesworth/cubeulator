#include <cube/profile/recorder.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string_view>
#include <utility>

namespace cube::profile {

namespace {

// Word 4 of a slot: the thread, the kind and whether a value is attached.
constexpr std::uint64_t kHasValue = std::uint64_t{1} << 40;

std::uint64_t pack_meta(std::uint32_t thread, Kind kind, bool has_value) noexcept {
    return static_cast<std::uint64_t>(thread) | (static_cast<std::uint64_t>(kind) << 32) |
           (has_value ? kHasValue : 0U);
}

std::uint64_t bits(double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

double real(std::uint64_t word) noexcept {
    return std::bit_cast<double>(word);
}

std::string number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.3f", value);
    return buffer;
}

void append_escaped(std::string& out, std::string_view text) {
    for (const char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
}

std::string json_string(std::string_view text) {
    std::string out = "\"";
    append_escaped(out, text);
    out += '"';
    return out;
}

// The epoch is taken the first time any thread asks, and the same value is
// read by every thread afterwards.
std::chrono::steady_clock::time_point epoch() {
    static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    return start;
}

// A CSV field: zone names are dotted lower case, but a quote or comma in one
// must not break the row.
std::string csv_field(std::string_view text) {
    if (text.find_first_of(",\"\n") == std::string_view::npos) {
        return std::string(text);
    }
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') {
            out += '"';
        }
        out += c;
    }
    out += '"';
    return out;
}

}  // namespace

Recorder::Recorder(std::size_t capacity)
    : mask_(std::bit_ceil(std::max<std::size_t>(capacity, 2U)) - 1U),
      slots_(std::make_unique<Slot[]>(mask_ + 1U)) {
    (void)epoch();
}

Recorder& Recorder::global() {
    static Recorder instance;
    return instance;
}

double Recorder::now_us() noexcept {
    const auto elapsed = std::chrono::steady_clock::now() - epoch();
    return std::chrono::duration<double, std::micro>(elapsed).count();
}

std::uint32_t Recorder::this_thread() noexcept {
    static std::atomic<std::uint32_t> next{1};
    thread_local const std::uint32_t id = next.fetch_add(1, std::memory_order_relaxed);
    return id;
}

void Recorder::record(Kind kind, const char* name, double start_us, double duration_us,
                      double value, bool has_value) noexcept {
    const std::uint64_t index = head_.fetch_add(1, std::memory_order_relaxed);
    Slot& slot = slots_[index & mask_];

    // A seqlock whose data words are atomic: the sequence is odd while this
    // thread is writing and holds 2 * index + 2 when the event is whole.
    slot.sequence.store(2U * index + 1U, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.words[0].store(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(name)),
                        std::memory_order_relaxed);
    slot.words[1].store(bits(start_us), std::memory_order_relaxed);
    slot.words[2].store(bits(duration_us), std::memory_order_relaxed);
    slot.words[3].store(bits(value), std::memory_order_relaxed);
    slot.words[4].store(pack_meta(this_thread(), kind, has_value), std::memory_order_relaxed);
    slot.sequence.store(2U * index + 2U, std::memory_order_release);
}

void Recorder::zone(const char* name, double start_us, double end_us) noexcept {
    record(Kind::zone, name, start_us, end_us - start_us, 0.0, false);
}

void Recorder::zone_with_value(const char* name, double start_us, double end_us,
                               std::uint64_t value) noexcept {
    record(Kind::zone, name, start_us, end_us - start_us, static_cast<double>(value), true);
}

void Recorder::frame(const char* set) noexcept {
    record(Kind::frame, set, now_us(), 0.0, 0.0, false);
}

void Recorder::message(const char* text) noexcept {
    record(Kind::message, text, now_us(), 0.0, 0.0, false);
}

void Recorder::plot(const char* name, double value) noexcept {
    record(Kind::plot, name, now_us(), 0.0, value, true);
}

void Recorder::set_thread_name(std::string_view name) {
    const std::uint32_t id = this_thread();
    const std::scoped_lock lock(names_mutex_);
    for (auto& entry : thread_names_) {
        if (entry.first == id) {
            entry.second = std::string(name);
            return;
        }
    }
    thread_names_.emplace_back(id, std::string(name));
}

void Recorder::set_app_info(std::string_view text) {
    const std::scoped_lock lock(names_mutex_);
    app_info_ = std::string(text);
}

std::vector<Event> Recorder::snapshot() const {
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    const std::uint64_t capacity = static_cast<std::uint64_t>(mask_) + 1U;
    const std::uint64_t count = std::min(head, capacity);

    std::vector<Event> events;
    events.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = head - count; index < head; ++index) {
        const Slot& slot = slots_[index & mask_];
        const std::uint64_t before = slot.sequence.load(std::memory_order_acquire);
        if (before != 2U * index + 2U) {
            continue;  // being written, or already overwritten by a later lap
        }
        std::uint64_t words[5];
        for (std::size_t i = 0; i < 5; ++i) {
            words[i] = slot.words[i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.sequence.load(std::memory_order_relaxed) != before) {
            continue;
        }

        Event event;
        event.name = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(words[0]));
        event.start_us = real(words[1]);
        event.duration_us = real(words[2]);
        event.value = real(words[3]);
        event.thread = static_cast<std::uint32_t>(words[4] & 0xFFFFFFFFU);
        event.kind = static_cast<Kind>((words[4] >> 32) & 0xFFU);
        event.has_value = (words[4] & kHasValue) != 0;
        events.push_back(event);
    }
    std::ranges::stable_sort(events, {}, &Event::start_us);
    return events;
}

std::uint64_t Recorder::recorded() const noexcept {
    return head_.load(std::memory_order_acquire);
}

std::uint64_t Recorder::dropped() const noexcept {
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    const std::uint64_t capacity = static_cast<std::uint64_t>(mask_) + 1U;
    return head > capacity ? head - capacity : 0U;
}

void Recorder::clear() noexcept {
    head_.store(0, std::memory_order_release);
}

std::vector<ZoneStats> Recorder::zone_stats() const {
    struct Accumulator {
        std::uint64_t count = 0;
        double total = 0.0;
        double min = 0.0;
        double max = 0.0;
        double mean = 0.0;  // Welford
        double m2 = 0.0;
    };
    std::map<std::string, Accumulator> by_name;
    for (const Event& event : snapshot()) {
        if (event.kind != Kind::zone || event.name == nullptr) {
            continue;
        }
        Accumulator& acc = by_name[event.name];
        const double d = event.duration_us;
        if (acc.count == 0) {
            acc.min = d;
            acc.max = d;
        } else {
            acc.min = std::min(acc.min, d);
            acc.max = std::max(acc.max, d);
        }
        ++acc.count;
        acc.total += d;
        const double delta = d - acc.mean;
        acc.mean += delta / static_cast<double>(acc.count);
        acc.m2 += delta * (d - acc.mean);
    }

    std::vector<ZoneStats> stats;
    stats.reserve(by_name.size());
    for (const auto& [name, acc] : by_name) {
        ZoneStats entry;
        entry.name = name;
        entry.count = acc.count;
        entry.total_us = acc.total;
        entry.mean_us = acc.mean;
        entry.min_us = acc.min;
        entry.max_us = acc.max;
        entry.stddev_us = acc.count > 1 ? std::sqrt(acc.m2 / static_cast<double>(acc.count)) : 0.0;
        stats.push_back(std::move(entry));
    }
    return stats;
}

std::string Recorder::zones_csv() const {
    const std::vector<Event> events = snapshot();
    double first = 0.0;
    double last = 0.0;
    bool any = false;
    for (const Event& event : events) {
        const double end = event.start_us + event.duration_us;
        first = any ? std::min(first, event.start_us) : event.start_us;
        last = any ? std::max(last, end) : end;
        any = true;
    }
    const double span = last - first;

    // tracy-csvexport's header and units (nanoseconds), so that a script
    // written against a Tracy export reads this unchanged. There is no source
    // location here, so the two columns are empty and zero.
    std::string out =
        "name,src_file,src_line,total_ns,total_perc,counts,mean_ns,min_ns,max_ns,std_ns\n";
    for (const ZoneStats& zone : zone_stats()) {
        const double percent = span > 0.0 ? zone.total_us / span * 100.0 : 0.0;
        char row[256];
        std::snprintf(row, sizeof row, ",0,%.0f,%.2f,%llu,%.0f,%.0f,%.0f,%.0f\n",
                      zone.total_us * 1000.0, percent, static_cast<unsigned long long>(zone.count),
                      zone.mean_us * 1000.0, zone.min_us * 1000.0, zone.max_us * 1000.0,
                      zone.stddev_us * 1000.0);
        // name, then an empty src_file, then the numbers above
        out += csv_field(zone.name);
        out += ',';
        out += row;
    }
    return out;
}

std::string Recorder::chrome_trace_json() const {
    std::vector<std::pair<std::uint32_t, std::string>> names;
    std::string app;
    {
        const std::scoped_lock lock(names_mutex_);
        names = thread_names_;
        app = app_info_;
    }
    const std::vector<Event> events = snapshot();

    std::map<std::uint32_t, std::string> thread_label;
    for (const auto& [id, name] : names) {
        thread_label[id] = name;
    }
    for (const Event& event : events) {
        thread_label.try_emplace(event.thread, "thread " + std::to_string(event.thread));
    }

    std::string out = "{\"traceEvents\":[";
    out += R"({"ph":"M","name":"process_name","pid":1,"tid":0,"args":{"name":"Cubeulator"}})";
    for (const auto& [id, label] : thread_label) {
        out += R"(,{"ph":"M","name":"thread_name","pid":1,"tid":)" + std::to_string(id) +
               R"(,"args":{"name":)" + json_string(label) + "}}";
    }
    for (const Event& event : events) {
        const std::string name = json_string(event.name != nullptr ? event.name : "?");
        const std::string common = R"("pid":1,"tid":)" + std::to_string(event.thread) +
                                   R"(,"ts":)" + number(event.start_us);
        switch (event.kind) {
            case Kind::zone:
                out += ",{\"name\":" + name + R"(,"cat":"cube","ph":"X",)" + common + R"(,"dur":)" +
                       number(event.duration_us);
                if (event.has_value) {
                    out += R"(,"args":{"value":)" + number(event.value) + "}";
                }
                out += "}";
                break;
            case Kind::frame:
                out += ",{\"name\":" + name + R"(,"cat":"frame","ph":"i","s":"p",)" + common + "}";
                break;
            case Kind::message:
                out +=
                    ",{\"name\":" + name + R"(,"cat":"message","ph":"i","s":"p",)" + common + "}";
                break;
            case Kind::plot:
                out += ",{\"name\":" + name + R"(,"cat":"plot","ph":"C",)" + common +
                       R"(,"args":{"value":)" + number(event.value) + "}}";
                break;
        }
    }
    out += R"(],"displayTimeUnit":"ms","otherData":{"app":)" + json_string(app) +
           R"(,"recorded":)" + std::to_string(recorded()) + R"(,"dropped":)" +
           std::to_string(dropped()) + "}}";
    return out;
}

std::string Recorder::summary_json() const {
    std::string app;
    {
        const std::scoped_lock lock(names_mutex_);
        app = app_info_;
    }

    std::map<std::string, std::uint64_t> frames;
    std::map<std::string, std::uint64_t> messages;
    for (const Event& event : snapshot()) {
        if (event.name == nullptr) {
            continue;
        }
        if (event.kind == Kind::frame) {
            ++frames[event.name];
        } else if (event.kind == Kind::message) {
            ++messages[event.name];
        }
    }

    std::string out = R"({"app":)" + json_string(app) + R"(,"recorded":)" +
                      std::to_string(recorded()) + R"(,"dropped":)" + std::to_string(dropped()) +
                      R"(,"zones":{)";
    bool first = true;
    for (const ZoneStats& zone : zone_stats()) {
        out += (first ? "" : ",") + json_string(zone.name) + R"(:{"count":)" +
               std::to_string(zone.count) + R"(,"total_ms":)" + number(zone.total_us / 1000.0) +
               R"(,"mean_ms":)" + number(zone.mean_us / 1000.0) + R"(,"min_ms":)" +
               number(zone.min_us / 1000.0) + R"(,"max_ms":)" + number(zone.max_us / 1000.0) +
               R"(,"stddev_ms":)" + number(zone.stddev_us / 1000.0) + "}";
        first = false;
    }
    const auto counts = [&out](const char* key, const std::map<std::string, std::uint64_t>& map) {
        out += std::string(R"(},")") + key + R"(":{)";
        bool first_entry = true;
        for (const auto& [name, count] : map) {
            out += (first_entry ? "" : ",") + json_string(name) + ":" + std::to_string(count);
            first_entry = false;
        }
    };
    counts("frames", frames);
    counts("messages", messages);
    out += "}}";
    return out;
}

}  // namespace cube::profile
