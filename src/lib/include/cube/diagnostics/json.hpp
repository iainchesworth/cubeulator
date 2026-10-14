#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cube::diagnostics {

// A JSON value, built to be written and never read back: the developer
// diagnostics channel (docs/diagnostics.md) describes what the app is doing as
// JSON Lines, and this is the whole of the JSON it needs. Objects keep the
// order their members were set in, so a record's text is the same on every
// platform and a test can compare it.
//
// No dependency and no allocation until a value is built: the channel is off
// by default, and nothing here runs when it is.
class Json {
public:
    Json() = default;  // null
    explicit Json(std::nullptr_t) {}
    explicit Json(bool value) : kind_(Kind::boolean), integer_(value ? 1 : 0) {}
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    explicit Json(T value) : kind_(Kind::integer), integer_(static_cast<std::int64_t>(value)) {}
    // A float is written with the digits that identify that float, not the
    // seventeen of the double it would widen to: 33.3F is "33.3".
    explicit Json(float value)
        : kind_(Kind::real), real_(static_cast<double>(value)), single_(true) {}
    explicit Json(double value) : kind_(Kind::real), real_(value) {}
    explicit Json(const char* value) : kind_(Kind::string), text_(value != nullptr ? value : "") {}
    explicit Json(std::string value) : kind_(Kind::string), text_(std::move(value)) {}
    explicit Json(std::string_view value) : kind_(Kind::string), text_(value) {}

    [[nodiscard]] static Json array() {
        Json json;
        json.kind_ = Kind::array;
        return json;
    }
    [[nodiscard]] static Json object() {
        Json json;
        json.kind_ = Kind::object;
        return json;
    }

    // Array: appends. Does nothing to any other kind of value. The overload
    // for anything that is not already a Json builds one, so a number or a
    // string goes in as it is while the constructors stay explicit.
    Json& push(Json value);
    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, Json>)
    Json& push(T&& value) {
        return push(Json(std::forward<T>(value)));
    }
    // Object: sets a member, replacing one of the same name. Does nothing to
    // any other kind of value. Takes natives as `push` does.
    Json& set(std::string_view key, Json value);
    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, Json>)
    Json& set(std::string_view key, T&& value) {
        return set(key, Json(std::forward<T>(value)));
    }

    // One line: no newline anywhere in it, whatever strings were put in.
    [[nodiscard]] std::string dump() const;

private:
    enum class Kind : std::uint8_t { null, boolean, integer, real, string, array, object };

    void write(std::string& out) const;

    Kind kind_ = Kind::null;
    // The boolean and the integer share one slot.
    std::int64_t integer_ = 0;
    double real_ = 0.0;
    bool single_ = false;
    std::string text_;
    // Object member names, parallel to items_; empty for an array.
    std::vector<std::string> keys_;
    std::vector<Json> items_;
};

}  // namespace cube::diagnostics
