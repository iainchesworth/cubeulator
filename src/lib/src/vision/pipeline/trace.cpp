#include <cube/vision/pipeline/trace.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <system_error>

namespace cube::vision::pipeline {

namespace {

// JSON string escaping for the small character set that can actually occur
// in stage names, keys, and reason strings. Control characters are emitted
// as \u00XX so a malformed input can never break the line-oriented format.
void append_escaped(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
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
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(ch)));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}

// Deterministic, locale-free number formatting. std::to_chars emits the
// shortest round-trip representation, so identical inputs give identical
// bytes — the property the replay-determinism check rides on. Non-finite
// doubles have no JSON spelling; emit null rather than invalid JSON.
void append_double(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char buf[32];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, value);
    if (ec != std::errc{}) {
        out += "null";
        return;
    }
    out.append(buf, ptr);
}

void append_value(std::string& out, const TraceValue& value) {
    switch (value.index()) {
        case 0:
            out += std::get<bool>(value) ? "true" : "false";
            break;
        case 1: {
            char buf[24];
            const auto [ptr, ec] =
                std::to_chars(buf, buf + sizeof buf, std::get<std::int64_t>(value));
            if (ec == std::errc{}) {
                out.append(buf, ptr);
            } else {
                out += "null";
            }
            break;
        }
        case 2:
            append_double(out, std::get<double>(value));
            break;
        default:
            append_escaped(out, std::get<std::string>(value));
            break;
    }
}

}  // namespace

std::string to_jsonl(const FrameTrace& trace) {
    std::string out;
    out.reserve(256 + trace.stages.size() * 128);
    out += "{\"seq\":";
    {
        char buf[16];
        const auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, trace.seq);
        if (ec == std::errc{}) {
            out.append(buf, ptr);
        }
    }
    out += ",\"t_ms\":";
    {
        char buf[24];
        const auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, trace.t_ms);
        if (ec == std::errc{}) {
            out.append(buf, ptr);
        }
    }
    out += ",\"stages\":[";
    bool first_stage = true;
    for (const auto& stage : trace.stages) {
        if (!first_stage) {
            out.push_back(',');
        }
        first_stage = false;
        out += "{\"stage\":";
        append_escaped(out, stage.stage);
        out += ",\"ran\":";
        out += stage.ran ? "true" : "false";
        out += ",\"ms\":";
        append_double(out, stage.duration_ms);
        if (!stage.fields.empty()) {
            out += ",\"fields\":{";
            bool first_field = true;
            for (const auto& [key, value] : stage.fields) {
                if (!first_field) {
                    out.push_back(',');
                }
                first_field = false;
                append_escaped(out, key);
                out.push_back(':');
                append_value(out, value);
            }
            out.push_back('}');
        }
        out.push_back('}');
    }
    out += "]}";
    return out;
}

std::optional<std::string> validate_trace(const FrameTrace& trace) {
    if (trace.stages.size() != kStageNames.size()) {
        return "frame " + std::to_string(trace.seq) + ": expected " +
               std::to_string(kStageNames.size()) + " stage records, got " +
               std::to_string(trace.stages.size());
    }
    for (std::size_t i = 0; i < kStageNames.size(); ++i) {
        if (trace.stages[i].stage != kStageNames[i]) {
            return "frame " + std::to_string(trace.seq) + ": stage " + std::to_string(i) + " is '" +
                   trace.stages[i].stage + "', expected '" + std::string(kStageNames[i]) + "'";
        }
    }
    return std::nullopt;
}

}  // namespace cube::vision::pipeline
