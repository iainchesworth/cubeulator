#include <cube/diagnostics/record.hpp>

#include <fmt/format.h>

#include <cstdint>
#include <utility>

namespace cube::diagnostics {

Json make_record(std::uint64_t seq, std::string_view time, std::string_view kind, Json data) {
    Json record = Json::object();
    record.set("seq", seq);
    record.set("time", time);
    record.set("kind", kind);
    record.set("data", std::move(data));
    return record;
}

std::string utc_timestamp(std::chrono::system_clock::time_point when) {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;

    const auto since_epoch = duration_cast<milliseconds>(when.time_since_epoch());
    std::int64_t millis = since_epoch.count();
    std::int64_t days = millis / 86'400'000;
    std::int64_t in_day = millis % 86'400'000;
    if (in_day < 0) {
        in_day += 86'400'000;
        --days;
    }

    // Days since 1970-01-01 to a civil date: Howard Hinnant's days_from_civil
    // inverted, which needs no calendar library.
    days += 719'468;
    const std::int64_t era = (days >= 0 ? days : days - 146'096) / 146'097;
    const std::int64_t day_of_era = days - (era * 146'097);
    const std::int64_t year_of_era =
        (day_of_era - (day_of_era / 1'460) + (day_of_era / 36'524) - (day_of_era / 146'096)) / 365;
    const std::int64_t day_of_year =
        day_of_era - ((365 * year_of_era) + (year_of_era / 4) - (year_of_era / 100));
    const std::int64_t shifted_month = ((5 * day_of_year) + 2) / 153;
    const std::int64_t day = day_of_year - (((153 * shifted_month) + 2) / 5) + 1;
    const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    const std::int64_t year = year_of_era + (era * 400) + (month <= 2 ? 1 : 0);

    return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", year, month, day,
                       in_day / 3'600'000, (in_day / 60'000) % 60, (in_day / 1'000) % 60,
                       in_day % 1'000);
}

}  // namespace cube::diagnostics
