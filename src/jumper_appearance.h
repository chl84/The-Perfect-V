#pragma once
#include <array>

namespace sjp {
// Rendering preferences and replay metadata; never used by simulation or scoring.
enum class JumperAppearance : int { Pro, President, Actor, Cartoon };
inline constexpr std::array<JumperAppearance, 4> JUMPER_APPEARANCES{
    JumperAppearance::Pro, JumperAppearance::President, JumperAppearance::Actor, JumperAppearance::Cartoon};
inline bool validJumperAppearance(JumperAppearance appearance) {
    return appearance == JumperAppearance::Pro || appearance == JumperAppearance::President ||
           appearance == JumperAppearance::Actor || appearance == JumperAppearance::Cartoon;
}
inline const char *jumperAppearanceName(JumperAppearance appearance) {
    if (appearance == JumperAppearance::Cartoon)
        return "Cartoon";
    if (appearance == JumperAppearance::Actor)
        return "Actor";
    return appearance == JumperAppearance::President ? "President" : "Pro";
}
} // namespace sjp
