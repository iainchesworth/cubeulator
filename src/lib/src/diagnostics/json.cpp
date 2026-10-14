#include <cube/diagnostics/json.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cube::diagnostics {

namespace {

// Appends one Unicode scalar value as a JSON string character.
void append_code_point(std::string& out, char32_t code_point) {
    switch (code_point) {
        case U'"':
            out += "\\\"";
            return;
        case U'\\':
            out += "\\\\";
            return;
        case U'\n':
            out += "\\n";
            return;
        case U'\r':
            out += "\\r";
            return;
        case U'\t':
            out += "\\t";
            return;
        default:
            break;
    }
    if (code_point < 0x20 || code_point == 0x7F) {
        out += fmt::format("\\u{:04x}", static_cast<unsigned>(code_point));
        return;
    }
    // UTF-8, so the line is valid text wherever it is read.
    if (code_point < 0x80) {
        out += static_cast<char>(code_point);
    } else if (code_point < 0x800) {
        out += static_cast<char>(0xC0 | (code_point >> 6));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x10000) {
        out += static_cast<char>(0xE0 | (code_point >> 12));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code_point >> 18));
        out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
}

// Appends `text` as a JSON string. Bytes that are not well-formed UTF-8 (a
// device name in a legacy code page, say) come out as U+FFFD: the file must
// stay readable by anything that reads JSON, and a diagnostic is no place to
// refuse a string.
void append_string(std::string& out, std::string_view text) {
    constexpr char32_t kReplacement = 0xFFFD;
    out += '"';
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        char32_t code_point = lead;
        char32_t smallest = 0;
        if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            code_point = lead & 0x07U;
            smallest = 0x10000;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            code_point = lead & 0x0FU;
            smallest = 0x800;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            code_point = lead & 0x1FU;
            smallest = 0x80;
        } else if (lead >= 0x80) {
            append_code_point(out, kReplacement);
            ++i;
            continue;
        }
        bool well_formed = i + length <= text.size();
        for (std::size_t k = 1; well_formed && k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            well_formed = (next & 0xC0U) == 0x80U;
            code_point = (code_point << 6) | (next & 0x3FU);
        }
        const bool surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
        if (!well_formed || code_point < smallest || code_point > 0x10FFFF || surrogate) {
            append_code_point(out, kReplacement);
            ++i;
            continue;
        }
        append_code_point(out, code_point);
        i += length;
    }
    out += '"';
}

}  // namespace

Json& Json::push(Json value) {
    if (kind_ == Kind::array) {
        items_.push_back(std::move(value));
    }
    return *this;
}

Json& Json::set(std::string_view key, Json value) {
    if (kind_ != Kind::object) {
        return *this;
    }
    const auto existing =
        std::ranges::find_if(keys_, [key](const std::string& member) { return member == key; });
    if (existing != keys_.end()) {
        items_.at(static_cast<std::size_t>(existing - keys_.begin())) = std::move(value);
        return *this;
    }
    keys_.emplace_back(key);
    items_.push_back(std::move(value));
    return *this;
}

std::string Json::dump() const {
    std::string out;
    write(out);
    return out;
}

void Json::write(std::string& out) const {
    switch (kind_) {
        case Kind::null:
            out += "null";
            return;
        case Kind::boolean:
            out += integer_ != 0 ? "true" : "false";
            return;
        case Kind::integer:
            out += fmt::format("{}", integer_);
            return;
        case Kind::real:
            // JSON has no NaN or infinity; a reading that is not a number is
            // null, which says so and keeps the line parseable.
            if (!std::isfinite(real_)) {
                out += "null";
            } else if (single_) {
                out += fmt::format("{}", static_cast<float>(real_));
            } else {
                out += fmt::format("{}", real_);
            }
            return;
        case Kind::string:
            append_string(out, text_);
            return;
        case Kind::array: {
            out += '[';
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i != 0) {
                    out += ',';
                }
                items_.at(i).write(out);
            }
            out += ']';
            return;
        }
        case Kind::object: {
            out += '{';
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i != 0) {
                    out += ',';
                }
                append_string(out, keys_.at(i));
                out += ':';
                items_.at(i).write(out);
            }
            out += '}';
            return;
        }
    }
}

}  // namespace cube::diagnostics
