#pragma once
#include "visual_snow_mesh.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sjp::visual {
// Read the existing snow mesh; this never changes hill or collision geometry.
class CameraSurface {
  public:
    const Hill &hill;
    double start, step, zStep;

    bool conservativeRail;

    explicit CameraSurface(const Hill &h, bool rail = false)
        : hill(h), start(std::min(-170 * h.scale(), h.inrunStart() - 60 * h.scale())),
          step(3 * h.scale()), zStep(10 * h.scale()), conservativeRail(rail) {}

    double height(double x, double z) const {
        if (conservativeRail)
            return coarseRailHeight(x, z);
        double y = terrainSnowHeight(hill, x, z);
        if (x >= hill.inrunStart() && x <= 0 && std::abs(z) <= 2.3)
            y = std::max(y, inrunSnowHeight(hill, x));
        return y;
    }

  private:
    // Keep the established camera-2 rail stable. Its conservative coarse
    // envelope already has a metre of spare clearance; the refined surface
    // remains the receiver for shadows, markers and live camera protection.
    double coarseRailHeight(double x, double z) const {
        const double x0 = start + std::floor((x - start) / step) * step;
        const double z0 = std::floor(z / zStep) * zStep;
        const double u = (x - x0) / step, v = (z - z0) / zStep;
        auto node = [&](double xx, double zz) {
            const double edge = std::max(0.0, std::abs(zz) - 19 * hill.scale());
            return hill.ground(xx) - edge * .08 + std::sin(xx * .017 + zz * .013) * edge * .018;
        };
        const double a = node(x0, z0), b = node(x0, z0 + zStep),
                     c = node(x0 + step, z0 + zStep), d = node(x0 + step, z0);
        double y = v >= u ? a * (1 - v) + b * (v - u) + c * u
                          : a * (1 - u) + c * v + d * (u - v);
        if (x >= hill.inrunStart() && x <= 0 && std::abs(z) <= 2.3) {
            const double rampStep = 2 * hill.scale();
            const double a = hill.inrunStart() + std::floor((x - hill.inrunStart()) / rampStep) * rampStep;
            const double b = std::min(0.0, a + rampStep);
            const double t = b > a ? (x - a) / (b - a) : 0;
            y = std::max(y, hill.inrun(a) * (1 - t) + hill.inrun(b) * t);
        }
        return y;
    }
};

inline double cameraRequiredLift(const CameraSurface &surface, Vec3 eye, Vec3 focus) {
    constexpr double clearance = 1.75, sightClearance = .2;
    double lift = std::max(0.0, surface.height(eye.x, eye.z) + clearance - eye.y);
    const Vec3 ray = focus - eye;
    auto measure = [&](double t) {
        if (t >= 1 - 1e-8)
            return;
        const Vec3 p = eye + ray * t;
        lift = std::max(lift, (surface.height(p.x, p.z) + sightClearance - p.y) / (1 - t));
    };
    // Read the exact cached receiver topology, including its fixed fine belt
    // and stitched sides. Camera protection never changes simulation contact.
    measure(0);
    measure(1);
    snowEdgeCrossings(surface.hill, eye, focus, measure);
    auto boundary = [&](double origin, double delta, double edge) {
        if (std::abs(delta) > 1e-10) {
            const double t = (edge - origin) / delta;
            if (t > 0 && t < 1)
                measure(t);
        }
    };
    boundary(eye.z, ray.z, -2.3);
    boundary(eye.z, ray.z, 2.3);
    boundary(eye.x, ray.x, surface.hill.inrunStart());
    boundary(eye.x, ray.x, 0);
    if (std::abs(ray.x) > 1e-10) {
        const double low = std::max(std::min(eye.x, focus.x), surface.hill.inrunStart());
        const double high = std::min(std::max(eye.x, focus.x), 0.0);
        const double step = inrunSnowStep(surface.hill);
        for (double x = surface.hill.inrunStart() +
                        std::ceil((low - surface.hill.inrunStart()) / step) * step; x < high; x += step)
            boundary(eye.x, ray.x, x);
    }
    return lift;
}

// A hill-specific rail, built once and followed by distance along the hill.
// Smoothing progress rather than world-space positions keeps the camera on
// the curve, including through the steep landing-to-outrun transition.
class ChaseCameraPath {
  public:
    void build(const Hill &hill) {
        const CameraSurface surface(hill, true);
        start = hill.start() - 5;
        constexpr double spacing = 1;
        const size_t count = size_t(std::ceil((hill.end() + 100 - start) / spacing)) + 1;
        std::vector<double> required(count);
        for (size_t i = 0; i < count; ++i) {
            const double x = start + i * spacing;
            Vec3 eye{x - 13, surface.height(x, 0) + 7, 9};
            double lift = 0;
            // Cover the tracking delay at normal jump speeds in advance.
            // Airborne focus points are higher and need no extra clearance.
            for (int ahead = 0; ahead <= 4; ++ahead) {
                const double xx = x + ahead;
                double y = surface.height(xx, 0);
                // A jumper crossing the table starts near its tangent,
                // above the landing surface below the takeoff edge.
                if (xx > 0 && xx < 8)
                    y = std::max(y, hill.inrun(0) + hill.inrunSlope(0) * xx - .03 * xx * xx);
                Vec3 focus{xx, y + .7, 0};
                lift = std::max(lift, cameraRequiredLift(surface, eye, focus));
            }
            required[i] = eye.y + lift;
        }
        heights.resize(count);
        double lift = 0;
        for (size_t i = 0; i < count; ++i) {
            double sum = 0, weight = 0;
            for (int j = -12; j <= 12; ++j) {
                const size_t at = size_t(std::clamp(int(i) + j, 0, int(count) - 1));
                const double w = std::exp(-double(j * j) / 32);
                sum += required[at] * w;
                weight += w;
            }
            heights[i] = sum / weight;
            lift = std::max(lift, required[i] - heights[i]);
        }
        // Keep the entire smoothed rail above the required envelope, with
        // room for interpolation and the lagging focus during normal jumps.
        for (double &height : heights)
            height += lift + 1;
    }

    Vec3 eye(double progress) const {
        const double position = std::clamp(progress - start, 0.0, double(heights.size() - 1));
        const int i = std::min(int(position), int(heights.size()) - 2);
        const double t = position - i;
        auto y = [&](int at) { return heights[size_t(std::clamp(at, 0, int(heights.size()) - 1))]; };
        // Catmull-Rom gives continuous velocity through each metre of rail.
        const double a = y(i - 1), b = y(i), c = y(i + 1), d = y(i + 2);
        const double height = .5 * (2 * b + (c - a) * t +
                                   (2 * a - 5 * b + 4 * c - d) * t * t +
                                   (3 * b - a - 3 * c + d) * t * t * t);
        return {progress - 13, height, 9};
    }

  private:
    double start = 0;
    std::vector<double> heights;
};
} // namespace sjp::visual
