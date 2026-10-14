#pragma once
#include "draw_commands.h"
#include "physics.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sjp::visual {
struct StartSignal {
    static constexpr double radius = .16, lensSpacing = .23, range = 6, attenuationDistance = 2;
    Vec3 base, center, front, right;
    bool ready, blink;

    StartSignal(const Hill &hill, const Frame &frame) {
        const double x = hill.start() + 2;
        base = {x, hill.inrun(x), -3};
        front = Vec3{-2, 0, 3}.normalized();
        center = base + Vec3{0, 2.7, 0} + front * .12;
        right = cross({0, 1, 0}, front).normalized();
        ready = Phase(frame.phase) == Phase::Ready;
        blink = ready && frame.elapsed >= 5 && std::fmod(frame.elapsed, .7) > .35;
    }
    Vec3 lens(int index) const {
        return center + Vec3{0, index == 0 ? lensSpacing : -lensSpacing, 0} + front * .105;
    }
    bool lit(int index) const { return !blink && index == (ready ? 1 : 0); }
    std::array<float, 4> emission(int index) const {
        if (!lit(index))
            return {0, 0, 0, 0};
        return index == 0 ? std::array<float, 4>{.48f, .016f, .008f, 0}
                          : std::array<float, 4>{.035f, .47f, .07f, 0};
    }
    gfx::SpotLight light() const {
        const auto p = lens(ready ? 1 : 0) + front * .028;
        return {{float(p.x), float(p.y), float(p.z), float(1 / (range * range))},
                {float(front.x), float(front.y), float(front.z), .25f}};
    }
    std::array<float, 4> lightColor() const {
        const float strength = blink ? 0 : .55f;
        return ready ? std::array<float, 4>{.075f, 1, .15f, strength}
                     : std::array<float, 4>{1, .035f, .017f, strength};
    }
    // Same short-range, directional diffuse light as the vertex shader.
    // No simulation state, random values or extra shadow passes.
    Vec3 sample(Vec3 p, Vec3 normal) const {
        const auto source = light();
        const auto color = lightColor();
        const Vec3 delta =
            Vec3{source.positionRange[0], source.positionRange[1], source.positionRange[2]} - p;
        const double d2 = dot(delta, delta), falloff = 1 - d2 * source.positionRange[3];
        if (color[3] == 0 || falloff <= 0 || d2 < .0001)
            return {};
        const Vec3 direction = delta * (1 / std::sqrt(d2));
        double cone = clamp((dot(direction * -1, front) - .25) / (.90 - .25), 0.0, 1.0);
        cone = cone * cone * (3 - 2 * cone);
        const double weight = color[3] * cone * falloff * falloff * std::max(0.0, dot(normal, direction)) /
                              (1 + d2 / (attenuationDistance * attenuationDistance));
        return Vec3{color[0], color[1], color[2]} * weight;
    }
};
} // namespace sjp::visual
