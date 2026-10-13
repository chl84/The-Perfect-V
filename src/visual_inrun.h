#pragma once
#include "camera_path.h"
#include "visual_floodlights.h"
#include <vector>

namespace sjp::visual {
inline constexpr double inrunWalkInner = 2.45, inrunWalkOuter = 3.65, inrunColumnZ = 2.85;
// Camera 1 views from +Z. Keep the service stairs and fixtures on the far side.
inline constexpr int inrunServiceSide = -1;
struct InrunSupport {
    double x = 0, footingBottom = 0, footingTop = 0, deck = 0;
    double height() const { return deck - footingTop; }
    bool exposed() const { return height() > .18; }
};
struct InrunLayout {
    std::vector<InrunSupport> supports;
    std::array<gfx::SpotLight, gfx::ARENA_LIGHT_COUNT> lights{};
    int lightColumns = 1;
    double firstLightX = -2.5, lightSpacing = 12;
    static constexpr double lightRange = 14, attenuationDistance = 5.5;

    void build(const Hill &h) {
        supports.clear();
        const CameraSurface snow(h);
        const double first = h.inrunStart() + .4, last = -.75;
        const int bays = int(std::ceil((last - first) / clamp(7 + h.scale(), 7.0, 9.0)));
        double maxHeight = 0;
        for (int i = 0; i <= bays; ++i) {
            const double x = i == bays ? last : first + (last - first) * i / bays;
            // Read the actual unmodified snow triangles outside the track.
            // The footing embeds at both ends even on a steep hillside.
            const double a = snow.height(x - .55, inrunColumnZ), b = snow.height(x + .55, inrunColumnZ);
            InrunSupport support{x, std::min(a, b) - .22, std::max(a, b) + .12, h.inrun(x) - .46};
            supports.push_back(support);
            maxHeight = std::max(maxHeight, support.height());
        }
        const bool large = -h.inrunStart() >= 68 || maxHeight >= 35;
        firstLightX = large ? h.start() + 6 : -2.5;
        lightColumns = large ? clamp(int(std::ceil((-2.5 - firstLightX) / 20)) + 1, 2, 5) : 1;
        lightSpacing = lightColumns > 1 ? (-2.5 - firstLightX) / (lightColumns - 1) : 12;
        lights = {};
        for (int column = 0; column < lightColumns; ++column) {
            const double x = firstLightX + column * lightSpacing;
            const Vec3 source{x, h.inrun(x) + 2.4, inrunServiceSide * 3.28};
            const Vec3 aim = (Vec3{x + 1, h.inrun(std::min(0.0, x + 1)), 0} - source).normalized();
            lights[size_t(column)] = {
                {float(source.x), float(source.y), float(source.z), float(1 / (lightRange * lightRange))},
                {float(aim.x), float(aim.y), float(aim.z), .40f}};
        }
    }
    static double strength(environment::TimeOfDay time) {
        switch (time) {
        case environment::TimeOfDay::Night: return .70;
        case environment::TimeOfDay::Evening: return .18;
        case environment::TimeOfDay::Morning: return .06;
        default: return 0;
        }
    }
    FloodlightField::Sample sample(Vec3 p, environment::TimeOfDay time) const {
        FloodlightField::Sample result;
        const double power = strength(time), coordinate = (p.x - firstLightX) / lightSpacing;
        if (power == 0 || coordinate <= -1 || coordinate >= lightColumns || p.x >= 12)
            return result;
        const int left = int(std::floor(coordinate));
        for (int column = left; column <= left + 1; ++column) {
            if (column < 0 || column >= lightColumns)
                continue;
            const double along = smoothUnit(1 - std::abs(coordinate - column));
            const auto &light = lights[size_t(column)];
            const auto &pos = light.positionRange, &cone = light.directionCone;
            const Vec3 delta = Vec3{pos[0], pos[1], pos[2]} - p;
            const double d2 = dot(delta, delta), range = 1 - d2 * pos[3];
            if (range <= 0 || d2 < .0001)
                continue;
            const Vec3 direction = delta * (1 / std::sqrt(d2));
            const double beam = smoothUnit((dot(direction * -1, Vec3{cone[0], cone[1], cone[2]}) - cone[3]) /
                                          (.90 - cone[3]));
            const double weight = power * along * beam * range * range /
                                  (1 + d2 / (attenuationDistance * attenuationDistance));
            result.direction = result.direction + direction * weight;
            result.intensity += weight;
        }
        return result;
    }
};
} // namespace sjp::visual
