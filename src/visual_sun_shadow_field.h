#pragma once
#include "visual_mountains.h"
#include "visual_scenery_mask.h"
#include "visual_terrain_sun.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace sjp::visual {
// Coarse, smooth sun occlusion by the terrain itself. A ridge between a snow
// point and the sun darkens that point exactly as much as an opaque caster.
class TerrainShadowGrid {
  public:
    double x0 = 0, z0 = 0, step = 1;
    int columns = 0, rows = 0;
    std::vector<float> opacity;

    template <class Exposure>
    TerrainShadowGrid(const Hill &h, const TerrainSunHorizon &horizon, double strength, Exposure exposure,
                      double spacing) {
        const double s = h.scale();
        x0 = snowGridStart(h);
        z0 = -200 * s;
        step = spacing;
        const double x1 = std::max(420 * s, h.end() + 80 * s), z1 = 200 * s;
        columns = int(std::ceil((x1 - x0) / step));
        rows = int(std::ceil((z1 - z0) / step));
        std::vector<float> raw(size_t(columns) * rows, 0.f);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column) {
                const double x = x0 + (column + .5) * step, z = z0 + (row + .5) * step;
                const Vec3 p{x, terrainSnowHeight(h, x, z), z};
                const double blocked = 1 - horizon.visibility(p);
                if (blocked <= 0)
                    continue;
                raw[size_t(row * columns + column)] =
                    float(strength * blocked * exposure(p) * valleyEdgeOpacity(h, z, x));
            }
        // One small binomial blur keeps the coarse grid from reading as cells.
        opacity.assign(raw.size(), 0.f);
        constexpr float weights[]{1, 2, 1};
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column) {
                float sum = 0, total = 0;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int r = row + dz, c = column + dx;
                        if (r < 0 || r >= rows || c < 0 || c >= columns)
                            continue;
                        const float w = weights[dx + 1] * weights[dz + 1];
                        sum += w * raw[size_t(r * columns + c)];
                        total += w;
                    }
                opacity[size_t(row * columns + column)] = sum / total;
            }
    }
    double sample(double x, double z) const {
        if (opacity.empty())
            return 0;
        const double u = clamp((x - x0) / step - .5, 0.0, double(columns - 1));
        const double v = clamp((z - z0) / step - .5, 0.0, double(rows - 1));
        const int c = int(u), r = int(v), cc = std::min(c + 1, columns - 1), rr = std::min(r + 1, rows - 1);
        auto at = [&](int column, int row) { return double(opacity[size_t(row * columns + column)]); };
        const double top = at(c, r) * (1 - (u - c)) + at(cc, r) * (u - c);
        const double bottom = at(c, rr) * (1 - (u - c)) + at(cc, rr) * (u - c);
        return top * (1 - (v - r)) + bottom * (v - r);
    }
    // Largest opacity touching a world rectangle, for skipping empty decal cells.
    double maximum(double left, double right, double front, double back) const {
        const int c0 = std::max(0, int(std::floor((left - x0) / step)) - 1);
        const int c1 = std::min(columns - 1, int(std::floor((right - x0) / step)) + 1);
        const int r0 = std::max(0, int(std::floor((front - z0) / step)) - 1);
        const int r1 = std::min(rows - 1, int(std::floor((back - z0) / step)) + 1);
        double result = 0;
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                result = std::max(result, double(opacity[size_t(r * columns + c)]));
        return result;
    }
};

// Everything a frame needs to know about one baked sun direction: the
// horizon for the athlete and the combined snow occlusion beneath it.
struct SunShadowField {
    std::unique_ptr<TerrainSunHorizon> horizon;
    std::unique_ptr<SceneryShadowMask> scenery;
    std::unique_ptr<TerrainShadowGrid> terrain;
    size_t textureBytes = 0;

    double visibility(Vec3 p) const { return horizon ? horizon->visibility(p) : 1; }
    // Opacity of the baked sun shadow already drawn on the snow at x/z.
    double opacity(double x, double z) const {
        double objects = 0;
        if (scenery) {
            const int cx = int(std::floor(x / scenery->step)), cz = int(std::floor(z / scenery->step));
            objects = scenery->sample(cx, cz) / 255.0;
        }
        const double ground = terrain ? terrain->sample(x, z) : 0;
        return 1 - (1 - objects) * (1 - ground);
    }
};
} // namespace sjp::visual
