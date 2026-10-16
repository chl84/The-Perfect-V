#include "visual_snow_mesh.h"
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
using namespace sjp;
using Point = std::array<long long, 2>;
using Edge = std::array<Point, 2>;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Point key(Vec3 p) {
    return {std::llround(p.x * 1e7), std::llround(p.z * 1e7)};
}
void stitchedPatch(const Hill &h, double center) {
    const double dx = 3 * h.scale(), dz = 10 * h.scale();
    const double left = visual::snowCellStart(center, visual::snowGridStart(h), dx) - dx;
    const double right = left + 3 * dx, front = -30 * h.scale(), back = 30 * h.scale();
    std::map<Edge, size_t> edges;
    double area = 0;
    for (int ix = 0; ix < 3; ++ix)
        for (int iz = -3; iz < 3; ++iz)
            visual::snowCellTriangles(
                h, left + ix * dx, iz * dz,
                [&](double x, double z) { return visual::backdropGround(h, x, z); },
                [&](Vec3 a, Vec3 b, Vec3 c) {
                    const auto normal = cross(b - a, c - a);
                    require(normal.y > 0, "Snow has a reversed or degenerate triangle");
                    area += normal.y * .5;
                    for (auto p : {a, b, c})
                        require(std::abs(p.y - visual::backdropGround(h, p.x, p.z)) < 1e-9,
                                "Snow vertex moved off the original curve");
                    for (auto p : {(a + b + c) * (1. / 3), a * .7 + b * .2 + c * .1})
                        require(std::abs(p.y - visual::terrainSnowHeight(h, p.x, p.z)) < 1e-6,
                                "Receiver height does not match a rendered triangle");
                    for (auto pair : {std::array<Vec3, 2>{a, b}, {b, c}, {c, a}}) {
                        Edge edge{key(pair[0]), key(pair[1])};
                        if (edge[1] < edge[0])
                            std::swap(edge[0], edge[1]);
                        ++edges[edge];
                    }
                });
    require(std::abs(area - (right - left) * (back - front)) < 1e-7,
            "Refined snow loses coverage or overlaps itself");
    const auto low = key({left, 0, front}), high = key({right, 0, back});
    for (const auto &[edge, count] : edges) {
        const bool boundary = (edge[0][0] == edge[1][0] && (edge[0][0] == low[0] || edge[0][0] == high[0])) ||
                              (edge[0][1] == edge[1][1] && (edge[0][1] == low[1] || edge[0][1] == high[1]));
        require(count == (boundary ? 1u : 2u), "Fine/coarse snow join has a crack or T-junction");
    }
}
void edgeBends(const Hill &h) {
    const double x = h.kMarkerX, s = h.scale();
    // Cross both transition fans and the central fine strip obliquely.
    const Vec3 a{x - 6 * s, 0, -25 * s}, b{x + 7 * s, 0, 25 * s};
    std::vector<double> cuts{0, 1};
    visual::snowEdgeCrossings(h, a, b, [&](double t) { cuts.push_back(t); });
    std::sort(cuts.begin(), cuts.end());
    for (size_t i = 1; i < cuts.size(); ++i) {
        const auto p = a + (b - a) * cuts[i - 1], q = a + (b - a) * cuts[i];
        const double low = visual::terrainSnowHeight(h, p.x, p.z);
        const double high = visual::terrainSnowHeight(h, q.x, q.z);
        for (int j = 1; j < 8; ++j) {
            const double t = j / 8.0;
            const auto r = p + (q - p) * t;
            require(std::abs(visual::terrainSnowHeight(h, r.x, r.z) - (low * (1 - t) + high * t)) < 1e-6,
                    "Ray/contact traversal misses a terrain bend");
        }
    }
}
} // namespace
int main() {
    try {
        double maximumError = 0;
        for (const auto &h : hills()) {
            for (double x : {0.0, h.kMarkerX, h.end() + 8 * h.scale()})
                stitchedPatch(h, x);
            edgeBends(h);
            const double step = 3 * h.scale(), start = visual::snowGridStart(h);
            const double end = std::min(h.outrunBottom() + 40, h.end() + 6 * h.scale());
            for (double x = start; x + step <= end; x += step) {
                if (x < 0)
                    continue;
                size_t core = 0, outer = 0, transition = 0;
                auto height = [&](double xx, double zz) { return visual::backdropGround(h, xx, zz); };
                visual::snowCellTriangles(h, x, 0, height, [&](Vec3, Vec3, Vec3) { ++core; });
                visual::snowCellTriangles(h, x, 10 * h.scale(), height,
                                          [&](Vec3, Vec3, Vec3) { ++transition; });
                visual::snowCellTriangles(h, x, 20 * h.scale(), height, [&](Vec3, Vec3, Vec3) { ++outer; });
                require(core == 8 && transition == 5 && outer == 2,
                        "Fixed belt tessellation exceeds its geometry budget");
                for (int i = 0; i < 64; ++i) {
                    const double xx = x + step * i / 64;
                    maximumError =
                        std::max(maximumError, std::abs(visual::terrainSnowHeight(h, xx, 0) - h.ground(xx)));
                }
            }
        }
        require(maximumError < .0086, "Ski corridor is not four times finer on every hill");
        std::cout << "32 hills: stitched snow, unchanged profile nodes, exact receivers, bounded geometry; "
                     "max curve error "
                  << maximumError * 1000 << " mm\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
