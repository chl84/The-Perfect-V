#pragma once

namespace sjp {
// A rendering preference only; never passed to simulation or scoring.
enum class JumperAppearance : int { Pro, President };
inline bool validJumperAppearance(JumperAppearance appearance) {
    return appearance == JumperAppearance::Pro || appearance == JumperAppearance::President;
}
inline const char *jumperAppearanceName(JumperAppearance appearance) {
    return appearance == JumperAppearance::President ? "President" : "Pro";
}
} // namespace sjp
