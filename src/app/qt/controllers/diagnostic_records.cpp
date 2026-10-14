#include "controllers/diagnostic_records.hpp"

#include <utility>

namespace cube::app::records {

using cube::diagnostics::Json;
using cube::platform::camera::ControlOutcome;
using cube::platform::camera::PhotometryControl;

std::string_view id_of(PhotometryControl control) noexcept {
    switch (control) {
        case PhotometryControl::auto_white_balance:
            return "auto_white_balance";
        case PhotometryControl::exposure:
            return "exposure";
        case PhotometryControl::brightness:
            return "brightness";
        case PhotometryControl::saturation:
            return "saturation";
    }
    return "unknown";
}

std::string_view id_of(ControlOutcome outcome) noexcept {
    switch (outcome) {
        case ControlOutcome::not_requested:
            return "not_requested";
        case ControlOutcome::unsupported:
            return "unsupported";
        case ControlOutcome::rejected:
            return "rejected";
        case ControlOutcome::applied:
            return "applied";
    }
    return "unknown";
}

Json photometry(const cube::platform::camera::PhotometryLockReport& report) {
    Json out = Json::object();
    out.set("platform_supported", report.platform_supported);
    out.set("exposure_locked", report.exposure_locked());
    out.set("fully_applied", report.fully_applied());
    for (const PhotometryControl control : cube::platform::camera::kPhotometryControls) {
        const auto& result = report.result(control);
        Json entry = Json::object();
        entry.set("outcome", id_of(result.outcome));
        entry.set("requested", result.requested);
        entry.set("readback", result.readback);
        out.set(id_of(control), std::move(entry));
    }
    out.set("white_balance_kelvin",
            report.white_balance_kelvin.has_value() ? Json(*report.white_balance_kelvin) : Json());
    out.set("summary", report.summary());
    return out;
}

}  // namespace cube::app::records
