#pragma once
#include "visual_environment.h"
#include <cmath>

namespace sjp::visual {
inline double skySmooth(double a, double b, double value) {
    const double t = clamp((value - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
inline double skyNoise(double x, double z, uint32_t seed) {
    const int ix = int(std::floor(x)), iz = int(std::floor(z));
    const double u = skySmooth(0, 1, x - ix), v = skySmooth(0, 1, z - iz);
    const auto node = [&](int a, int b) {
        return environment::hash(uint32_t(a) * 0x9e3779b9U ^ uint32_t(b) * 0x85ebca6bU ^ seed) /
               double(UINT32_MAX);
    };
    const double a = node(ix, iz), b = node(ix + 1, iz), c = node(ix, iz + 1), d = node(ix + 1, iz + 1);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}
// A fixed sky dome, sampled only at the small background mesh's vertices.
// World-space directions keep clouds anchored through camera changes; the
// cosmetic seed makes pause, replay seeking and repeated draws deterministic.
inline double skyCloudCover(environment::Conditions conditions, Vec3 direction) {
    if (conditions.weather == environment::Weather::Sunny)
        return 0; // Clear means a cloudless sky, including moonlit nights.
    const uint32_t seed = environment::hash(conditions.seed ^ 0x3a97c821U);
    const double broad = skyNoise(direction.x * 2.6, direction.z * 2.6, seed);
    const double detail = skyNoise(direction.x * 5.2 + 1.7, direction.z * 5.2 - 2.1, seed ^ 0x57b4d963U);
    const double layers = broad * .84 + detail * .16;
    const double horizon = skySmooth(.015, .23, direction.y);
    using environment::Weather;
    if (conditions.weather == Weather::Fog)
        return (.36 + .18 * layers) * horizon;
    return (.68 + .28 * layers) * horizon;
}
inline Vec3 skyColor(const EnvironmentLight &light, environment::Conditions conditions, Vec3 direction) {
    const double height = skySmooth(-.03, .82, direction.y);
    Vec3 base = mix(light.skyHorizon, light.skyTop, height);
    const bool clear = conditions.weather == environment::Weather::Sunny;
    const bool morning = conditions.time == environment::TimeOfDay::Morning;
    if (clear && (morning || conditions.time == environment::TimeOfDay::Evening)) {
        const Vec3 coolHorizon = morning ? Vec3{.53, .65, .76} : Vec3{.44, .48, .63};
        base = mix(coolHorizon, light.skyTop, skySmooth(-.02, .55, direction.y));
        const Vec3 bearing = Vec3{direction.x, 0, direction.z}.normalized();
        const Vec3 sunBearing = Vec3{light.direction.x, 0, light.direction.z}.normalized();
        const double horizon = 1 - skySmooth(.02, .22, std::abs(direction.y));
        const double towardSun = skySmooth(.15, .95, dot(bearing, sunBearing));
        const double halo = std::pow(std::max(0.0, dot(direction, light.direction)), 24);
        // A warm horizon toward the sun, with a cool upper/opposite sky.
        // Lighting, the sun direction and the scene's fog remain unchanged.
        base = mix(base, light.skyHorizon, horizon * (.10 + .55 * towardSun) + .18 * halo);
    }
    const double cover = skyCloudCover(conditions, direction);
    if (clear)
        return base;
    Vec3 cloud = light.moon ? mix(light.skyHorizon, Vec3{.17, .20, .26}, .35)
                            : mix(light.skyHorizon, clear ? Vec3{.92, .94, .96} : Vec3{.64, .68, .73}, .55);
    if (!clear) {
        const double shading =
            skyNoise(direction.x * 3.1 - 1.2, direction.z * 3.1 + .7, conditions.seed ^ 0x2764ad91U);
        cloud = cloud * (.94 + .10 * shading);
    }
    return mix(base, cloud, cover);
}
} // namespace sjp::visual
