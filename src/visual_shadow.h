#pragma once
#include "visual_floodlights.h"
#include "original_skeleton.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sjp::visual {
struct LandingShadow {
    Vec3 cast;
    double opacity, softness;
};
inline LandingShadow landingShadow(const EnvironmentLight &light, double clearance, double slope,
                                   FloodlightField::Sample lamps = {}) {
    clearance = std::max(0.0, clearance);
    const double sunWeight = light.direct.length();
    Vec3 direction = (light.direction * sunWeight + lamps.direction).normalized();
    direction = mix(Vec3{0, 1, 0}, direction, smoothUnit((direction.y - .05) / .15)).normalized();
    // A broad sky contribution takes over smoothly at grazing incidence. It
    // retains one projection instead of dropping the silhouette entirely.
    const Vec3 normal = Vec3{-slope, 1, 0}.normalized();
    const double direct = smoothUnit((dot(normal, direction) - .08) / .32);
    direction = mix(Vec3{0, 1, 0}, direction, direct).normalized();
    const double nearSnow = std::exp(-clearance / 32);
    const double strength = std::max({light.shadowStrength * direct,
                                      .32 * std::min(1.0, lamps.intensity), .28 * nearSnow});
    const double softness = (.025 + std::min(.18, clearance * .005)) *
                            (1 + (light.shadowSoftness - 1) * (1 - nearSnow) * .5);
    return {{-direction.x / direction.y, 0, -direction.z / direction.y},
            strength * std::exp(-clearance / 85), softness};
}
struct ShadowCapsule {
    Vec3 a, b;
    double radius;
};

// Compose coverage before drawing: overlapping limbs cannot darken each other.
// The bounded CPU mask becomes a single non-overlapping mesh, without textures,
// offscreen passes or per-part alpha blending on the snow.
struct ShadowMask {
    static constexpr int columns = 96, rows = 64;
    std::array<float, columns * rows> coverage{};
    double x = 0, z = 0, dx = 1, dz = 1;

    void build(const std::vector<ShadowCapsule> &parts, double sigma) {
        coverage.fill(0);
        if (parts.empty())
            return;
        double right = parts[0].a.x, top = parts[0].a.z;
        x = right;
        z = top;
        for (const auto &part : parts)
            for (Vec3 p : {part.a, part.b}) {
                x = std::min(x, p.x - part.radius);
                z = std::min(z, p.z - part.radius);
                right = std::max(right, p.x + part.radius);
                top = std::max(top, p.z + part.radius);
            }
        const double padding = 3 * sigma + .08;
        x -= padding;
        z -= padding;
        dx = (right + padding - x) / (columns - 1);
        dz = (top + padding - z) / (rows - 1);
        const double antialias = std::hypot(dx, dz) * .5;
        for (const auto &part : parts) {
            const double radius = part.radius + antialias;
            const int left = std::max(0, int(std::floor((std::min(part.a.x, part.b.x) - radius - x) / dx)));
            const int right =
                std::min(columns - 1, int(std::ceil((std::max(part.a.x, part.b.x) + radius - x) / dx)));
            const int bottom = std::max(0, int(std::floor((std::min(part.a.z, part.b.z) - radius - z) / dz)));
            const int top =
                std::min(rows - 1, int(std::ceil((std::max(part.a.z, part.b.z) + radius - z) / dz)));
            const double vx = part.b.x - part.a.x, vz = part.b.z - part.a.z;
            const double length2 = vx * vx + vz * vz;
            for (int row = bottom; row <= top; ++row)
                for (int column = left; column <= right; ++column) {
                    const double px = x + column * dx - part.a.x, pz = z + row * dz - part.a.z;
                    const double t =
                        length2 > 1e-12 ? std::clamp((px * vx + pz * vz) / length2, 0.0, 1.0) : 0;
                    const double distance = std::hypot(px - t * vx, pz - t * vz);
                    const double alpha = std::clamp((radius - distance) / (2 * antialias), 0.0, 1.0);
                    auto &value = coverage[size_t(row * columns + column)];
                    value = std::max(value, float(alpha * alpha * (3 - 2 * alpha)));
                }
        }
        if (sigma <= 0)
            return;
        auto kernel = [&](double step) {
            const int radius = int(std::ceil(3 * sigma / step));
            std::vector<float> weights(size_t(radius * 2 + 1));
            double sum = 0;
            for (int i = -radius; i <= radius; ++i) {
                const double weight = std::exp(-.5 * std::pow(i * step / sigma, 2));
                weights[size_t(i + radius)] = float(weight);
                sum += weight;
            }
            for (float &weight : weights)
                weight /= float(sum);
            return weights;
        };
        const auto horizontal = kernel(dx), vertical = kernel(dz);
        std::array<float, columns * rows> temporary{};
        const int rx = int(horizontal.size()) / 2, rz = int(vertical.size()) / 2;
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                for (int i = std::max(-rx, -column); i <= std::min(rx, columns - 1 - column); ++i)
                    temporary[size_t(row * columns + column)] +=
                        coverage[size_t(row * columns + column + i)] * horizontal[size_t(i + rx)];
        coverage.fill(0);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                for (int i = std::max(-rz, -row); i <= std::min(rz, rows - 1 - row); ++i)
                    coverage[size_t(row * columns + column)] +=
                        temporary[size_t((row + i) * columns + column)] * vertical[size_t(i + rz)];
    }
};

inline std::vector<ShadowCapsule> shadowSilhouette(const OriginalSkeleton &skeleton,
                                                Vec3 cast = {.32, 0, -.25}, double clearance = 0,
                                                double slope = 0) {
    auto project = [&](OriginalJoint joint) {
        const Vec3 p = skeleton[joint];
        // Apply one projection direction to every joint, including feet below
        // the pose origin. The whole silhouette follows the displayed pose.
        const double distance = (clearance + p.y - slope * p.x) / std::max(.05, 1 + slope * cast.x);
        return Vec3{p.x + distance * cast.x, 0, p.z + distance * cast.z};
    };
    std::vector<ShadowCapsule> parts;
    parts.reserve(17);
    auto segment = [&](OriginalJoint a, OriginalJoint b, double radius) {
        parts.push_back({project(a), project(b), radius});
    };
    segment(OriginalJoint::HipCenter, OriginalJoint::ShoulderCenter, .18);
    segment(OriginalJoint::ShoulderCenter, OriginalJoint::Head, .08);
    segment(OriginalJoint::Head, OriginalJoint::Head, .19);
    segment(OriginalJoint::Hip1, OriginalJoint::Hip2, .09);
    segment(OriginalJoint::Shoulder1, OriginalJoint::Shoulder2, .09);
    const OriginalJoint ankles[]{OriginalJoint::Ankle1, OriginalJoint::Ankle2};
    const OriginalJoint knees[]{OriginalJoint::Knee1, OriginalJoint::Knee2};
    const OriginalJoint hips[]{OriginalJoint::Hip1, OriginalJoint::Hip2};
    const OriginalJoint shoulders[]{OriginalJoint::Shoulder1, OriginalJoint::Shoulder2};
    const OriginalJoint elbows[]{OriginalJoint::Elbow1, OriginalJoint::Elbow2};
    const OriginalJoint hands[]{OriginalJoint::Hand1, OriginalJoint::Hand2};
    const OriginalJoint rear[]{OriginalJoint::SkiRear1, OriginalJoint::SkiRear2};
    const OriginalJoint front[]{OriginalJoint::SkiFront1, OriginalJoint::SkiFront2};
    const OriginalJoint tips[]{OriginalJoint::SkiTip1, OriginalJoint::SkiTip2};
    for (size_t side = 0; side < 2; ++side) {
        segment(hips[side], knees[side], .105);
        segment(knees[side], ankles[side], .075);
        segment(shoulders[side], elbows[side], .06);
        segment(elbows[side], hands[side], .05);
        segment(rear[side], front[side], .036);
        segment(front[side], tips[side], .033);
    }
    return parts;
}
} // namespace sjp::visual
