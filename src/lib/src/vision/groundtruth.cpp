#include <cube/vision/groundtruth.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <variant>

namespace cube::vision::groundtruth {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// A minimal recursive-descent JSON parser. Small on purpose: the sidecar
// files are machine-written, so this favours clear errors over speed and
// supports exactly the JSON the spec uses (no comments, no NaN literals).
// ---------------------------------------------------------------------------

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

struct JsonValue {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value;

    [[nodiscard]] const JsonValue* find(const std::string& key) const {
        if (const auto* object = std::get_if<JsonObject>(&value)) {
            for (const auto& [k, v] : *object) {
                if (k == key) {
                    return &v;
                }
            }
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    Parsed<JsonValue> parse() {
        auto value = parse_value();
        if (!value) {
            return value;
        }
        skip_whitespace();
        if (pos_ != text_.size()) {
            return error("trailing characters after the JSON value");
        }
        return value;
    }

private:
    Parsed<JsonValue> error(const std::string& message) const {
        return std::unexpected("JSON error at offset " + std::to_string(pos_) + ": " + message);
    }

    void skip_whitespace() {
        while (pos_ < text_.size() &&
               (std::isspace(static_cast<unsigned char>(text_[pos_])) != 0)) {
            ++pos_;
        }
    }

    [[nodiscard]] bool consume(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    Parsed<JsonValue> parse_value() {
        skip_whitespace();
        if (pos_ >= text_.size()) {
            return error("unexpected end of input");
        }
        const char ch = text_[pos_];
        switch (ch) {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"': {
                auto text = parse_string();
                if (!text) {
                    return std::unexpected(text.error());
                }
                return JsonValue{std::move(*text)};
            }
            case 't':
                if (text_.compare(pos_, 4, "true") == 0) {
                    pos_ += 4;
                    return JsonValue{true};
                }
                return error("bad literal");
            case 'f':
                if (text_.compare(pos_, 5, "false") == 0) {
                    pos_ += 5;
                    return JsonValue{false};
                }
                return error("bad literal");
            case 'n':
                if (text_.compare(pos_, 4, "null") == 0) {
                    pos_ += 4;
                    return JsonValue{nullptr};
                }
                return error("bad literal");
            default:
                return parse_number();
        }
    }

    Parsed<std::string> parse_string() {
        if (!consume('"')) {
            return std::unexpected("JSON error at offset " + std::to_string(pos_) +
                                   ": expected string");
        }
        std::string out;
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"') {
                return out;
            }
            if (ch == '\\') {
                if (pos_ >= text_.size()) {
                    break;
                }
                const char escaped = text_[pos_++];
                switch (escaped) {
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u':
                        // The sidecars never need non-ASCII escapes; accept
                        // and substitute so a stray one cannot derail a
                        // whole clip's labels.
                        pos_ = std::min(pos_ + 4, text_.size());
                        out.push_back('?');
                        break;
                    default:
                        out.push_back(escaped);
                }
            } else {
                out.push_back(ch);
            }
        }
        return std::unexpected("JSON error: unterminated string");
    }

    Parsed<JsonValue> parse_number() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0 || text_[pos_] == '-' ||
                text_[pos_] == '+' || text_[pos_] == '.' || text_[pos_] == 'e' ||
                text_[pos_] == 'E')) {
            ++pos_;
        }
        double number = 0.0;
        const auto [ptr, ec] = std::from_chars(text_.data() + start, text_.data() + pos_, number);
        if (ec != std::errc{} || ptr != text_.data() + pos_ || start == pos_) {
            pos_ = start;
            return error("bad number");
        }
        return JsonValue{number};
    }

    Parsed<JsonValue> parse_array() {
        (void)consume('[');
        JsonArray array;
        skip_whitespace();
        if (consume(']')) {
            return JsonValue{std::move(array)};
        }
        for (;;) {
            auto element = parse_value();
            if (!element) {
                return element;
            }
            array.push_back(std::move(*element));
            skip_whitespace();
            if (consume(']')) {
                return JsonValue{std::move(array)};
            }
            if (!consume(',')) {
                return error("expected ',' or ']' in array");
            }
        }
    }

    Parsed<JsonValue> parse_object() {
        (void)consume('{');
        JsonObject object;
        skip_whitespace();
        if (consume('}')) {
            return JsonValue{std::move(object)};
        }
        for (;;) {
            skip_whitespace();
            auto key = parse_string();
            if (!key) {
                return std::unexpected(key.error());
            }
            skip_whitespace();
            if (!consume(':')) {
                return error("expected ':' after object key");
            }
            auto value = parse_value();
            if (!value) {
                return value;
            }
            object.emplace_back(std::move(*key), std::move(*value));
            skip_whitespace();
            if (consume('}')) {
                return JsonValue{std::move(object)};
            }
            if (!consume(',')) {
                return error("expected ',' or '}' in object");
            }
        }
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

// --- typed extraction helpers ----------------------------------------------

std::optional<double> as_number(const JsonValue* value) {
    if (value == nullptr) {
        return std::nullopt;
    }
    if (const auto* number = std::get_if<double>(&value->value)) {
        return *number;
    }
    return std::nullopt;
}

std::optional<cv::Point2f> as_point(const JsonValue* value) {
    if (value == nullptr) {
        return std::nullopt;
    }
    const auto x = as_number(value->find("x"));
    const auto y = as_number(value->find("y"));
    if (!x || !y) {
        return std::nullopt;
    }
    return cv::Point2f(static_cast<float>(*x), static_cast<float>(*y));
}

std::optional<Tag> tag_from_name(const std::string& name) {
    if (name == "in_frame") {
        return kInFrame;
    }
    if (name == "absent") {
        return kAbsent;
    }
    if (name == "occluded") {
        return kOccluded;
    }
    if (name == "steady") {
        return kSteady;
    }
    if (name == "turning") {
        return kTurning;
    }
    return std::nullopt;
}

std::optional<cube::model::Face> face_from_letter(const std::string& letter) {
    if (letter.size() != 1) {
        return std::nullopt;
    }
    switch (letter[0]) {
        case 'U':
            return cube::model::Face::U;
        case 'R':
            return cube::model::Face::R;
        case 'F':
            return cube::model::Face::F;
        case 'D':
            return cube::model::Face::D;
        case 'L':
            return cube::model::Face::L;
        case 'B':
            return cube::model::Face::B;
        default:
            return std::nullopt;
    }
}

}  // namespace

TagSet Segments::tags_at(int pass) const {
    TagSet tags = 0;
    for (const auto& segment : segments) {
        if (pass >= segment.start && pass <= segment.end) {
            tags |= segment.tags;
        }
    }
    return tags;
}

Parsed<Segments> parse_segments(const std::string& json_text) {
    auto root = JsonParser(json_text).parse();
    if (!root) {
        return std::unexpected(root.error());
    }
    Segments out;
    out.version = static_cast<int>(as_number(root->find("version")).value_or(0));
    if (const auto* clip = root->find("clip")) {
        if (const auto* text = std::get_if<std::string>(&clip->value)) {
            out.clip = *text;
        }
    }
    const auto* segments = root->find("segments");
    const auto* segment_array =
        segments != nullptr ? std::get_if<JsonArray>(&segments->value) : nullptr;
    if (segment_array == nullptr) {
        return std::unexpected("segments.json: missing 'segments' array");
    }
    for (std::size_t i = 0; i < segment_array->size(); ++i) {
        const auto& entry = (*segment_array)[i];
        Segment segment;
        const auto start = as_number(entry.find("start"));
        const auto end = as_number(entry.find("end"));
        if (!start || !end || *end < *start) {
            return std::unexpected("segments[" + std::to_string(i) + "]: bad start/end");
        }
        segment.start = static_cast<int>(*start);
        segment.end = static_cast<int>(*end);
        const auto* tags = entry.find("tags");
        const auto* tag_array = tags != nullptr ? std::get_if<JsonArray>(&tags->value) : nullptr;
        if (tag_array == nullptr || tag_array->empty()) {
            return std::unexpected("segments[" + std::to_string(i) + "]: missing tags");
        }
        for (const auto& tag_value : *tag_array) {
            const auto* name = std::get_if<std::string>(&tag_value.value);
            const auto tag = name != nullptr ? tag_from_name(*name) : std::nullopt;
            if (!tag) {
                return std::unexpected("segments[" + std::to_string(i) + "]: unknown tag");
            }
            segment.tags |= *tag;
        }
        out.segments.push_back(segment);
    }
    if (const auto* events = root->find("reentry_events")) {
        if (const auto* event_array = std::get_if<JsonArray>(&events->value)) {
            for (const auto& event : *event_array) {
                if (const auto pass = as_number(&event)) {
                    out.reentry_events.push_back(static_cast<int>(*pass));
                }
            }
        }
    }
    // Exclusivity validation on the EFFECTIVE (union) tag set, per frame
    // boundary points only - segment endpoints are where sets change.
    for (const auto& segment : out.segments) {
        for (const int pass : {segment.start, segment.end}) {
            const TagSet tags = out.tags_at(pass);
            if ((tags & kInFrame) != 0 && (tags & kAbsent) != 0) {
                return std::unexpected("pass " + std::to_string(pass) +
                                       " is both in_frame and absent");
            }
            if ((tags & kSteady) != 0 && (tags & kTurning) != 0) {
                return std::unexpected("pass " + std::to_string(pass) +
                                       " is both steady and turning");
            }
            if ((tags & kOccluded) != 0 && (tags & kInFrame) == 0) {
                return std::unexpected("pass " + std::to_string(pass) +
                                       " is occluded but not in_frame");
            }
        }
    }
    return out;
}

Parsed<Keyframe> parse_keyframe(const std::string& json_text) {
    auto root = JsonParser(json_text).parse();
    if (!root) {
        return std::unexpected(root.error());
    }
    Keyframe out;
    out.version = static_cast<int>(as_number(root->find("version")).value_or(0));
    const auto pass = as_number(root->find("pass"));
    if (!pass) {
        return std::unexpected("keyframe: missing pass");
    }
    out.pass = static_cast<int>(*pass);
    if (const auto* interpolated = root->find("interpolated")) {
        if (const auto* flag = std::get_if<bool>(&interpolated->value)) {
            out.interpolated = *flag;
        }
    }

    const auto* corners = root->find("corners");
    const auto* corner_array =
        corners != nullptr ? std::get_if<JsonArray>(&corners->value) : nullptr;
    if (corner_array == nullptr) {
        return std::unexpected("keyframe: missing 'corners' array");
    }
    bool seen_ids[8] = {};
    for (std::size_t i = 0; i < corner_array->size(); ++i) {
        const auto& entry = (*corner_array)[i];
        LabeledCorner corner;
        const auto id = as_number(entry.find("model_id"));
        const auto point = as_point(&entry);
        if (!id || *id < 0 || *id > 7 || !point) {
            return std::unexpected("corners[" + std::to_string(i) +
                                   "]: bad model_id or coordinates");
        }
        corner.model_id = static_cast<int>(*id);
        if (seen_ids[corner.model_id]) {
            return std::unexpected("corners: duplicate model_id " +
                                   std::to_string(corner.model_id));
        }
        seen_ids[corner.model_id] = true;
        corner.px = *point;
        out.corners.push_back(corner);
    }

    const auto* faces = root->find("faces");
    const auto* face_array = faces != nullptr ? std::get_if<JsonArray>(&faces->value) : nullptr;
    if (face_array == nullptr) {
        return std::unexpected("keyframe: missing 'faces' array");
    }
    for (std::size_t i = 0; i < face_array->size(); ++i) {
        const auto& entry = (*face_array)[i];
        LabeledFace face;
        const auto* letter = entry.find("face");
        const auto* letter_text =
            letter != nullptr ? std::get_if<std::string>(&letter->value) : nullptr;
        const auto face_id = letter_text != nullptr ? face_from_letter(*letter_text) : std::nullopt;
        if (!face_id) {
            return std::unexpected("faces[" + std::to_string(i) + "]: bad face letter");
        }
        face.face = *face_id;
        const auto* quad = entry.find("quad");
        static constexpr std::array<const char*, 4> kQuadKeys{"tl", "tr", "br", "bl"};
        for (std::size_t c = 0; c < 4; ++c) {
            const auto point = as_point(quad != nullptr ? quad->find(kQuadKeys[c]) : nullptr);
            if (!point) {
                return std::unexpected("faces[" + std::to_string(i) + "]: quad missing " +
                                       kQuadKeys[c]);
            }
            face.quad[c] = *point;
        }
        const auto* centres = entry.find("centres");
        const auto* centre_array =
            centres != nullptr ? std::get_if<JsonArray>(&centres->value) : nullptr;
        if (centre_array == nullptr || centre_array->size() != 9) {
            return std::unexpected(
                "faces[" + std::to_string(i) + "]: centres must be exactly 9 points, got " +
                std::to_string(centre_array == nullptr ? 0 : centre_array->size()));
        }
        for (std::size_t c = 0; c < 9; ++c) {
            const auto point = as_point(&(*centre_array)[c]);
            if (!point) {
                return std::unexpected("faces[" + std::to_string(i) + "]: centres[" +
                                       std::to_string(c) + "] bad");
            }
            face.centres[c] = *point;
        }
        out.faces.push_back(std::move(face));
    }
    return out;
}

Parsed<ClipGroundTruth> load_clip(const std::string& recording_dir) {
    ClipGroundTruth out;
    const fs::path root = fs::path(recording_dir) / "groundtruth";
    if (!fs::exists(root)) {
        return out;  // unlabelled clip: legal, gates nothing
    }
    const auto read_file = [](const fs::path& path) {
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };
    if (const fs::path segments_path = root / "segments.json"; fs::exists(segments_path)) {
        auto segments = parse_segments(read_file(segments_path));
        if (!segments) {
            return std::unexpected(segments_path.filename().string() + ": " + segments.error());
        }
        out.segments = std::move(*segments);
    }
    if (const fs::path keyframes = root / "keyframes"; fs::exists(keyframes)) {
        for (const auto& entry : fs::directory_iterator(keyframes)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }
            auto keyframe = parse_keyframe(read_file(entry.path()));
            if (!keyframe) {
                return std::unexpected("keyframes/" + entry.path().filename().string() + ": " +
                                       keyframe.error());
            }
            const int name_pass = std::atoi(entry.path().stem().string().c_str());
            if (keyframe->pass != name_pass) {
                return std::unexpected("keyframes/" + entry.path().filename().string() +
                                       ": pass field " + std::to_string(keyframe->pass) +
                                       " does not match filename");
            }
            out.keyframes.emplace(keyframe->pass, std::move(*keyframe));
        }
    }
    return out;
}

}  // namespace cube::vision::groundtruth
