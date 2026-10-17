#pragma once
#include <array>
#include <string_view>

namespace sjp {
// Project-supplied reference distances, shown in Hills only; not personal save data.
struct WorldRecord {
    std::string_view hillId;
    double metres;
};
inline constexpr std::array<WorldRecord, 32> WORLD_RECORDS{{
    {"dsj2-finland", 120.70},
    {"dsj2-switzerland", 212.86},
    {"dsj2-czech-rep", 159.55},
    {"dsj2-belarus", 273.03},
    {"dsj2-austria", 85.41},
    {"dsj2-usa", 171.63},
    {"dsj2-latvia", 199.06},
    {"dsj2-poland", 95.05},
    {"dsj2-japan", 240.07},
    {"dsj2-belgium", 129.04},
    {"dsj2-iceland", 218.01},
    {"dsj2-england", 61.58},
    {"dsj2-germany", 137.86},
    {"dsj2-estonia", 189.99},
    {"dsj2-norway", 105.84},
    {"dsj2-australia", 301.80},
    {"dsj2-ireland", 142.22},
    {"dsj2-ukraine", 75.11},
    {"dsj2-hungary", 231.30},
    {"dsj2-sweden", 168.93},
    {"dsj2-italy", 257.64},
    {"dsj2-denmark", 98.18},
    {"dsj2-slovakia", 133.42},
    {"dsj2-canada", 210.05},
    {"dsj2-lithuania", 171.66},
    {"dsj2-kazakhstan", 108.23},
    {"dsj2-china", 248.53},
    {"dsj2-france", 181.32},
    {"dsj2-holland", 128.83},
    {"dsj2-russia", 232.34},
    {"dsj2-korea", 179.06},
    {"dsj2-slovenia", 290.46},
}};
inline constexpr double worldRecord(std::string_view hillId) {
    for (const auto &record : WORLD_RECORDS)
        if (record.hillId == hillId)
            return record.metres;
    return 0;
}
} // namespace sjp
