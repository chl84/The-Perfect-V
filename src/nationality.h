#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace sjp {
struct Nationality {
    const char *code, *name, *iso;
    int flagX, flagY, flagWidth, flagHeight;
};
inline constexpr int FLAG_ATLAS_WIDTH = 528, FLAG_ATLAS_HEIGHT = 528;
inline constexpr std::array<Nationality, 33> NATIONALITIES{{
#include "nationalities.inc"
}};
// Original hill labels, Olympic codes and ISO alpha-2/alpha-3 aliases.
const Nationality *nationality(std::string_view country);
struct AthleteIdentity {
    std::string name, country;
};
// Cosmetic randomness is independent of the original skills/wind generators.
uint32_t athleteIdentitySeed();
AthleteIdentity randomAthleteIdentity(uint32_t seed, std::string_view country = {});
} // namespace sjp
