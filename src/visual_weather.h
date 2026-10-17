#pragma once
#include "physics.h"
#include <algorithm>
#include <cstring>

namespace sjp::visual {
// Cosmetic hash only: never advances OriginalRandom or a simulation.
inline uint32_t weatherHash(uint32_t seed) {
    return environment::hash(seed);
}
inline bool snowForJump(uint32_t seed) {
    return environment::resolve({environment::TimeOfDay::Day, environment::Weather::Random}, seed).weather ==
           environment::Weather::Snowfall;
}
inline uint32_t replayWeatherSeed(const Replay &replay) {
    uint32_t seed = uint32_t(replay.hill) ^ 0x16ba3f29U;
    for (const Frame &frame : replay.frames)
        for (float value : {frame.x, frame.y, frame.elapsed, frame.windSpeed, frame.windAngle}) {
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            seed = weatherHash(seed ^ bits);
        }
    return seed;
}
inline Vec3 windDirection(const Frame &frame) {
    // The source/HUD angle is in the x/y plane, not the x/z terrain plane.
    return {std::cos(frame.windAngle), std::sin(frame.windAngle), 0};
}
inline Vec3 snowDrift(Vec3 wind, double age) {
    // Retain a mostly downward fall: only a small fraction of the air velocity
    // carries flakes horizontally or lifts/slows their descent.
    return {wind.x * age * .08, wind.y * age * .04, 0};
}
inline Vec3 snowfallCenter(Vec3 eye, Vec3 forward, double aspect) {
    // Snow is foreground weather, not a cloud around the camera's distant
    // subject. Keep the visible frustum inside the fixed world-cell volume;
    // ultrawide views bring it closer instead of increasing particle count.
    const double halfFov = std::tan(48 * PI / 360);
    const double distance = std::min(10.0, 10.0 / (halfFov * std::max(1.0, aspect)));
    return eye + forward * distance;
}
inline Vec3 windsockPoint(const Frame &frame, double time, double fraction) {
    const double speed = clamp(double(frame.windSpeed), 0.0, 18.0);
    const double extension = speed / (speed + .35);
    const Vec3 direction = windDirection(frame);
    const double droop = .08 + .65 * (1 - extension);
    const double flutter = .055 * extension * std::sin(time * 4.3 - fraction * 5);
    return direction * (2.2 * extension * fraction) +
           Vec3{0, -fraction * fraction * droop, flutter * fraction};
}
} // namespace sjp::visual
