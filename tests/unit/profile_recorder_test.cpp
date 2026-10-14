#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cube/profile/recorder.hpp>
#include <cube/profiling.hpp>

#include "must_have.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

// The recorder is plain C++ and is built into cube::lib on every platform, so
// it is tested here on every platform, whichever cube/profiling.hpp variant the
// build selected. The last block checks the macros of the recording variant
// and is skipped, with a reason, when another variant is in use.

namespace {

using cube::profile::Event;
using cube::profile::Kind;
using cube::profile::Recorder;

// --- a JSON reader, just enough to check the recorder's output is JSON --------

struct Json;
using JsonObject = std::map<std::string, Json>;
using JsonArray = std::vector<Json>;

struct Json {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value;

    [[nodiscard]] const JsonObject& object() const { return std::get<JsonObject>(value); }
    [[nodiscard]] const JsonArray& array() const { return std::get<JsonArray>(value); }
    [[nodiscard]] const std::string& string() const { return std::get<std::string>(value); }
    [[nodiscard]] double number() const { return std::get<double>(value); }
    [[nodiscard]] const Json& at(const std::string& key) const { return object().at(key); }
    [[nodiscard]] bool has(const std::string& key) const { return object().contains(key); }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    // nullopt for anything that is not exactly one JSON value.
    std::optional<Json> parse() {
        auto result = value();
        skip();
        if (!result || pos_ != text_.size()) {
            return std::nullopt;
        }
        return result;
    }

private:
    void skip() {
        while (pos_ < text_.size() && (text_.at(pos_) == ' ' || text_.at(pos_) == '\n' ||
                                       text_.at(pos_) == '\r' || text_.at(pos_) == '\t')) {
            ++pos_;
        }
    }
    bool eat(char c) {
        skip();
        if (pos_ < text_.size() && text_.at(pos_) == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    std::optional<Json> value() {
        skip();
        if (pos_ >= text_.size()) {
            return std::nullopt;
        }
        const char c = text_.at(pos_);
        if (c == '{') {
            return object();
        }
        if (c == '[') {
            return array();
        }
        if (c == '"') {
            auto s = string();
            if (!s) {
                return std::nullopt;
            }
            return Json{*s};
        }
        if (literal("true")) {
            return Json{true};
        }
        if (literal("false")) {
            return Json{false};
        }
        if (literal("null")) {
            return Json{nullptr};
        }
        return number();
    }

    std::optional<Json> number() {
        const std::size_t begin = pos_;
        while (pos_ < text_.size() &&
               std::string_view("+-.eE0123456789").contains(text_.at(pos_))) {
            ++pos_;
        }
        if (begin == pos_) {
            return std::nullopt;
        }
        return Json{std::stod(std::string(text_.substr(begin, pos_ - begin)))};
    }

    std::optional<std::string> string() {
        if (!eat('"')) {
            return std::nullopt;
        }
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_.at(pos_++);
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20U) {
                return std::nullopt;  // a raw control character is not JSON
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                return std::nullopt;
            }
            const char escape = text_.at(pos_++);
            switch (escape) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) {
                        return std::nullopt;
                    }
                    out += static_cast<char>(
                        std::stoi(std::string(text_.substr(pos_, 4)), nullptr, 16));
                    pos_ += 4;
                    break;
                }
                default:
                    return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Json> array() {
        eat('[');
        JsonArray items;
        if (eat(']')) {
            return Json{std::move(items)};
        }
        for (bool more = true; more; more = eat(',')) {
            auto item = value();
            if (!item) {
                return std::nullopt;
            }
            items.push_back(std::move(*item));
        }
        if (!eat(']')) {
            return std::nullopt;
        }
        return Json{std::move(items)};
    }

    std::optional<Json> object() {
        eat('{');
        JsonObject members;
        if (eat('}')) {
            return Json{std::move(members)};
        }
        for (bool more = true; more; more = eat(',')) {
            skip();
            auto key = string();
            if (!key || !eat(':')) {
                return std::nullopt;
            }
            auto item = value();
            if (!item) {
                return std::nullopt;
            }
            members.emplace(std::move(*key), std::move(*item));
        }
        if (!eat('}')) {
            return std::nullopt;
        }
        return Json{std::move(members)};
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

Json parse(const std::string& text) {
    auto parsed = JsonReader(text).parse();
    return ::cube::test::must_have(parsed);
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    for (std::string part; std::getline(stream, part, separator);) {
        parts.push_back(part);
    }
    return parts;
}

// The events a recorder holds that are of one kind and name.
std::vector<Event> events_named(const Recorder& recorder, Kind kind, std::string_view name) {
    std::vector<Event> found;
    for (const Event& event : recorder.snapshot()) {
        if (event.kind == kind && event.name != nullptr && name == event.name) {
            found.push_back(event);
        }
    }
    return found;
}

}  // namespace

// --- the reader used above ------------------------------------------------------

TEST_CASE("the test's own JSON reader accepts JSON and rejects what is not", "[profile]") {
    REQUIRE(JsonReader(R"({"a":[1,2.5,true,null,"x\n\"y"],"b":{}})").parse().has_value());
    REQUIRE_FALSE(JsonReader(R"({"a":1,})").parse().has_value());
    REQUIRE_FALSE(JsonReader("{\"a\":\"raw\nnewline\"}").parse().has_value());
    REQUIRE_FALSE(JsonReader(R"([1,2)").parse().has_value());
    REQUIRE_FALSE(JsonReader(R"({"a":1} trailing)").parse().has_value());
}

// --- recording ------------------------------------------------------------------

TEST_CASE("a new recorder is empty", "[profile]") {
    const Recorder recorder(64);
    REQUIRE(recorder.recorded() == 0);
    REQUIRE(recorder.dropped() == 0);
    REQUIRE(recorder.snapshot().empty());
    REQUIRE(recorder.zone_stats().empty());
}

TEST_CASE("a zone is recorded with its name, start and duration", "[profile]") {
    Recorder recorder(64);
    recorder.zone("test.zone", 10.0, 12.5);

    const auto events = recorder.snapshot();
    REQUIRE(events.size() == 1);
    CHECK(events.at(0).kind == Kind::zone);
    CHECK(std::string_view(events.at(0).name) == "test.zone");
    CHECK(events.at(0).start_us == 10.0);
    CHECK(events.at(0).duration_us == 2.5);
    CHECK_FALSE(events.at(0).has_value);
    CHECK(recorder.recorded() == 1);
}

TEST_CASE("a zone's attached number is kept", "[profile]") {
    Recorder recorder(64);
    recorder.zone_with_value("test.work", 0.0, 1.0, 123456789012ULL);

    const auto events = recorder.snapshot();
    REQUIRE(events.size() == 1);
    CHECK(events.at(0).has_value);
    CHECK(events.at(0).value == 123456789012.0);
}

TEST_CASE("frames, messages and plots are recorded as their own kinds", "[profile]") {
    Recorder recorder(64);
    recorder.frame("Capture");
    recorder.message("scan.complete");
    recorder.plot("queue.depth", 3.5);

    const auto events = recorder.snapshot();
    REQUIRE(events.size() == 3);
    CHECK(events.at(0).kind == Kind::frame);
    CHECK(std::string_view(events.at(0).name) == "Capture");
    CHECK(events.at(1).kind == Kind::message);
    CHECK(std::string_view(events.at(1).name) == "scan.complete");
    CHECK(events.at(2).kind == Kind::plot);
    CHECK(events.at(2).has_value);
    CHECK(events.at(2).value == 3.5);
}

TEST_CASE("the snapshot is in time order whatever order events were recorded in", "[profile]") {
    Recorder recorder(64);
    recorder.zone("late", 30.0, 31.0);
    recorder.zone("early", 10.0, 11.0);
    recorder.zone("middle", 20.0, 21.0);

    const auto events = recorder.snapshot();
    REQUIRE(events.size() == 3);
    CHECK(std::string_view(events.at(0).name) == "early");
    CHECK(std::string_view(events.at(1).name) == "middle");
    CHECK(std::string_view(events.at(2).name) == "late");
}

TEST_CASE("the clock is monotonic and microseconds", "[profile]") {
    const double first = Recorder::now_us();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const double second = Recorder::now_us();
    CHECK(second > first);
    // Five milliseconds slept: well over 4000 us, and not seconds' worth.
    CHECK(second - first >= 4000.0);
    CHECK(second - first < 5'000'000.0);
}

TEST_CASE("clear forgets everything and recording carries on", "[profile]") {
    Recorder recorder(64);
    recorder.zone("before", 0.0, 1.0);
    recorder.zone("before", 1.0, 2.0);
    recorder.clear();

    REQUIRE(recorder.recorded() == 0);
    REQUIRE(recorder.snapshot().empty());

    recorder.zone("after", 5.0, 6.0);
    const auto events = recorder.snapshot();
    REQUIRE(events.size() == 1);
    CHECK(std::string_view(events.at(0).name) == "after");
}

TEST_CASE("when the ring is full the oldest events go and are counted", "[profile]") {
    Recorder recorder(8);
    // Static, because the recorder keeps the pointers it is given.
    static constexpr std::array<const char*, 13> kNames = {
        "z0", "z1", "z2", "z3", "z4", "z5", "z6", "z7", "z8", "z9", "za", "zb", "zc",
    };
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        recorder.zone(kNames.at(i), static_cast<double>(i), static_cast<double>(i) + 0.5);
    }

    CHECK(recorder.recorded() == 13);
    CHECK(recorder.dropped() == 5);
    const auto events = recorder.snapshot();
    REQUIRE(events.size() == 8);
    CHECK(std::string_view(events.front().name) == "z5");
    CHECK(std::string_view(events.back().name) == "zc");
}

TEST_CASE("a capacity that is not a power of two is rounded up", "[profile]") {
    Recorder recorder(5);  // 8
    for (int i = 0; i < 8; ++i) {
        recorder.zone("z", static_cast<double>(i), static_cast<double>(i) + 1.0);
    }
    CHECK(recorder.dropped() == 0);
    CHECK(recorder.snapshot().size() == 8);
    recorder.zone("z", 9.0, 10.0);
    CHECK(recorder.dropped() == 1);
}

TEST_CASE("threads record into one timeline without losing events", "[profile]") {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 500;
    Recorder recorder(std::size_t{1} << 14U);

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&recorder] {
            for (int i = 0; i < kPerThread; ++i) {
                const double start = Recorder::now_us();
                recorder.zone("test.worker", start, Recorder::now_us());
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    CHECK(recorder.recorded() == static_cast<std::uint64_t>(kThreads * kPerThread));
    CHECK(recorder.dropped() == 0);
    const auto events = recorder.snapshot();
    REQUIRE(events.size() == static_cast<std::size_t>(kThreads * kPerThread));
    std::set<std::uint32_t> threads;
    for (const Event& event : events) {
        threads.insert(event.thread);
        CHECK(event.duration_us >= 0.0);
    }
    CHECK(threads.size() == static_cast<std::size_t>(kThreads));
}

TEST_CASE("reading while another thread records never returns a torn event", "[profile]") {
    // A small ring, so the writer laps it constantly: every slot is being
    // rewritten while the reader walks it. Each event's duration is its
    // start + 1, which a torn read (one event's start with another's duration)
    // would break.
    Recorder recorder(16);
    std::atomic<bool> stop{false};
    std::thread writer([&recorder, &stop] {
        double t = 0.0;
        while (!stop.load(std::memory_order_relaxed)) {
            recorder.zone("test.tear", t, t + (t + 1.0));  // duration = t + 1
            t += 1.0;
        }
    });

    for (int round = 0; round < 2000; ++round) {
        for (const Event& event : recorder.snapshot()) {
            REQUIRE(event.kind == Kind::zone);
            REQUIRE(event.duration_us == event.start_us + 1.0);
        }
    }
    stop.store(true);
    writer.join();
}

// --- statistics -----------------------------------------------------------------

TEST_CASE("zone statistics are per name", "[profile]") {
    Recorder recorder(64);
    recorder.zone("a", 0.0, 2.0);
    recorder.zone("a", 10.0, 14.0);
    recorder.zone("b", 20.0, 21.0);

    const auto stats = recorder.zone_stats();
    REQUIRE(stats.size() == 2);

    const auto& a = stats.at(0);
    REQUIRE(a.name == "a");
    CHECK(a.count == 2);
    CHECK(a.total_us == 6.0);
    CHECK(a.mean_us == 3.0);
    CHECK(a.min_us == 2.0);
    CHECK(a.max_us == 4.0);
    CHECK(a.stddev_us == 1.0);  // population: durations 2 and 4

    const auto& b = stats.at(1);
    REQUIRE(b.name == "b");
    CHECK(b.count == 1);
    CHECK(b.stddev_us == 0.0);
}

TEST_CASE("only zones count toward zone statistics", "[profile]") {
    Recorder recorder(64);
    recorder.frame("f");
    recorder.message("m");
    recorder.plot("p", 1.0);
    CHECK(recorder.zone_stats().empty());
}

// --- exports --------------------------------------------------------------------

TEST_CASE("the CSV has tracy-csvexport's columns and one row per zone", "[profile]") {
    Recorder recorder(64);
    recorder.zone("solver.solve", 0.0, 10.0);
    recorder.zone("solver.solve", 20.0, 40.0);
    recorder.zone("vision.analyse", 50.0, 51.0);

    const auto lines = split(recorder.zones_csv(), '\n');
    REQUIRE(lines.size() == 3);
    CHECK(lines.at(0) ==
          "name,src_file,src_line,total_ns,total_perc,counts,mean_ns,min_ns,max_ns,std_ns");

    const auto solve = split(lines.at(1), ',');
    REQUIRE(solve.size() == 10);
    CHECK(solve.at(0) == "solver.solve");
    CHECK(solve.at(1).empty());
    CHECK(solve.at(2) == "0");
    CHECK(solve.at(3) == "30000");  // 30 us in nanoseconds
    CHECK(solve.at(5) == "2");      // counts
    CHECK(solve.at(6) == "15000");  // mean
    CHECK(solve.at(7) == "10000");  // min
    CHECK(solve.at(8) == "20000");  // max
    CHECK(solve.at(9) == "5000");   // std

    CHECK(split(lines.at(2), ',').at(0) == "vision.analyse");
}

TEST_CASE("a zone name with a comma or a quote does not break its CSV row", "[profile]") {
    Recorder recorder(64);
    recorder.zone("odd,\"name\"", 0.0, 1.0);

    const auto lines = split(recorder.zones_csv(), '\n');
    REQUIRE(lines.size() == 2);
    CHECK(lines.at(1).starts_with("\"odd,\"\"name\"\"\","));
}

TEST_CASE("the Chrome trace is JSON in the Trace Event Format", "[profile]") {
    Recorder recorder(64);
    recorder.set_thread_name("test-main");
    recorder.set_app_info("Cubeulator 1.2.3 \"quoted\"\nline two");
    recorder.zone("test.zone", 100.0, 150.0);
    recorder.zone_with_value("test.valued", 200.0, 210.0, 42);
    recorder.frame("Capture");
    recorder.message("test.message");
    recorder.plot("test.plot", 7.25);

    const Json trace = parse(recorder.chrome_trace_json());
    const auto& events = trace.at("traceEvents").array();

    std::map<std::string, const Json*> by_name;
    std::string process_name;
    std::string thread_name;
    for (const Json& event : events) {
        const std::string& phase = event.at("ph").string();
        const std::string& name = event.at("name").string();
        if (phase == "M" && name == "process_name") {
            process_name = event.at("args").at("name").string();
        } else if (phase == "M" && name == "thread_name") {
            thread_name = event.at("args").at("name").string();
        } else {
            by_name[name] = &event;
        }
    }
    CHECK(process_name == "Cubeulator");
    CHECK(thread_name == "test-main");

    REQUIRE(by_name.contains("test.zone"));
    const Json& zone = *by_name["test.zone"];
    CHECK(zone.at("ph").string() == "X");
    CHECK(zone.at("ts").number() == 100.0);
    CHECK(zone.at("dur").number() == 50.0);
    CHECK(zone.at("pid").number() == 1.0);
    CHECK_FALSE(zone.has("args"));

    REQUIRE(by_name.contains("test.valued"));
    CHECK(by_name["test.valued"]->at("args").at("value").number() == 42.0);

    REQUIRE(by_name.contains("Capture"));
    CHECK(by_name["Capture"]->at("ph").string() == "i");
    REQUIRE(by_name.contains("test.message"));
    CHECK(by_name["test.message"]->at("ph").string() == "i");

    REQUIRE(by_name.contains("test.plot"));
    CHECK(by_name["test.plot"]->at("ph").string() == "C");
    CHECK(by_name["test.plot"]->at("args").at("value").number() == 7.25);

    const Json& other = trace.at("otherData");
    CHECK(other.at("app").string() == "Cubeulator 1.2.3 \"quoted\"\nline two");
    CHECK(other.at("recorded").number() == 5.0);
    CHECK(other.at("dropped").number() == 0.0);
}

TEST_CASE("the trace names threads that were not named", "[profile]") {
    Recorder recorder(64);
    recorder.zone("test.unnamed", 0.0, 1.0);

    const Json trace = parse(recorder.chrome_trace_json());
    bool found = false;
    for (const Json& event : trace.at("traceEvents").array()) {
        if (event.at("ph").string() == "M" && event.at("name").string() == "thread_name") {
            found = event.at("args").at("name").string().starts_with("thread ");
        }
    }
    CHECK(found);
}

TEST_CASE("an empty recorder exports valid, empty documents", "[profile]") {
    const Recorder recorder(64);
    CHECK(!parse(recorder.chrome_trace_json()).at("traceEvents").array().empty());  // metadata
    CHECK(parse(recorder.summary_json()).at("zones").object().empty());
    CHECK(split(recorder.zones_csv(), '\n').size() == 1);  // the header
}

TEST_CASE("the summary holds zone statistics in milliseconds and the counts of marks",
          "[profile]") {
    Recorder recorder(64);
    recorder.set_app_info("test app");
    recorder.zone("test.zone", 0.0, 2000.0);
    recorder.zone("test.zone", 3000.0, 7000.0);
    recorder.frame("UI");
    recorder.frame("UI");
    recorder.frame("Capture");
    recorder.message("note");

    const Json summary = parse(recorder.summary_json());
    CHECK(summary.at("app").string() == "test app");
    CHECK(summary.at("recorded").number() == 6.0);

    const Json& zone = summary.at("zones").at("test.zone");
    CHECK(zone.at("count").number() == 2.0);
    CHECK(zone.at("total_ms").number() == 6.0);
    CHECK(zone.at("mean_ms").number() == 3.0);
    CHECK(zone.at("min_ms").number() == 2.0);
    CHECK(zone.at("max_ms").number() == 4.0);

    CHECK(summary.at("frames").at("UI").number() == 2.0);
    CHECK(summary.at("frames").at("Capture").number() == 1.0);
    CHECK(summary.at("messages").at("note").number() == 1.0);
}

TEST_CASE("names with control characters are escaped, not emitted raw", "[profile]") {
    Recorder recorder(64);
    recorder.zone("tab\there\x01", 0.0, 1.0);
    const Json trace = parse(recorder.chrome_trace_json());  // a raw control char fails here
    bool found = false;
    for (const Json& event : trace.at("traceEvents").array()) {
        found = found || event.at("name").string() == std::string("tab\there\x01");
    }
    CHECK(found);
}

// --- the macros (profiling-recorder variant) ---------------------------------------

namespace {

int nested_work() {
    CUBE_ZONE_SCOPED_N("macro.inner");
    return 7;
}

int outer_work() {
    CUBE_ZONE_SCOPED_N("macro.outer");
    const int total = nested_work();
    {
        CUBE_ZONE_SCOPED_N("macro.second");
        CUBE_ZONE_SCOPED_VALUE(total * 6);
    }
    return total;
}

}  // namespace

TEST_CASE("the macros of the recorder variant write to the global recorder", "[profile]") {
    if (!cube::profiling::to_recorder) {
        SKIP("this build selected another cube/profiling.hpp variant");
    }
    Recorder& recorder = Recorder::global();
    recorder.clear();

    CHECK(outer_work() == 7);
    CUBE_ZONE_BEGIN(manual, "macro.manual");
    CUBE_ZONE_VALUE(manual, 9);
    CUBE_ZONE_END(manual);
    CUBE_ZONE_END(manual);  // ending twice records once
    CUBE_MESSAGE("macro.message");
    CUBE_FRAME_MARK_NAMED("macro.frames");
    CUBE_FRAME_MARK();
    CUBE_PLOT("macro.plot", 4);
    CUBE_THREAD_NAME("macro-test");
    CUBE_APP_INFO(std::string_view{"macro test"});

    const auto inner = events_named(recorder, Kind::zone, "macro.inner");
    const auto outer = events_named(recorder, Kind::zone, "macro.outer");
    const auto second = events_named(recorder, Kind::zone, "macro.second");
    REQUIRE(inner.size() == 1);
    REQUIRE(outer.size() == 1);
    REQUIRE(second.size() == 1);

    // The inner zone sits inside the outer one on the timeline.
    CHECK(inner.at(0).start_us >= outer.at(0).start_us);
    CHECK(inner.at(0).start_us + inner.at(0).duration_us <=
          outer.at(0).start_us + outer.at(0).duration_us);

    // The value went to the innermost scoped zone open when it was set, which
    // is "macro.second", not the outer one.
    CHECK(second.at(0).has_value);
    CHECK(second.at(0).value == 42.0);
    CHECK_FALSE(outer.at(0).has_value);

    const auto manual_events = events_named(recorder, Kind::zone, "macro.manual");
    REQUIRE(manual_events.size() == 1);
    CHECK(manual_events.at(0).has_value);
    CHECK(manual_events.at(0).value == 9.0);

    CHECK(events_named(recorder, Kind::message, "macro.message").size() == 1);
    CHECK(events_named(recorder, Kind::frame, "macro.frames").size() == 1);
    CHECK(events_named(recorder, Kind::frame, "frame").size() == 1);
    const auto plot = events_named(recorder, Kind::plot, "macro.plot");
    REQUIRE(plot.size() == 1);
    CHECK(plot.at(0).value == 4.0);

    CHECK_THAT(recorder.chrome_trace_json(), Catch::Matchers::ContainsSubstring("\"macro-test\""));
    CHECK_THAT(recorder.chrome_trace_json(), Catch::Matchers::ContainsSubstring("macro test"));
    recorder.clear();
}

TEST_CASE("a scoped zone is closed when an exception unwinds past it", "[profile]") {
    if (!cube::profiling::to_recorder) {
        SKIP("this build selected another cube/profiling.hpp variant");
    }
    Recorder& recorder = Recorder::global();
    recorder.clear();

    const auto throws_in_a_zone = [] {
        CUBE_ZONE_SCOPED_N("macro.throws");
        throw std::runtime_error("unwind");
    };
    CHECK_THROWS_AS(throws_in_a_zone(), std::runtime_error);
    {
        // Were the throwing zone still open, this one would nest beneath it and
        // the value below would land on the wrong zone.
        CUBE_ZONE_SCOPED_N("macro.after");
        CUBE_ZONE_SCOPED_VALUE(5);
    }

    CHECK(events_named(recorder, Kind::zone, "macro.throws").size() == 1);
    const auto after = events_named(recorder, Kind::zone, "macro.after");
    REQUIRE(after.size() == 1);
    CHECK(after.at(0).value == 5.0);
    recorder.clear();
}

TEST_CASE("a value with no zone open is dropped, not misplaced", "[profile]") {
    if (!cube::profiling::to_recorder) {
        SKIP("this build selected another cube/profiling.hpp variant");
    }
    Recorder& recorder = Recorder::global();
    recorder.clear();

    CUBE_ZONE_SCOPED_VALUE(99);
    CHECK(recorder.snapshot().empty());
}
