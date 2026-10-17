#pragma once
#include "original_skeleton.h"
#include <array>
#include <cmath>

namespace sjp::visual {
// Frame-derived, bounded presentation motion. No solver, history or random state:
// identical replay frames (including paused/seeking frames) produce identical cloth.
struct HeroBreeze {
    Vec3 flow;
    double stream, gust, time;
};
inline HeroBreeze heroBreeze(const Frame &f) {
    const double wind = double(f.windSpeed);
    Vec3 air{-double(f.vx) + wind * std::cos(double(f.windAngle)), -double(f.vy),
             wind * std::sin(double(f.windAngle)) * .25};
    const double speed = air.length();
    double stream = clamp(speed / 22, 0.0, 1.0);
    stream = stream * stream * (3 - 2 * stream);
    return {speed > 1e-8 ? air * (1 / speed) : Vec3{-1, 0, 0}, stream,
            clamp(std::abs(wind) / 8, 0.0, 1.0), double(f.elapsed)};
}
inline Vec3 heroForward(Vec3 up) {
    return cross(up, std::abs(up.z) < .9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0}).normalized();
}
inline Vec3 heroCapePoint(const OriginalSkeleton &s, const Frame &f, double t, double across) {
    using J = OriginalJoint;
    const Vec3 shoulder = s[J::ShoulderCenter], hip = s[J::HipCenter];
    const Vec3 up = (shoulder - hip).normalized(), front = heroForward(up), side = cross(front, up).normalized();
    const auto air = heroBreeze(f);
    const Vec3 root = shoulder - up * .035 - front * .142;
    const Vec3 draped = up * (-1.03 * t) - front * (.09 * t);
    const Vec3 streamed = air.flow * (1.14 * t) - up * (.10 * t) + Vec3{0, -.10 * t * t, 0};
    const double wave = air.time * (6 + air.gust * 2) - 9 * t + 2.5 * across;
    const double envelope = t * t;
    const double ripple = (.010 + .065 * air.stream + .025 * air.gust) * envelope * std::sin(wave);
    return root + draped * (1 - air.stream) + streamed * air.stream +
           side * (across * (.205 + .20 * t) + .035 * air.stream * envelope * std::sin(wave * .8)) +
           front * (.016 * t * std::cos(across * 5 * PI) + ripple) + Vec3{0, ripple * .7, 0};
}
// One connected surface: closed crown, an opening for the face, covered nape,
// then a tapered moving curtain. Only the lengths below row 6 can move.
inline constexpr size_t heroHairRows = 14, heroHairColumns = 20, heroHairFixedRows = 7;
using HeroHairMesh = std::array<std::array<Vec3, heroHairColumns + 1>, heroHairRows>;
inline HeroHairMesh heroHairMesh(const OriginalSkeleton &s, const Frame &f) {
    using J = OriginalJoint;
    const Vec3 head = s[J::Head], up = (head - s[J::ShoulderCenter]).normalized();
    const Vec3 front = heroForward(up), side = cross(front, up).normalized();
    const auto air = heroBreeze(f);
    constexpr std::array<double, heroHairRows> heights{
        .184, .166, .115, .045, -.065, -.155, -.225, -.275, -.36, -.455, -.54, -.60, -.63, -.65};
    constexpr std::array<double, heroHairRows> depths{
        0, .074, .124, .149, .145, .122, .105, .115, .120, .110, .090, .065, .025, 0};
    constexpr std::array<double, heroHairRows> widths{
        0, .073, .127, .149, .145, .123, .140, .157, .160, .152, .139, .115, .050, 0};
    const Vec3 neck = head - up * .225 - front * .100;
    HeroHairMesh mesh{};
    for (size_t r = 0; r < mesh.size(); ++r) {
        const double t = r < heroHairFixedRows ? 0 : double(r - (heroHairFixedRows - 1)) /
            (heroHairRows - heroHairFixedRows);
        const double opening = r < 3 ? 0 : .90;
        const double blend = air.stream * t * t * (3 - 2 * t);
        // Drape the loose curtain outside the cape, rather than through it.
        const double backDrape = .27 * clamp(t * 7, 0.0, 1.0);
        const double phase = air.time * (6.8 + air.gust) - t * 10;
        const double envelope = t * t;
        const Vec3 flutter = side * ((.012 + .040 * air.stream + .025 * air.gust) * envelope * std::sin(phase)) +
                             front * (.018 * air.stream * envelope * std::cos(phase)) +
                             Vec3{0, .018 * air.stream * envelope * std::sin(phase * .8), 0};
        for (size_t c = 0; c <= heroHairColumns; ++c) {
            const double angle = opening + (2 * PI - 2 * opening) * c / heroHairColumns;
            const double lateral = widths[r] * std::sin(angle);
            const Vec3 resting = head + up * heights[r] +
                                 front * (depths[r] * std::cos(angle) - backDrape) + side * lateral;
            const Vec3 streaming = neck + air.flow * (.48 * t) - up * (.035 * t) +
                                   side * lateral + front * (depths[r] * .6 * std::cos(angle));
            mesh[r][c] = resting * (1 - blend) + streaming * blend + flutter;
        }
    }
    return mesh;
}
} // namespace sjp::visual
