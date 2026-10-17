#pragma once
#include "original_skeleton.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sjp::visual {
inline constexpr size_t cartoonHairSamples = 33;
using CartoonHairPath = std::array<Vec3, cartoonHairSamples>;
struct CartoonHairAxis {
    Vec3 side, normal, tangent;
};
using CartoonHairAxes = std::array<CartoonHairAxis, cartoonHairSamples>;

// Rotation-minimizing transport follows each bend continuously. Aligning every
// section with world-horizontal instead can choose opposite signs as a curled
// ground section sways, flipping the flattened tube by 180 degrees in one frame.
inline CartoonHairAxes cartoonHairAxes(const CartoonHairPath &path, const Frame &frame) {
    CartoonHairAxes axes{};
    for (size_t r = 0; r < path.size(); ++r) {
        const Vec3 tangent = (path[std::min(r + 1, path.size() - 1)] - path[r ? r - 1 : 0]).normalized();
        Vec3 side{0, 0, 1};
        if (r) {
            const Vec3 rotation = cross(axes[r - 1].tangent, tangent);
            const double denominator = 1 + dot(axes[r - 1].tangent, tangent);
            side = axes[r - 1].side;
            if (denominator > 1e-8)
                side = side + cross(rotation, side) + cross(rotation, cross(rotation, side)) * (1 / denominator);
        }
        side = side - tangent * dot(side, tangent);
        if (side.length() < 1e-8)
            side = cross(tangent, {0, 1, 0});
        side = side.normalized();
        axes[r] = {side, cross(tangent, side).normalized(), tangent};
    }
    // Keep the settled coils flat on the ground without a binary +/- choice.
    // Fade this preference out before the lengths begin streaming in flight.
    double settle = clamp(std::hypot(double(frame.vx), double(frame.vy)) / 4, 0.0, 1.0);
    settle = 1 - settle * settle * (3 - 2 * settle);
    if (settle > 0)
        for (size_t r = 0; r < path.size(); ++r) {
            const Vec3 up = Vec3{0, 1, 0} - axes[r].tangent * axes[r].tangent.y;
            if (up.length() < 1e-8) continue;
            const Vec3 target = cross(up.normalized(), axes[r].tangent).normalized();
            double weight = clamp((double(r) / (path.size() - 1) - .36) / .24, 0.0, 1.0);
            weight = weight * weight * (3 - 2 * weight) * settle;
            weight *= clamp((1 + dot(target, axes[r].side)) / .25, 0.0, 1.0);
            axes[r].side = (axes[r].side * (1 - weight) + target * weight).normalized();
            axes[r].normal = cross(axes[r].tangent, axes[r].side).normalized();
        }
    return axes;
}

// Presentation only. No accumulated state: pause, replay seeks and interpolated
// frames reproduce exactly the same roots, streaming and travelling gusts.
inline CartoonHairPath cartoonHairPath(const OriginalSkeleton &s, const Frame &frame) {
    using J = OriginalJoint;
    const Vec3 head = s[J::Head], shoulder = s[J::ShoulderCenter], hip = s[J::HipCenter];
    const auto forwardOf = [](Vec3 up) {
        return cross(up, std::abs(up.z) < .9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0}).normalized();
    };
    const Vec3 up = (shoulder - hip).normalized(), forward = forwardOf(up), side = cross(forward, up).normalized();
    const Vec3 headUp = (head - shoulder).normalized(), headForward = forwardOf(headUp);
    const Vec3 ankles = (s[J::Ankle1] + s[J::Ankle2]) * .5, knees = (s[J::Knee1] + s[J::Knee2]) * .5;
    const Vec3 skiForward = (s[J::SkiFront1] + s[J::SkiFront2] - s[J::SkiRear1] - s[J::SkiRear2]).normalized();
    const Vec3 skiSide = cross(skiForward, std::abs(skiForward.y) < .95 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}).normalized();
    const Vec3 skiUp = cross(skiSide, skiForward).normalized();
    const Vec3 root = head - headForward * .115 + headUp * .070;
    const std::array<Vec3, 11> drape{
        root, shoulder - forward * .18 + side * .10 + up * .04,
        hip - forward * .25 + side * .20,
        knees - forward * .34 + side * .23,
        ankles - skiForward * .40 + skiSide * .32 + skiUp * .085,
        ankles - skiForward * .70 + skiSide * .24 + skiUp * .085,
        ankles - skiForward * .12 - skiSide * .10 + skiUp * .085,
        ankles + skiForward * .62 - skiSide * .20 + skiUp * .100,
        ankles + skiForward * .88 - skiSide * .04 + skiUp * .165,
        ankles + skiForward * .58 + skiSide * .05 + skiUp * .205,
        ankles + skiForward * .39 - skiSide * .10 + skiUp * .120};
    double stream = clamp((std::hypot(double(frame.vx), double(frame.vy)) - 2) / 18, 0.0, 1.0);
    stream = stream * stream * (3 - 2 * stream);
    Vec3 airflow{-double(frame.vx), -double(frame.vy), 0};
    airflow = airflow.length() > 1e-6 ? airflow.normalized() : Vec3{-1, .05, 0}.normalized();
    const double gust = clamp(std::abs(double(frame.windSpeed)) / 8, 0.0, 1.0);
    CartoonHairPath path{};
    for (size_t i = 0; i < path.size(); ++i) {
        const double t = double(i) / (path.size() - 1), position = t * (drape.size() - 1);
        const size_t n = std::min(size_t(position), drape.size() - 2);
        const Vec3 a = drape[n ? n - 1 : 0], b = drape[n], c = drape[n + 1],
                   d = drape[std::min(n + 2, drape.size() - 1)];
        const double u = position - n, u2 = u * u, u3 = u2 * u;
        const Vec3 resting = (b * 2 + (c - a) * u + (a * 2 - b * 5 + c * 4 - d) * u2 +
                              (b * 3 - a - c * 3 + d) * u3) * .5;
        const Vec3 streamed = root + airflow * (3.8 * t) +
                              Vec3{0, .18 * t + .12 * std::sin(t * 2 * PI), .15 * std::sin(t * 3 * PI)};
        const double envelope = t * t;
        const double phase = double(frame.elapsed) * (5.5 + gust * 2) - t * 13;
        const Vec3 flutter{.025 * std::sin(phase * .8),
                           (.015 + .15 * stream + .03 * gust) * std::sin(phase),
                           (.025 + .19 * stream + .05 * gust) * std::sin(phase * 1.23 + .7)};
        path[i] = resting * (1 - stream) + streamed * stream + flutter * envelope;
        // The draped lower lengths rest above the ski plane, not below it.
        if (t > .45 && stream < .75) {
            const double clearance = dot(path[i] - ankles, skiUp);
            if (clearance < .065)
                path[i] = path[i] + skiUp * ((.065 - clearance) * (1 - stream));
        }
    }
    return path;
}
} // namespace sjp::visual
