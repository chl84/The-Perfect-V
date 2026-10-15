#pragma once
#include "physics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sjp::visual {
inline constexpr int contactShadowTextureSize = 64;
inline std::array<uint8_t, contactShadowTextureSize * contactShadowTextureSize> contactShadowMask() {
    std::array<uint8_t, contactShadowTextureSize * contactShadowTextureSize> pixels{};
    for (int y = 0; y < contactShadowTextureSize; ++y)
        for (int x = 0; x < contactShadowTextureSize; ++x) {
            const double u = 2.0 * x / (contactShadowTextureSize - 1) - 1;
            const double v = 2.0 * y / (contactShadowTextureSize - 1) - 1;
            const double fade = std::max(0.0, 1 - std::hypot(u, v));
            pixels[size_t(y * contactShadowTextureSize + x)] = uint8_t(std::lround(255 * fade * fade));
        }
    return pixels;
}

// Clip the existing snow triangles to a small 12-sided decal footprint. All
// new vertices interpolate their receiver triangle, including cell diagonals;
// a single large quad would cut through curved/steep snow. Built only once
// with the cached scenery, never used by physics or the athlete's shadow.
template <class Height, class Emit>
void contactShadowTriangles(const Hill &hill, Vec3 center, double rx, double rz,
                            Height height, Emit emit) {
    const double step = 3 * hill.scale(), zStep = 10 * hill.scale();
    const double start = std::min(-170 * hill.scale(), hill.inrunStart() - 60 * hill.scale());
    const double zStart = -200 * hill.scale();
    const double left = center.x - rx, right = center.x + rx;
    const double front = center.z - rz, back = center.z + rz;
    auto clipTriangle = [&](Vec3 a, Vec3 b, Vec3 c) {
        std::array<Vec3, 16> polygon{{a, b, c}}, next{};
        int count = 3;
        auto clip = [&](double nx, double nz) {
            int output = 0;
            auto distance = [&](Vec3 p) {
                return nx * (p.x - center.x) / rx + nz * (p.z - center.z) / rz - std::cos(PI / 12);
            };
            for (int i = 0; i < count; ++i) {
                const Vec3 p = polygon[size_t(i)], q = polygon[size_t((i + 1) % count)];
                const double pDistance = distance(p), qDistance = distance(q);
                const bool pInside = pDistance <= 0, qInside = qDistance <= 0;
                if (pInside)
                    next[size_t(output++)] = p;
                if (pInside != qInside && pDistance != 0 && qDistance != 0)
                    next[size_t(output++)] = p + (q - p) * (pDistance / (pDistance - qDistance));
            }
            polygon = next;
            count = output;
        };
        // Avoid shading/blending the transparent corners of a large rectangle.
        // Only the faint outermost falloff is trimmed; the texture supplies the
        // smooth interior without the old four radial geometry bands.
        for (int side = 0; side < 12 && count; ++side) {
            const double angle = (side + .5) * 2 * PI / 12;
            clip(std::cos(angle), std::sin(angle));
        }
        for (int i = 1; i + 1 < count; ++i) {
            const auto normal = cross(polygon[size_t(i)] - polygon[0], polygon[size_t(i + 1)] - polygon[0]);
            if (std::abs(normal.y) > 1e-10)
                emit(polygon[0], polygon[size_t(i)], polygon[size_t(i + 1)]);
        }
    };
    const int firstX = int(std::floor((left - start) / step));
    const int lastX = int(std::ceil((right - start) / step));
    const int firstZ = int(std::floor((front - zStart) / zStep));
    const int lastZ = int(std::ceil((back - zStart) / zStep));
    for (int x = firstX; x < lastX; ++x)
        for (int z = firstZ; z < lastZ; ++z) {
            const double x0 = start + x * step, x1 = x0 + step;
            const double z0 = zStart + z * zStep, z1 = z0 + zStep;
            const Vec3 a{x0, height(x0, z0), z0}, b{x0, height(x0, z1), z1};
            const Vec3 c{x1, height(x1, z1), z1}, d{x1, height(x1, z0), z0};
            clipTriangle(a, b, c);
            clipTriangle(a, c, d);
        }
}
} // namespace sjp::visual
