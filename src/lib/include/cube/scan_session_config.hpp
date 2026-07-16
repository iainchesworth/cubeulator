#pragma once

#include <cstdint>
#include <string>

namespace cube {

// Configuration for a single scan session (camera capture -> cube state).
// Every field is optional at the call site, so this is a deducing-`this`
// fluent builder rather than a constructor with a long parameter list:
//   auto config = ScanSessionConfig{}.with_device("front-camera").with_auto_advance(true);
class ScanSessionConfig {
public:
    template <typename Self>
    [[nodiscard]] auto&& with_device(this Self&& self, std::string device_identifier) {
        self.device_identifier_ = std::move(device_identifier);
        return std::forward<Self>(self);
    }

    template <typename Self>
    [[nodiscard]] auto&& with_auto_advance(this Self&& self, bool enabled) {
        self.auto_advance_ = enabled;
        return std::forward<Self>(self);
    }

    template <typename Self>
    [[nodiscard]] auto&& with_frame_timeout_ms(this Self&& self, std::uint32_t timeout_ms) {
        self.frame_timeout_ms_ = timeout_ms;
        return std::forward<Self>(self);
    }

    [[nodiscard]] const std::string& device_identifier() const noexcept { return device_identifier_; }
    [[nodiscard]] bool auto_advance() const noexcept { return auto_advance_; }
    [[nodiscard]] std::uint32_t frame_timeout_ms() const noexcept { return frame_timeout_ms_; }

private:
    std::string device_identifier_;
    bool auto_advance_ = false;
    std::uint32_t frame_timeout_ms_ = 5000;
};

}  // namespace cube
