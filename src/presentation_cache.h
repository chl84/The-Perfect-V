#pragma once
#include "physics.h"
#include <tuple>

namespace sjp::visual {
// Compare values, never struct padding. All procedural clothing, contact and
// shadow inputs are included, including temporary interpolated skeletons.
inline bool samePresentationFrame(const Frame &a, const Frame &b) {
    if (std::tie(a.x, a.y, a.vx, a.vy, a.pitch, a.elapsed, a.phase, a.landing, a.crashed, a.pose,
                 a.telemarkWidth, a.windSpeed, a.windAngle, a.leadingLeg, a.hasRenderSkeleton) !=
        std::tie(b.x, b.y, b.vx, b.vy, b.pitch, b.elapsed, b.phase, b.landing, b.crashed, b.pose,
                 b.telemarkWidth, b.windSpeed, b.windAngle, b.leadingLeg, b.hasRenderSkeleton))
        return false;
    if (a.hasRenderSkeleton)
        for (size_t i = 0; i < a.renderSkeleton.size(); ++i) {
            const auto &x = a.renderSkeleton[i], &y = b.renderSkeleton[i];
            if (x.x != y.x || x.y != y.y || x.z != y.z)
                return false;
        }
    return true;
}
} // namespace sjp::visual
