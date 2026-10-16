#pragma once
#include "visual_terrain_profile.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sjp::visual {
// Presentation tessellation only. Original Hill evaluators remain authoritative
// for physics, scoring, poses and recordings.
inline constexpr int snowSubdivisions = 4;
inline double snowGridStart(const Hill &h) {
    return std::min(-170 * h.scale(), h.inrunStart() - 60 * h.scale());
}
inline double inrunSnowStep(const Hill &h) {
    return 2 * h.scale() / snowSubdivisions;
}
inline bool refinedSnowCell(const Hill &h, double x) {
    return x + 3 * h.scale() > 0 && x < h.end() + 8 * h.scale();
}
inline double snowCellStart(double value, double start, double step) {
    return start + std::floor((value - start) / step) * step;
}

// The two central rows have four short quads each. Adjacent rows join their
// fine edge to the original coarse edge with a fan: no T-junctions or moving LOD.
// Farther scenery retains its original nodes and triangles.
template <class Height, class Emit>
void snowCellTriangles(const Hill &h, double x, double z, Height height, Emit emit) {
    const double dx = 3 * h.scale(), dz = 10 * h.scale();
    auto point = [&](double xx, double zz) { return Vec3{xx, height(xx, zz), zz}; };
    const double middle = z + dz * .5;
    if (!refinedSnowCell(h, x) || std::abs(middle) > 20 * h.scale()) {
        const auto a = point(x, z), b = point(x, z + dz), c = point(x + dx, z + dz), d = point(x + dx, z);
        emit(a, b, c);
        emit(a, c, d);
    } else if (std::abs(middle) < 10 * h.scale()) {
        for (int i = 0; i < snowSubdivisions; ++i) {
            const double a = x + dx * i / snowSubdivisions, b = x + dx * (i + 1) / snowSubdivisions;
            const auto aa = point(a, z), bb = point(a, z + dz), cc = point(b, z + dz), dd = point(b, z);
            emit(aa, bb, cc);
            emit(aa, cc, dd);
        }
    } else {
        const double inner = middle > 0 ? z : z + dz, outer = middle > 0 ? z + dz : z;
        const auto anchor = point(x, outer);
        auto previous = point(x, inner);
        for (int i = 1; i <= snowSubdivisions; ++i) {
            const auto next = point(x + dx * i / snowSubdivisions, inner);
            if (middle > 0)
                emit(anchor, next, previous);
            else
                emit(anchor, previous, next);
            previous = next;
        }
        if (middle > 0)
            emit(anchor, point(x + dx, outer), previous);
        else
            emit(anchor, previous, point(x + dx, outer));
    }
}

inline double snowPlaneHeight(Vec3 a, Vec3 b, Vec3 c, double x, double z) {
    const auto normal = cross(b - a, c - a);
    return a.y - (normal.x * (x - a.x) + normal.z * (z - a.z)) / normal.y;
}
// Clip decals to each actual receiver triangle. Interpolating all coordinates
// keeps marker vertices on the snow plane, including the stitched belt edges.
template <class Emit>
void clipSnowTriangle(Vec3 a, Vec3 b, Vec3 c, double left, double right, double front, double back,
                      Emit emit) {
    std::array<Vec3, 8> polygon{{a, b, c}}, next{};
    size_t count = 3;
    auto clip = [&](auto distance) {
        size_t output = 0;
        if (!count)
            return;
        Vec3 previous = polygon[count - 1];
        double before = distance(previous);
        for (size_t i = 0; i < count; ++i) {
            const Vec3 p = polygon[i];
            const double after = distance(p);
            if ((before >= 0) != (after >= 0) && before != 0 && after != 0)
                next[output++] = previous + (p - previous) * (before / (before - after));
            if (after >= 0)
                next[output++] = p;
            previous = p;
            before = after;
        }
        polygon = next;
        count = output;
    };
    clip([&](Vec3 p) { return p.x - left; });
    clip([&](Vec3 p) { return right - p.x; });
    clip([&](Vec3 p) { return p.z - front; });
    clip([&](Vec3 p) { return back - p.z; });
    if (count >= 3)
        emit(polygon, count);
}
inline bool snowTriangleContains(Vec3 a, Vec3 b, Vec3 c, double x, double z) {
    auto side = [&](Vec3 p, Vec3 q) { return (q.z - p.z) * (x - p.x) - (q.x - p.x) * (z - p.z); };
    const double tolerance = 1e-8;
    return side(a, b) >= -tolerance && side(b, c) >= -tolerance && side(c, a) >= -tolerance;
}
template <class Height> double terrainSnowHeight(const Hill &h, double x, double z, Height height) {
    const double dx = 3 * h.scale(), dz = 10 * h.scale();
    double x0 = snowCellStart(x, snowGridStart(h), dx);
    const double z0 = snowCellStart(z, -200 * h.scale(), dz);
    // The ski corridor has no lateral relief. This is the hot path for camera,
    // athlete shadow and fall contact, with just two original height samples.
    if (std::max(std::abs(z0), std::abs(z0 + dz)) < 19 * h.scale()) {
        double step = dx;
        if (refinedSnowCell(h, x0)) {
            step /= snowSubdivisions;
            x0 = snowCellStart(x, x0, step);
        }
        const double t = (x - x0) / step;
        return height(x0, 0) * (1 - t) + height(x0 + step, 0) * t;
    }
    double result = 0;
    snowCellTriangles(h, x0, z0, height, [&](Vec3 a, Vec3 b, Vec3 c) {
        if (snowTriangleContains(a, b, c, x, z))
            result = snowPlaneHeight(a, b, c, x, z);
    });
    return result;
}
inline double terrainSnowHeight(const Hill &h, double x, double z) {
    return terrainSnowHeight(h, x, z, [&](double xx, double zz) { return backdropGround(h, xx, zz); });
}
inline double inrunSnowHeight(const Hill &h, double x) {
    const double a = snowCellStart(x, h.inrunStart(), inrunSnowStep(h));
    const double b = std::min(0.0, a + inrunSnowStep(h));
    const double t = b > a ? (x - a) / (b - a) : 0;
    return h.inrun(a) * (1 - t) + h.inrun(b) * t;
}

// Visit all bends along an edge/ray, including the transition fans. Intersections
// use only X/Z topology; no expensive height evaluation is needed here.
template <class Emit> void snowEdgeCrossings(const Hill &h, Vec3 a, Vec3 b, Emit emit) {
    const double dx = 3 * h.scale(), dz = 10 * h.scale();
    const double start = snowGridStart(h), zStart = -200 * h.scale();
    if (std::max(std::abs(a.z), std::abs(b.z)) <= 10 * h.scale()) {
        // Inside the flat-width ski corridor only longitudinal bends matter.
        // This keeps the existing whole-body fall measurement inexpensive.
        if (std::abs(b.x - a.x) > 1e-10) {
            const double low = std::min(a.x, b.x), high = std::max(a.x, b.x);
            const double step = dx / snowSubdivisions;
            for (double x = start + std::ceil((low - start) / step) * step; x < high; x += step) {
                const double t = (x - a.x) / (b.x - a.x);
                if (t > 0 && t < 1)
                    emit(t);
            }
        }
        return;
    }
    const int firstX = int(std::floor((std::min(a.x, b.x) - start) / dx));
    const int lastX = int(std::floor((std::max(a.x, b.x) - start) / dx));
    const int firstZ = int(std::floor((std::min(a.z, b.z) - zStart) / dz));
    const int lastZ = int(std::floor((std::max(a.z, b.z) - zStart) / dz));
    const auto delta = b - a;
    auto intersect = [&](Vec3 p, Vec3 q) {
        const auto edge = q - p, offset = p - a;
        const double determinant = delta.x * edge.z - delta.z * edge.x;
        if (std::abs(determinant) < 1e-12)
            return;
        const double t = (offset.x * edge.z - offset.z * edge.x) / determinant;
        const double u = (offset.x * delta.z - offset.z * delta.x) / determinant;
        if (t > 0 && t < 1 && u >= -1e-9 && u <= 1 + 1e-9)
            emit(t);
    };
    for (int ix = firstX; ix <= lastX; ++ix)
        for (int iz = firstZ; iz <= lastZ; ++iz)
            snowCellTriangles(
                h, start + ix * dx, zStart + iz * dz, [](double, double) { return 0.0; },
                [&](Vec3 p, Vec3 q, Vec3 r) {
                    intersect(p, q);
                    intersect(q, r);
                    intersect(r, p);
                });
}
} // namespace sjp::visual
