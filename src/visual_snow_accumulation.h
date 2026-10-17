#pragma once
#include "physics.h"
#include "visual_snow_mesh.h"
#include <algorithm>
#include <array>
#include <vector>

namespace sjp::visual {
struct AccumulatedSnowVertex {
    Vec3 position, normal;
};
// Built once with each venue. No animation, solver, textures or runtime RNG.
class SnowAccumulation {
  public:
    std::vector<std::array<AccumulatedSnowVertex, 3>> triangles;
    static double variation(double seed) {
        return std::fmod(std::abs(std::sin(seed * 12.9898) * 43758.5453), 1.0);
    }
    static double smoothVariation(double coordinate, double seed) {
        const double cell = std::floor(coordinate), t = coordinate - cell;
        const double blend = t * t * (3 - 2 * t);
        return variation(seed + cell * 19.71) * (1 - blend) + variation(seed + (cell + 1) * 19.71) * blend;
    }
    void triangle(AccumulatedSnowVertex a, AccumulatedSnowVertex b, AccumulatedSnowVertex c) {
        const auto n = cross(b.position - a.position, c.position - a.position);
        if (n.length() < 1e-9)
            return;
        if (n.y < 0)
            std::swap(b, c);
        triangles.push_back({a, b, c});
    }
    template <class Point> void grid(int rows, int columns, Point point, bool closed = false) {
        std::vector<AccumulatedSnowVertex> mesh(size_t(rows + 1) * (columns + 1));
        auto at = [&](int r, int c) -> AccumulatedSnowVertex & {
            return mesh[size_t(r) * (columns + 1) + c];
        };
        for (int r = 0; r <= rows; ++r)
            for (int c = 0; c <= columns; ++c)
                at(r, c).position = point(r, c);
        for (int r = 0; r <= rows; ++r)
            for (int c = 0; c <= columns; ++c) {
                Vec3 n = cross(at(std::min(rows, r + 1), c).position - at(std::max(0, r - 1), c).position,
                               at(r, std::min(columns, c + 1)).position - at(r, std::max(0, c - 1)).position);
                if (n.y < 0)
                    n = n * -1;
                at(r, c).normal = n.length() > 1e-9 ? n.normalized() : Vec3{0, 1, 0};
            }
        if (closed)
            for (int c = 0; c <= columns; ++c)
                at(0, c).normal = at(rows, c).normal = (at(0, c).normal + at(rows, c).normal).normalized();
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < columns; ++c) {
                triangle(at(r, c), at(r + 1, c), at(r + 1, c + 1));
                triangle(at(r, c), at(r + 1, c + 1), at(r, c + 1));
            }
    }
    // Independent shape controls avoid scaled copies of the same pillow.
    template <class Path> void capOnPath(Path path, double width, double height, double seed) {
        const auto random = [&](int n) { return variation(seed + n * 17.137); };
        const std::array<double, 5> along{0, .10 + .19 * random(1), .36 + .25 * random(2),
                                          .73 + .17 * random(3), 1};
        const std::array<double, 5> envelope{0, .28 + .68 * random(4), .72 + .28 * random(5),
                                             .20 + .72 * random(6), 0};
        const double left = .55 + .90 * random(7), right = .55 + .90 * random(8);
        grid(4, 4, [&](int r, int c) {
            const double across = (c - 2) * .5, shape = envelope[r];
            const double t = along[r];
            const Vec3 lateral =
                cross(path(std::min(1., t + .01)) - path(std::max(0., t - .01)), {0, 1, 0}).normalized();
            const double rowWidth = .55 + .70 * random(10 + r);
            const double bend = .24 * shape * std::sin(seed + r * 2.1);
            return path(t) +
                   lateral * (width * shape * (across * rowWidth * (across < 0 ? left : right) + bend)) +
                   Vec3{0,
                        -.018 + height * shape * std::sqrt(std::max(0., 1 - across * across)) *
                                    (1 + .25 * across + .12 * std::sin(seed + r * 1.7)),
                        0};
        });
    }
    void cap(Vec3 a, Vec3 b, double width, double height, double seed) {
        if ((b - a).length() >= .15)
            capOnPath([&](double t) { return a + (b - a) * t; }, width, height, seed);
    }
    // Irregular distances along a whole rail, independent of its support bays.
    // Short edges may stay bare; long gaps alternate with small and large caps.
    void caps(const std::vector<Vec3> &path, double width, double height, double seed,
              double spacing = 1.55) {
        if (path.size() < 2)
            return;
        std::vector<double> distance(path.size(), 0);
        for (size_t i = 1; i < path.size(); ++i)
            distance[i] = distance[i - 1] + (path[i] - path[i - 1]).length();
        const double length = distance.back(), scale = spacing / 1.55;
        const auto point = [&](double d) {
            const auto end = std::upper_bound(distance.begin(), distance.end(), d);
            const size_t i = std::min(path.size() - 1, size_t(end - distance.begin()));
            const size_t a = i ? i - 1 : 0;
            const double t = distance[i] > distance[a] ? (d - distance[a]) / (distance[i] - distance[a]) : 0;
            return path[a] + (path[i] - path[a]) * t;
        };
        double cursor = (.25 + 3.4 * variation(seed + 5.13)) * scale;
        for (int i = 0; cursor < length - .18; ++i) {
            const double s = seed + i * 31.731;
            const double end =
                std::min(length - .04, cursor + (.25 + 4.4 * std::pow(variation(s + 1.7), .9)) * scale);
            if (end - cursor >= .15)
                capOnPath([&](double t) { return point(cursor + (end - cursor) * t); },
                          width * (.45 + 1.8 * std::pow(variation(s + 8.2), 1.1)),
                          height * (.60 + 1.2 * variation(s + 14.8)), s);
            cursor = end + (.70 + 4.5 * std::pow(variation(s + 22.4), 1.1)) * scale;
        }
    }
    void caps(Vec3 a, Vec3 b, double width, double height, double seed, double spacing = 1.55) {
        caps(std::vector<Vec3>{a, b}, width, height, seed, spacing);
    }
    // a/b follow the wall's maximum permitted snow height, not its ground.
    template <class Ground>
    void bank(Vec3 a, Vec3 b, Vec3 outward, double width, double height, double seed, Ground ground) {
        const int count = std::max(3, int(std::ceil((b - a).length() / 1.35)));
        constexpr std::array<double, 4> across{0, .25, .62, 1}, profile{1, .87, .32, 0};
        grid(count, 3, [&](int r, int c) {
            const double t = r / double(count), envelope = std::sin(PI * t);
            const double uneven = .85 + .15 * std::sin(seed + t * 8) + .10 * std::sin(t * 19 + seed * .7);
            const Vec3 wall = a + (b - a) * t;
            Vec3 p = wall + outward * (width * across[c] * (.65 + .35 * envelope) * uneven);
            const double base = ground(p.x, p.z);
            const double rise =
                std::min(height * uneven, std::max(0., wall.y - ground(wall.x, wall.z) - .025));
            p.y = base - .025 + rise * envelope * profile[c];
            return p;
        });
    }
    // Taller stand banks use the actual stepped floor as their ceiling. Deep
    // troughs alternate with broad high drifts; wider feet keep the slopes soft.
    template <class Ground, class Ceiling>
    void standBank(Vec3 a, Vec3 b, Vec3 outward, double seed, Ground ground, Ceiling ceiling) {
        const double length = (b - a).length();
        const int count = std::max(6, int(std::ceil(length / .60)));
        constexpr std::array<double, 5> across{0, .20, .45, .75, 1}, profile{1, .93, .58, .14, 0};
        grid(count, 4, [&](int r, int c) {
            const double t = r / double(count);
            const Vec3 wall = a + (b - a) * t;
            const double coordinate = wall.x * .43 + wall.z * .37;
            const double noise = clamp((smoothVariation(coordinate, seed) - .15) / .70, 0., 1.);
            const double fraction = .12 + .88 * std::pow(noise, .7);
            const double top = ceiling(wall.x, wall.z);
            const double available = std::max(0., top - ground(wall.x, wall.z));
            const double maximum = std::min(2.7, available);
            const double edge = clamp(std::min(t, 1 - t) * length / (.5 + .55 * maximum), 0., 1.);
            const double taper = edge * edge * (3 - 2 * edge);
            const double rise = maximum * fraction * taper;
            const double width = .75 + 1.05 * rise + .35 * smoothVariation(coordinate * 1.7, seed + 47);
            Vec3 p = wall + outward * (width * across[c]);
            const double base = ground(p.x, p.z) - .025;
            p.y = std::min(base + rise * profile[c], std::max(base, top));
            return p;
        });
    }
    // Foundation snow rests on the ground under a bridge, even when its skirt
    // crosses the ski corridor. CameraSurface also includes the raised deck.
    void footing(const Hill &hill, double x, double z, double halfX, double halfZ, double top, double seed) {
        footing(x, z, halfX, halfZ, top, seed,
                [&](double xx, double zz) { return terrainSnowHeight(hill, xx, zz); });
    }
    template <class Ground>
    void footing(double x, double z, double halfX, double halfZ, double top, double seed, Ground ground) {
        const double width = .45 + .25 * variation(seed);
        constexpr std::array<double, 5> across{0, .2, .45, .75, 1}, profile{1, .92, .58, .15, 0};
        grid(
            16, 4,
            [&](int r, int c) {
                const double angle = (r % 16) * 2 * PI / 16;
                const Vec3 direction{std::cos(angle), 0, std::sin(angle)};
                const double edge = std::min(halfX / std::max(1e-8, std::abs(direction.x)),
                                             halfZ / std::max(1e-8, std::abs(direction.z)));
                const Vec3 wall = Vec3{x, 0, z} + direction * edge;
                const double uneven =
                    .88 + .12 * std::sin(angle * 3 + seed) + .09 * std::sin(angle * 5 - seed);
                Vec3 p = wall + direction * (width * across[c] * uneven);
                const double rise = std::min(.38 * uneven, std::max(0., top - ground(wall.x, wall.z) - .025));
                p.y = ground(p.x, p.z) - .025 + rise * profile[c];
                return p;
            },
            true);
    }
};
} // namespace sjp::visual
