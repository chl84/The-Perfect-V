#pragma once
#include "visual_environment.h"
#include "visual_floodlights.h"

namespace sjp::visual {
inline double backdropGround(const Hill &hill, double x, double z) {
    const double original = arenaGround(hill, x, z), bottom = hill.outrunBottom();
    double height = hill.ground(x), surface = original;
    const double tailStart = std::max(bottom + 47, 190 * hill.scale());
    if (x > tailStart) {
        // Beyond the prepared track and all arena fixtures, retain the endpoint
        // and its tangent, then
        // roll over into a descending continuation instead of an infinite wall.
        const double dx = x - tailStart, run = 8 * hill.scale();
        const double extension = hill.ground(tailStart) +
                                 (hill.slope(tailStart) * run * dx - .24 * dx * dx) / (dx + run);
        surface -= height - extension;
        height = extension;
    }
    const double core = std::max(50 * hill.scale(), 24.0);
    if (x <= bottom)
        return surface;
    // Only the decorative flanks beyond the outrun bottom stop following the
    // quadratic's unbounded uphill extrapolation. Keep a wide original strip
    // around the ski surface, stands and masts, and blend continuously across it.
    const double t = clamp((std::abs(z) - core) / (60 * hill.scale()), 0.0, 1.0);
    const double weight = t * t * (3 - 2 * t);
    if (weight == 0)
        return surface;
    const double dx = x - bottom;
    const double drop = .24 * dx * dx / (dx + 30 * hill.scale());
    return surface - weight * (height - hill.ground(bottom) + drop);
}
// Photographic distant scenery follows world directions, never camera position.
inline double mountainU(Vec3 direction) {
    return .5 + std::atan2(direction.z, direction.x) / (2 * PI);
}
inline double mountainV(Vec3 direction) {
    const double altitude = std::asin(clamp(direction.normalized().y, -1.0, 1.0));
    return clamp((.27 - altitude) / .95, 0.0, 1.0);
}
inline Vec3 mountainTint(const EnvironmentLight &light) {
    // The photo's snow white point receives the same ambient, sun/moon and
    // fill colors as the valley's natural snow, rather than a time-only tint.
    // A broad hillside normal avoids relighting details baked into the photo.
    const Vec3 normal = Vec3{.38, .92, .08}.normalized();
    const double direct = std::max(0.0, dot(normal, light.direction));
    const double fill = std::max(0.0, dot(normal, Vec3{90,110,-160}.normalized()));
    const Vec3 illumination = light.ambient + light.direct * direct + light.fill * fill;
    constexpr double whitePoint = .88;
    return {clamp(.83 * illumination.x, 0.0, 1.0) / whitePoint,
            clamp(.89 * illumination.y, 0.0, 1.0) / whitePoint,
            clamp(.94 * illumination.z, 0.0, 1.0) / whitePoint};
}
inline Vec3 mountainHazeColor(const EnvironmentLight &light) {
    // The lower photograph represents snow beyond the model's boundary.
    // Give its soft, detail-free foot the snow's illuminated palette as well
    // as atmospheric haze; plain sky-colored fog formed a gray band here.
    return mix(light.fog, mountainTint(light) * .88, .75);
}
inline constexpr double valleyEdgeStart = 195, valleyEdgeEnd = 200;
inline double valleyBackStart(const Hill &hill) {
    return std::min(-170 * hill.scale(), hill.inrunStart() - 60 * hill.scale());
}
inline double valleyBackOpacity(const Hill &hill, double x) {
    const double t = clamp((x - valleyBackStart(hill)) / (5 * hill.scale()), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
inline double valleyEdgeOpacity(const Hill &hill, double z, double x = 0) {
    // All trees have their bases within 178 scaled metres; retain opaque snow
    // underneath them and throughout the playable corridor. Only the original
    // final five scaled metres dissolve into the photographic background.
    const double t = clamp((std::abs(z) / hill.scale() - valleyEdgeStart) /
                               (valleyEdgeEnd - valleyEdgeStart), 0.0, 1.0);
    return (1 - t * t * (3 - 2 * t)) * valleyBackOpacity(hill, x);
}
inline double mountainHazeDistance(Vec3 direction) {
    const double t = clamp((-.10 - direction.normalized().y) / .35, 0.0, 1.0);
    return 340 + 1100 * t * t * (3 - 2 * t);
}
} // namespace sjp::visual
