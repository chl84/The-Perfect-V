#pragma once
#include "draw_commands.h"
#include "visual_environment.h"
#include <array>
#include <cmath>

namespace sjp::visual {
inline double arenaGround(const Hill &h, double x, double z) {
    const double edge = std::max(0.0, std::abs(z) - 19 * h.scale());
    return h.ground(x) - edge * .08 + std::sin(x * .017 + z * .013) * edge * .018;
}
struct FloodlightMast {
    Vec3 base, lamp;
    double size;
};
inline int floodlightColumns(const Hill &h) {
    return h.k <= 60 ? 3 : h.k < 100 ? 4 : gfx::ARENA_LIGHT_COLUMNS;
}
inline double floodlightSpacing(const Hill &h) {
    // Keep the first/last fixtures and illuminated stretch, spreading fewer
    // columns over that same stretch on short hills.
    return (29.0 * (gfx::ARENA_LIGHT_COLUMNS - 1) / (floodlightColumns(h) - 1)) * h.scale();
}
inline FloodlightMast floodlightMast(const Hill &h, int column, int side) {
    const double s = h.scale(), size = std::sqrt(s);
    const double x = (60 + column * 116.0 / (floodlightColumns(h) - 1)) * s;
    const double setback = std::max(25 * s, 8.0) + 10.5;
    // Camera 1 follows at z=26. Keep the whole camera-side fixture outside
    // that corridor, including its foundation/head, without moving the camera.
    const double z = side > 0 ? std::max(setback, 30.0) : -setback;
    const double y = arenaGround(h, x, z);
    return {{x, y, z}, {x, y + 18 * size + 2, z + (z > 0 ? -.345 : .345)}, size};
}
inline double floodlightStrength(environment::TimeOfDay time) {
    switch (time) {
    case environment::TimeOfDay::Night: return 1.35;
    case environment::TimeOfDay::Evening: return .40;
    case environment::TimeOfDay::Morning: return .16;
    default: return 0;
    }
}
inline double smoothUnit(double x) {
    x = clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
struct FloodlightField {
    std::array<gfx::SpotLight, gfx::ARENA_LIGHT_COUNT> lights{};
    int columns;
    double firstX, spacing, strength, attenuationDistance;
    FloodlightField(const Hill &h, environment::TimeOfDay time)
        : columns(floodlightColumns(h)), firstX(60 * h.scale()), spacing(floodlightSpacing(h)), strength(floodlightStrength(time)),
          attenuationDistance(45 * std::sqrt(h.scale())) {
        const double range = 100 * std::sqrt(h.scale()) + 10;
        for (int column = 0; column < columns; ++column)
            for (int side = 0; side < 2; ++side) {
                const auto mast = floodlightMast(h, column, side ? 1 : -1);
                const Vec3 aim = (Vec3{mast.lamp.x, h.ground(mast.lamp.x), 0} - mast.lamp).normalized();
                auto &light = lights[size_t(column * 2 + side)];
                light.positionRange = {float(mast.lamp.x), float(mast.lamp.y), float(mast.lamp.z),
                                       float(1 / (range * range))};
                light.directionCone = {float(aim.x), float(aim.y), float(aim.z), .55f};
            }
    }
    struct Sample {
        Vec3 direction{};
        double intensity = 0;
    };
    // Same four-source attenuation as the vertex shader, also used to choose
    // one continuous shadow direction. Never select sources by camera position.
    Sample sample(Vec3 p) const {
        Sample result;
        if (strength == 0)
            return result;
        const double coordinate = (p.x - firstX) / spacing;
        if (coordinate <= -1 || coordinate >= columns)
            return result;
        const int left = int(std::floor(coordinate));
        for (int column = left; column <= left + 1; ++column) {
            if (column < 0 || column >= columns)
                continue;
            const double longitudinal = smoothUnit(1 - std::abs(coordinate - column));
            for (int side = 0; side < 2; ++side) {
                const auto &light = lights[size_t(column * 2 + side)];
                const auto &pos = light.positionRange, &cone = light.directionCone;
                const Vec3 delta = Vec3{pos[0], pos[1], pos[2]} - p;
                const double d2 = dot(delta, delta), range = 1 - d2 * pos[3];
                if (range <= 0 || d2 < .0001)
                    continue;
                const Vec3 direction = delta * (1 / std::sqrt(d2));
                const double angle = dot(direction * -1, Vec3{cone[0], cone[1], cone[2]});
                const double beam = smoothUnit((angle - cone[3]) / (.90 - cone[3]));
                const double weight = strength * longitudinal * beam * range * range /
                                      (1 + d2 / (attenuationDistance * attenuationDistance));
                result.direction = result.direction + direction * weight;
                result.intensity += weight;
            }
        }
        return result;
    }
};
} // namespace sjp::visual
