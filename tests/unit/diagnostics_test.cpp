// The developer diagnostics channel's serialisation (docs/diagnostics.md): the
// JSON value, the record envelope, the JSON Lines file, and the descriptions
// of the library's result structs. Nothing here needs a camera, a window or
// the app: it is the part of the channel that is plain text.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cube/diagnostics/describe.hpp>
#include <cube/diagnostics/json.hpp>
#include <cube/diagnostics/jsonl_file.hpp>
#include <cube/diagnostics/record.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/recording_v2.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using cube::diagnostics::Json;

namespace fs = std::filesystem;

namespace {

// A recursive-descent check that `text` is exactly one JSON value (RFC 8259,
// no extensions): the diagnostics file is read by programs this repository
// does not control, so "it looks like JSON" is not enough.
class JsonChecker {
public:
    explicit JsonChecker(std::string_view text) : text_(text) {}

    [[nodiscard]] bool valid() {
        skip_space();
        if (!value()) {
            return false;
        }
        skip_space();
        return pos_ == text_.size();
    }

private:
    [[nodiscard]] bool at_end() const { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const { return at_end() ? '\0' : text_[pos_]; }
    void skip_space() {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) {
            ++pos_;
        }
    }
    [[nodiscard]] bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            return false;
        }
        pos_ += word.size();
        return true;
    }
    [[nodiscard]] bool string() {
        if (peek() != '"') {
            return false;
        }
        ++pos_;
        while (!at_end()) {
            const auto c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) {
                return false;  // control characters must be escaped
            }
            if (c == '\\') {
                ++pos_;
                const char escape = peek();
                if (escape == 'u') {
                    ++pos_;
                    for (int i = 0; i < 4; ++i) {
                        const char hex = peek();
                        const bool digit = (hex >= '0' && hex <= '9') ||
                                           (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F');
                        if (!digit) {
                            return false;
                        }
                        ++pos_;
                    }
                    continue;
                }
                if (std::string_view("\"\\/bfnrt").find(escape) == std::string_view::npos) {
                    return false;
                }
            }
            ++pos_;
        }
        return false;
    }
    [[nodiscard]] bool number() {
        const std::size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        } else {
            return false;
        }
        if (peek() == '.') {
            ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) {
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (!(peek() >= '0' && peek() <= '9')) {
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        return pos_ > start;
    }
    [[nodiscard]] bool array() {
        ++pos_;  // [
        skip_space();
        if (peek() == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip_space();
            if (!value()) {
                return false;
            }
            skip_space();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
    }
    [[nodiscard]] bool object() {
        ++pos_;  // {
        skip_space();
        if (peek() == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip_space();
            if (!string()) {
                return false;
            }
            skip_space();
            if (peek() != ':') {
                return false;
            }
            ++pos_;
            skip_space();
            if (!value()) {
                return false;
            }
            skip_space();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
    }
    [[nodiscard]] bool value() {
        switch (peek()) {
            case '{':
                return object();
            case '[':
                return array();
            case '"':
                return string();
            case 't':
                return literal("true");
            case 'f':
                return literal("false");
            case 'n':
                return literal("null");
            default:
                return number();
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

[[nodiscard]] bool is_json(std::string_view text) {
    return JsonChecker(text).valid();
}

// A scratch directory under the system's, removed when the test is done.
class ScratchDir {
public:
    explicit ScratchDir(std::string_view name)
        : path_(fs::temp_directory_path() / ("cubeulator-diagnostics-test-" + std::string(name))) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

[[nodiscard]] std::vector<std::string> lines_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

// ---------------------------------------------------------------------------
// Json
// ---------------------------------------------------------------------------

TEST_CASE("a Json value writes objects in the order members were set", "[diagnostics]") {
    Json object = Json::object();
    object.set("zeta", 1);
    object.set("alpha", true);
    object.set("list", Json::array().push(1).push("two").push(Json()));
    object.set("nested", Json::object().set("x", 2.5));

    CHECK(object.dump() == R"({"zeta":1,"alpha":true,"list":[1,"two",null],"nested":{"x":2.5}})");
}

TEST_CASE("setting a member again replaces it where it stood", "[diagnostics]") {
    Json object = Json::object();
    object.set("a", 1).set("b", 2).set("a", 3);
    CHECK(object.dump() == R"({"a":3,"b":2})");
}

TEST_CASE("push and set do nothing to the wrong kind of value", "[diagnostics]") {
    Json number(7);
    number.push(1).set("k", 2);
    CHECK(number.dump() == "7");

    Json array = Json::array();
    array.set("k", 1);
    CHECK(array.dump() == "[]");
}

TEST_CASE("numbers are written as numbers, and a float as its own digits", "[diagnostics]") {
    CHECK(Json(0).dump() == "0");
    CHECK(Json(-12).dump() == "-12");
    CHECK(Json(33.3F).dump() == "33.3");
    CHECK(Json(0.1).dump() == "0.1");
    CHECK(Json(std::size_t{54}).dump() == "54");
}

TEST_CASE("a value that is not a number is null, so the line stays JSON", "[diagnostics]") {
    CHECK(Json(std::numeric_limits<float>::quiet_NaN()).dump() == "null");
    CHECK(Json(std::numeric_limits<double>::infinity()).dump() == "null");
    CHECK(Json(-std::numeric_limits<double>::infinity()).dump() == "null");
}

TEST_CASE("strings are escaped, and never carry a newline", "[diagnostics]") {
    const Json text(std::string("quote \" backslash \\ newline \n tab \t bell \x07 end"));
    const std::string line = text.dump();
    CHECK(line.find('\n') == std::string::npos);
    CHECK_THAT(line, ContainsSubstring(R"(\")"));
    CHECK_THAT(line, ContainsSubstring(R"(\\)"));
    CHECK_THAT(line, ContainsSubstring(R"(\n)"));
    CHECK_THAT(line, ContainsSubstring(R"(\t)"));
    CHECK_THAT(line, ContainsSubstring(R"(\u0007)"));
    CHECK(is_json(line));
}

TEST_CASE("UTF-8 passes through, and bytes that are not UTF-8 become U+FFFD", "[diagnostics]") {
    // "café", written as UTF-8 bytes.
    CHECK(Json(std::string("caf\xC3\xA9")).dump() == "\"caf\xC3\xA9\"");

    // A lone continuation byte, a truncated sequence, an overlong NUL and a
    // surrogate: none is text.
    for (const std::string& bad : {std::string("a\x80z"), std::string("a\xE2\x82"),
                                   std::string("a\xC0\x80z"), std::string("a\xED\xA0\x80z")}) {
        const std::string line = Json(bad).dump();
        CHECK_THAT(line, ContainsSubstring("\xEF\xBF\xBD"));
        CHECK(is_json(line));
    }
}

TEST_CASE("the checker the tests rely on rejects what is not JSON", "[diagnostics]") {
    CHECK(is_json(R"({"a":[1,2,{"b":null}],"c":"d"})"));
    CHECK_FALSE(is_json(R"({"a":1,})"));
    CHECK_FALSE(is_json(R"({a:1})"));
    CHECK_FALSE(is_json("{\"a\":\"line\nbreak\"}"));
    CHECK_FALSE(is_json("NaN"));
    CHECK_FALSE(is_json(R"([1,2)"));
}

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

TEST_CASE("a record is one line with a sequence number, a time, a kind and data", "[diagnostics]") {
    const Json record = cube::diagnostics::make_record(12, "2026-10-14T20:11:03.412Z", "scan",
                                                       Json::object().set("sides", 3));
    CHECK(record.dump() ==
          R"({"seq":12,"time":"2026-10-14T20:11:03.412Z","kind":"scan","data":{"sides":3}})");
    CHECK(is_json(record.dump()));
}

TEST_CASE("times are written as UTC ISO 8601 with milliseconds", "[diagnostics]") {
    using std::chrono::milliseconds;
    using std::chrono::system_clock;
    const auto at = [](std::int64_t ms) { return system_clock::time_point{milliseconds{ms}}; };

    CHECK(cube::diagnostics::utc_timestamp(at(0)) == "1970-01-01T00:00:00.000Z");
    CHECK(cube::diagnostics::utc_timestamp(at(-1)) == "1969-12-31T23:59:59.999Z");
    CHECK(cube::diagnostics::utc_timestamp(at(1'709'208'000'000)) == "2024-02-29T12:00:00.000Z");
    CHECK(cube::diagnostics::utc_timestamp(at(1'792'008'663'412)) == "2026-10-14T20:11:03.412Z");
    CHECK(cube::diagnostics::utc_timestamp(at(951'782'400'000)) == "2000-02-29T00:00:00.000Z");
}

// ---------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------

TEST_CASE("lines are on disk as soon as they are written, for another process to read",
          "[diagnostics]") {
    const ScratchDir dir("flush");
    const fs::path file = dir.path() / "nested" / "diagnostics.jsonl";

    cube::diagnostics::JsonlFile writer;
    REQUIRE_FALSE(writer.open(file).has_value());
    REQUIRE(writer.write_line(R"({"n":1})"));
    REQUIRE(writer.write_line(R"({"n":2})"));

    // Read while the writer still has the file open, which is what a person
    // with `tail -f` or another program does.
    const std::vector<std::string> lines = lines_of(file);
    REQUIRE(lines.size() == 2);
    CHECK(lines.at(0) == R"({"n":1})");
    CHECK(lines.at(1) == R"({"n":2})");
}

TEST_CASE("a file that is not open takes no lines", "[diagnostics]") {
    cube::diagnostics::JsonlFile writer;
    CHECK_FALSE(writer.is_open());
    CHECK_FALSE(writer.write_line("{}"));
}

TEST_CASE("a new run moves the last run's file aside and keeps only that one", "[diagnostics]") {
    const ScratchDir dir("previous");
    const fs::path file = dir.path() / "diagnostics.jsonl";
    const fs::path previous = dir.path() / "diagnostics.previous.jsonl";
    CHECK(cube::diagnostics::JsonlFile::previous_path(file) == previous);

    {
        cube::diagnostics::JsonlFile first;
        REQUIRE_FALSE(first.open(file).has_value());
        REQUIRE(first.write_line(R"({"run":1})"));
    }
    {
        cube::diagnostics::JsonlFile second;
        REQUIRE_FALSE(second.open(file).has_value());
        REQUIRE(second.write_line(R"({"run":2})"));
    }
    {
        cube::diagnostics::JsonlFile third;
        REQUIRE_FALSE(third.open(file).has_value());
        REQUIRE(third.write_line(R"({"run":3})"));
    }

    CHECK(lines_of(file) == std::vector<std::string>{R"({"run":3})"});
    CHECK(lines_of(previous) == std::vector<std::string>{R"({"run":2})"});
}

TEST_CASE("opening again in the same run appends instead of moving the file aside",
          "[diagnostics]") {
    const ScratchDir dir("append");
    const fs::path file = dir.path() / "diagnostics.jsonl";

    cube::diagnostics::JsonlFile writer;
    REQUIRE_FALSE(writer.open(file).has_value());
    REQUIRE(writer.write_line(R"({"n":1})"));
    writer.close();
    REQUIRE_FALSE(
        writer.open(file, cube::diagnostics::JsonlFile::kDefaultMaxBytes, false).has_value());
    REQUIRE(writer.write_line(R"({"n":2})"));

    CHECK(lines_of(file) == std::vector<std::string>{R"({"n":1})", R"({"n":2})"});
    CHECK_FALSE(fs::exists(dir.path() / "diagnostics.previous.jsonl"));
}

TEST_CASE("a file that grows past its limit is rotated, so a channel left on is bounded",
          "[diagnostics]") {
    const ScratchDir dir("rotate");
    const fs::path file = dir.path() / "diagnostics.jsonl";
    const std::string line(40, 'x');

    cube::diagnostics::JsonlFile writer;
    // Room for two 41-byte lines.
    REQUIRE_FALSE(writer.open(file, 100).has_value());
    for (int i = 0; i < 5; ++i) {
        REQUIRE(writer.write_line(line));
    }

    CHECK(lines_of(file).size() == 1);
    CHECK(lines_of(dir.path() / "diagnostics.previous.jsonl").size() == 2);
    CHECK(fs::file_size(file) <= 100);
}

TEST_CASE("a file that cannot be created says why", "[diagnostics]") {
    const ScratchDir dir("blocked");
    // A regular file where the directory has to be.
    const fs::path blocker = dir.path() / "blocker";
    std::ofstream(blocker) << "x";

    cube::diagnostics::JsonlFile writer;
    const auto problem = writer.open(blocker / "diagnostics.jsonl");
    REQUIRE(problem.has_value());
    CHECK_FALSE(problem->empty());
    CHECK_FALSE(writer.is_open());
}

// ---------------------------------------------------------------------------
// What the library's result structs become
// ---------------------------------------------------------------------------

TEST_CASE("names are fixed words, never text meant for a person", "[diagnostics]") {
    using cube::model::FacetColor;
    CHECK(cube::diagnostics::name_of(FacetColor::orange) == "orange");
    CHECK(cube::diagnostics::name_of(FacetColor::unknown) == "unknown");
    CHECK(cube::diagnostics::name_of(cube::solver::Color::Red) == "red");
    CHECK(cube::diagnostics::letter_of(cube::solver::Face::D) == "D");
    CHECK(cube::diagnostics::code_of(cube::CubeError::ambiguous_scan) == "ambiguous_scan");
    CHECK(cube::diagnostics::code_of(cube::CubeError::face_capture_incomplete) ==
          "face_capture_incomplete");
}

TEST_CASE("a cube's state is the manifest's 54-character string", "[diagnostics]") {
    const cube::solver::Cube solved = cube::solver::Cube::solved();
    const std::string state = cube::vision::recording::state_string(solved);
    REQUIRE(state.size() == 54);
    // Each face solid, in U R F D L B order.
    CHECK(state == "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB");
    // What the recorder will accept back.
    CHECK_FALSE(cube::vision::recording::state_string_problem(state).has_value());
    CHECK(cube::diagnostics::cube_json(solved).dump() == "\"" + state + "\"");

    const cube::solver::Cube turned = solved.apply(cube::solver::Move{cube::solver::Face::R, 1});
    const std::string turned_state = cube::vision::recording::state_string(turned);
    CHECK(turned_state != state);
    CHECK_FALSE(cube::vision::recording::state_string_problem(turned_state).has_value());
}

TEST_CASE("moves are written in standard notation", "[diagnostics]") {
    using cube::solver::Face;
    using cube::solver::Move;
    CHECK(cube::diagnostics::move_json(Move{Face::R, 1}).dump() == "\"R\"");
    CHECK(cube::diagnostics::move_json(Move{Face::U, 3}).dump() == "\"U'\"");
    CHECK(cube::diagnostics::move_json(std::nullopt).dump() == "null");
    CHECK(cube::diagnostics::moves_json({Move{Face::F, 2}, Move{Face::L, 3}}).dump() ==
          "\"F2 L'\"");
}

TEST_CASE("a classification is written with its hue, chroma and rule", "[diagnostics]") {
    const auto explained = cube::vision::classify_lab_explained(cv::Vec3f(60.0F, 40.0F, 40.0F));
    const std::string line = cube::diagnostics::to_json(explained).dump();
    CHECK(is_json(line));
    CHECK_THAT(line, ContainsSubstring(R"("rule":"hue_band")"));
    CHECK_THAT(line, ContainsSubstring(R"("name":"orange")"));
    CHECK_THAT(line, ContainsSubstring(R"("hue_deg":)"));
    CHECK(std::abs(explained.hue_degrees - 45.0F) < 0.01F);
    CHECK(std::abs(explained.chroma - (40.0F * std::sqrt(2.0F))) < 0.01F);
}

TEST_CASE("an assembly trace says which path was taken and why it failed", "[diagnostics]") {
    cube::vision::AssemblyTrace trace;
    trace.path = cube::vision::AssemblyPath::names_exchanged;
    trace.corner_reads = 4;
    trace.direct_error = cube::CubeError::invalid_cube_state;
    trace.exchange = cube::vision::NameExchange{
        .first_side = 2,
        .second_side = 5,
        .first_name = cube::model::FacetColor::red,
        .second_name = cube::model::FacetColor::orange,
        .centre_distance = 9.5F,
    };

    const std::string line = cube::diagnostics::to_json(trace).dump();
    CHECK(is_json(line));
    CHECK(line ==
          R"({"path":"names_exchanged","candidates":0,"corner_reads":4,"corner_fits":[],)"
          R"("chosen":null,"direct_error":"invalid_cube_state","exchange":{"first_side":2,)"
          R"("second_side":5,"first_name":"red","second_name":"orange","centre_distance":9.5},)"
          R"("error":null})");

    cube::vision::AssemblyTrace ambiguous;
    ambiguous.path = cube::vision::AssemblyPath::ambiguous;
    ambiguous.candidates = 2;
    ambiguous.corner_fits = {3, 3};
    ambiguous.error = cube::CubeError::ambiguous_scan;
    const std::string ambiguous_line = cube::diagnostics::to_json(ambiguous).dump();
    CHECK(is_json(ambiguous_line));
    CHECK_THAT(ambiguous_line, ContainsSubstring(R"("corner_fits":[3,3])"));
    CHECK_THAT(ambiguous_line, ContainsSubstring(R"("error":"ambiguous_scan")"));
}

TEST_CASE("a scan result is the cube's state and colours, or the reason there is none",
          "[diagnostics]") {
    cube::vision::ScannedCube scanned;
    scanned.cube = cube::solver::Cube::solved();
    scanned.colours.at(static_cast<std::size_t>(cube::solver::Color::Red)) =
        cv::Vec3f(41.0F, 66.0F, 49.0F);
    const std::string ok =
        cube::diagnostics::to_json(cube::Result<cube::vision::ScannedCube>(scanned)).dump();
    CHECK(is_json(ok));
    CHECK_THAT(ok, ContainsSubstring(R"("ok":true)"));
    CHECK_THAT(ok, ContainsSubstring(R"("red":[41,66,49])"));

    const std::string failed =
        cube::diagnostics::to_json(cube::Result<cube::vision::ScannedCube>(
                                       std::unexpected(cube::CubeError::no_valid_cube_detected)))
            .dump();
    CHECK(failed == R"({"ok":false,"error":"no_valid_cube_detected"})");
}
