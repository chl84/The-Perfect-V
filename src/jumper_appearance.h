#pragma once
#include <array>

namespace sjp {
// Rendering preferences and replay metadata; never used by simulation or scoring.
enum class JumperAppearance : int { Pro, President, Actor, Cartoon, Hero };
inline constexpr std::array<JumperAppearance, 5> JUMPER_APPEARANCES{
    JumperAppearance::Pro, JumperAppearance::President, JumperAppearance::Actor, JumperAppearance::Cartoon,
    JumperAppearance::Hero};
inline bool validJumperAppearance(JumperAppearance appearance) {
    return appearance == JumperAppearance::Pro || appearance == JumperAppearance::President ||
           appearance == JumperAppearance::Actor || appearance == JumperAppearance::Cartoon ||
           appearance == JumperAppearance::Hero;
}
inline const char *jumperAppearanceName(JumperAppearance appearance) {
    if (appearance == JumperAppearance::Hero)
        return "Hero";
    if (appearance == JumperAppearance::Cartoon)
        return "Cartoon";
    if (appearance == JumperAppearance::Actor)
        return "Actor";
    return appearance == JumperAppearance::President ? "President" : "Pro";
}
} // namespace sjp
