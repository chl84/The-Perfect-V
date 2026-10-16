#pragma once
#include "physics.h"
#include <algorithm>
#include <cmath>

namespace sjp::visual {
// CPU-only terrain height evaluators, shared by tessellation, cameras and lights.
inline double arenaGround(const Hill &h, double x, double z) {
    const double edge = std::max(0.0, std::abs(z) - 19 * h.scale());
    return h.ground(x) - edge * .08 + std::sin(x * .017 + z * .013) * edge * .018;
}
inline double backdropGround(const Hill &hill, double x, double z) {
    const double original = arenaGround(hill, x, z), bottom = hill.outrunBottom();
    double height = hill.ground(x), surface = original;
    const double tailStart = std::max(bottom + 47, 190 * hill.scale());
    if (x > tailStart) {
        // Beyond the prepared track and all arena fixtures, retain the endpoint
        // and its tangent, then
        // roll over into a descending continuation instead of an infinite wall.
        const double dx = x - tailStart, run = 8 * hill.scale();
        const double extension = hill.ground(tailStart) +
                                 (hill.slope(tailStart) * run * dx - .24 * dx * dx) / (dx + run);
        surface -= height - extension;
        height = extension;
    }
    const double core = std::max(50 * hill.scale(), 24.0);
    if (x <= bottom)
        return surface;
    // Only the decorative flanks beyond the outrun bottom stop following the
    // quadratic's unbounded uphill extrapolation. Keep a wide original strip
    // around the ski surface, stands and masts, and blend continuously across it.
    const double t = clamp((std::abs(z) - core) / (60 * hill.scale()), 0.0, 1.0);
    const double weight = t * t * (3 - 2 * t);
    if (weight == 0)
        return surface;
    const double dx = x - bottom;
    const double drop = .24 * dx * dx / (dx + 30 * hill.scale());
    return surface - weight * (height - hill.ground(bottom) + drop);
}
} // namespace sjp::visual
