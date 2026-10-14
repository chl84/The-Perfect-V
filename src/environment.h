#pragma once
#include <cstdint>
#include <string>

namespace sjp::environment {
enum class TimeOfDay : int { Morning, Day, Evening, Night, Random };
enum class Weather : int { Sunny, Overcast, Snowfall, Fog, Random };
struct Selection {
    TimeOfDay time = TimeOfDay::Day;
    Weather weather = Weather::Sunny;
};
struct Conditions {
    TimeOfDay time = TimeOfDay::Day;
    Weather weather = Weather::Sunny;
    uint32_t seed = 0;
};
inline bool valid(Selection value) {
    return int(value.time) >= 0 && int(value.time) <= 4 && int(value.weather) >= 0 && int(value.weather) <= 4;
}
inline bool valid(Conditions value) {
    return int(value.time) >= 0 && int(value.time) < 4 && int(value.weather) >= 0 && int(value.weather) < 4;
}
inline bool operator==(Conditions a, Conditions b) {
    return a.time == b.time && a.weather == b.weather && a.seed == b.seed;
}
inline uint32_t hash(uint32_t seed) {
    seed ^= seed >> 16;
    seed *= 0x7feb352dU;
    seed ^= seed >> 15;
    seed *= 0x846ca68bU;
    return seed ^ (seed >> 16);
}
inline Conditions resolve(Selection selection, uint32_t seed) {
    if (!valid(selection))
        selection = {};
    Conditions result{selection.time, selection.weather, seed};
    if (selection.time == TimeOfDay::Random)
        result.time = TimeOfDay(hash(seed ^ 0x71d046a3U) % 4);
    if (selection.weather == Weather::Random) {
        const uint32_t roll = hash(seed ^ 0x53a9d107U) % 10000U;
        result.weather = roll < 3000   ? Weather::Snowfall
                         : roll < 4000 ? Weather::Fog
                         : roll < 5500 ? Weather::Overcast
                                       : Weather::Sunny;
    }
    return result;
}
inline const char *name(TimeOfDay time) {
    constexpr const char *names[]{"Morning", "Day", "Evening", "Night", "Random"};
    return int(time) >= 0 && int(time) <= 4 ? names[int(time)] : names[1];
}
inline const char *name(Weather weather) {
    constexpr const char *names[]{"Sunny / clear", "Overcast", "Snowfall", "Fog", "Random"};
    return int(weather) >= 0 && int(weather) <= 4 ? names[int(weather)] : names[0];
}
inline std::string description(Conditions conditions) {
    const char *weather = conditions.weather == Weather::Sunny
                              ? (conditions.time == TimeOfDay::Night ? "Clear" : "Sunny")
                              : name(conditions.weather);
    return std::string(name(conditions.time)) + " / " + weather;
}
} // namespace sjp::environment
