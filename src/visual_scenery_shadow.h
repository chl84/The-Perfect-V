#pragma once
#include "visual_snow_mesh.h"
#include <optional>
#include <vector>

namespace sjp::visual {
// Intersect a decorative light ray with the existing snow mesh. A ray that
// escapes down the valley has no receiver; never stretch it onto distant snow.
inline std::optional<Vec3> projectToSnow(const Hill &h, Vec3 source, Vec3 direction) {
    direction = direction.normalized();
    if (direction.y >= -.05)
        return {};
    auto clearance = [&](double t) {
        const auto p = source + direction * t;
        return p.y - terrainSnowHeight(h, p.x, p.z);
    };
    if (clearance(0) <= .025)
        return Vec3{source.x, terrainSnowHeight(h, source.x, source.z), source.z};
    const double step = 1.5 * std::sqrt(h.scale()), limit = 200 * std::sqrt(h.scale());
    for (double end = step; end <= limit; end += step) {
        if (clearance(end) > 0)
            continue;
        double begin = end - step, finish = end;
        for (int i = 0; i < 15; ++i) {
            const double middle = (begin + finish) * .5;
            if (clearance(middle) > 0) begin = middle;
            else finish = middle;
        }
        auto p = source + direction * ((begin + finish) * .5);
        p.y = terrainSnowHeight(h, p.x, p.z);
        return p;
    }
    return {};
}
inline double footprintTurn(Vec3 a, Vec3 b, Vec3 c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}
inline std::vector<Vec3> shadowHull(std::vector<Vec3> points) {
    std::sort(points.begin(), points.end(), [](Vec3 a, Vec3 b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    });
    points.erase(std::unique(points.begin(), points.end(), [](Vec3 a, Vec3 b) {
        return a.x == b.x && a.z == b.z;
    }), points.end());
    if (points.size() < 3) return {};
    std::vector<Vec3> hull;
    for (const auto &p : points) {
        while (hull.size() >= 2 && footprintTurn(hull[hull.size()-2], hull.back(), p) <= 1e-10)
            hull.pop_back();
        hull.push_back(p);
    }
    const size_t lower = hull.size();
    for (size_t i = points.size() - 1; i-- > 0;) {
        const auto p = points[i];
        while (hull.size() > lower && footprintTurn(hull[hull.size()-2], hull.back(), p) <= 1e-10)
            hull.pop_back();
        hull.push_back(p);
    }
    hull.pop_back();
    return hull;
}

// Clip a small convex footprint to the exact coarse/refined receiver triangles.
// Used by baked scenery shadows and cached ski tracks; never by physics.
template <class Emit>
void snowDecalTriangles(const Hill &h, const std::vector<Vec3> &footprint, Emit emit) {
    if (footprint.size() < 3 || footprint.size() > 16) return;
    double area = 0, left = footprint[0].x, right = left, front = footprint[0].z, back = front;
    for (size_t i = 0; i < footprint.size(); ++i) {
        const auto p = footprint[i], q = footprint[(i + 1) % footprint.size()];
        area += p.x * q.z - q.x * p.z;
        left = std::min(left, p.x); right = std::max(right, p.x);
        front = std::min(front, p.z); back = std::max(back, p.z);
    }
    if (std::abs(area) < 1e-9) return;
    const double sign = area > 0 ? 1 : -1, dx = 3 * h.scale(), dz = 10 * h.scale();
    const double start = snowGridStart(h), end = std::max(420 * h.scale(), h.end() + 80 * h.scale());
    left = std::max(left, start); right = std::min(right, end);
    front = std::max(front, -200 * h.scale()); back = std::min(back, 200 * h.scale());
    if (left >= right || front >= back) return;
    for (double x = snowCellStart(left, start, dx); x < right; x += dx)
        for (double z = snowCellStart(front, -200 * h.scale(), dz); z < back; z += dz)
            snowCellTriangles(h, x, z, [&](double xx, double zz) { return backdropGround(h, xx, zz); },
                              [&](Vec3 a, Vec3 b, Vec3 c) {
                std::array<Vec3, 32> polygon{{a,b,c}}, next{};
                size_t count = 3;
                for (size_t edge = 0; edge < footprint.size() && count; ++edge) {
                    const auto p = footprint[edge], q = footprint[(edge+1) % footprint.size()];
                    size_t output = 0;
                    auto previous = polygon[count-1];
                    double before = sign * footprintTurn(p, q, previous);
                    for (size_t i = 0; i < count; ++i) {
                        const auto current = polygon[i];
                        const double after = sign * footprintTurn(p, q, current);
                        if ((before >= 0) != (after >= 0))
                            next[output++] = previous + (current-previous) * (before/(before-after));
                        if (after >= 0) next[output++] = current;
                        previous = current; before = after;
                    }
                    polygon = next; count = output;
                }
                for (size_t i = 1; i+1 < count; ++i)
                    if (std::abs(footprintTurn(polygon[0],polygon[i],polygon[i+1])) > 1e-10)
                        emit(polygon[0],polygon[i],polygon[i+1]);
            });
}
} // namespace sjp::visual
