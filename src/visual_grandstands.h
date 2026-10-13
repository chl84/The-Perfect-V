#pragma once
#include "camera_path.h"
#include <array>
#include <vector>

namespace sjp::visual {
struct StandTerrace {
    double x0 = 0, x1 = 0, base = 0;
    double floor(int row) const { return base + row * .70; }
    double woodTop(int row) const { return floor(row) + .13; }
};
struct StandSection {
    double center = 0, width = 0;
    std::vector<StandTerrace> terraces;

    const StandTerrace &at(double x) const {
        const double position = (x - terraces.front().x0) / width;
        const int index = clamp(int(std::floor(position * terraces.size())), 0, int(terraces.size()) - 1);
        return terraces[size_t(index)];
    }
};
struct GrandstandLayout {
    static constexpr int sectionCount = 14, rowCount = 3;
    static double center(const Hill &h, int index) { return (79 + index * 6.2) * h.scale(); }
    static double sectionWidth(const Hill &h) { return 5.8 * h.scale(); }
    static double nearDistance(const Hill &h) { return std::max(25 * h.scale(), 8.0); }
    double near = 0;
    std::array<StandSection, sectionCount * 2> sections;

    const StandSection &section(int side, int index) const {
        return sections[size_t((side > 0 ? sectionCount : 0) + index)];
    }
    void build(const Hill &h) {
        const CameraSurface snow(h);
        near = nearDistance(h);
        for (int side : {-1, 1})
            for (int index = 0; index < sectionCount; ++index) {
                auto &stand = sections[size_t((side > 0 ? sectionCount : 0) + index)];
                stand.center = center(h, index);
                stand.width = sectionWidth(h);
                stand.terraces.clear();
                const double first = stand.center - stand.width * .5, last = stand.center + stand.width * .5;
                const double front = side * (near + 1.35), back = side * (near + 4.95);
                double drop = 0;
                for (double z : {front, back})
                    drop = std::max(drop, std::abs(snow.height(last, z) - snow.height(first, z)));
                // Shorter horizontal landings on steep ground, with enough
                // width for both feet. All heights read the unchanged snow.
                const int limit = std::max(1, int(std::floor(stand.width / .95)));
                const int count = clamp(int(std::ceil(std::max(stand.width / 3.2, drop / .70))), 1, limit);
                stand.terraces.reserve(size_t(count));
                for (int bay = 0; bay < count; ++bay) {
                    const double x0 = first + stand.width * bay / count;
                    const double x1 = bay + 1 == count ? last : first + stand.width * (bay + 1) / count;
                    double ground = snow.height(x0, front);
                    for (double x : {x0, (x0 + x1) * .5, x1})
                        for (double z : {front, (front + back) * .5, back})
                            ground = std::max(ground, snow.height(x, z));
                    stand.terraces.push_back({x0, x1, ground + .65});
                }
            }
    }
};
} // namespace sjp::visual
