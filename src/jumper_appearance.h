#pragma once
#include <array>

namespace sjp {
// A rendering preference only; never passed to simulation or scoring.
enum class JumperAppearance : int { Pro, President, Actor };
inline constexpr std::array<JumperAppearance, 3> JUMPER_APPEARANCES{
    JumperAppearance::Pro, JumperAppearance::President, JumperAppearance::Actor};
inline bool validJumperAppearance(JumperAppearance appearance) {
    return appearance == JumperAppearance::Pro || appearance == JumperAppearance::President ||
           appearance == JumperAppearance::Actor;
}
inline const char *jumperAppearanceName(JumperAppearance appearance) {
    if (appearance == JumperAppearance::Actor)
        return "Actor";
    return appearance == JumperAppearance::President ? "President" : "Pro";
}
} // namespace sjp
