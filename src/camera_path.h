#pragma once
#include "physics.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sjp::visual {
// Read the existing snow mesh; this never changes hill or collision geometry.
class CameraSurface {
  public:
    const Hill &hill;
    double start, step, zStep;

    explicit CameraSurface(const Hill &h)
        : hill(h), start(std::min(-170 * h.scale(), h.inrunStart() - 60 * h.scale())),
          step(3 * h.scale()), zStep(10 * h.scale()) {}

    double height(double x, double z) const {
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
    auto nextGrid = [&](double origin, double delta, double start, double step, double t) {
        if (std::abs(delta) < 1e-10)
            return 1.0;
        const double cell = std::floor((origin + delta * (t + 1e-8) - start) / step);
        return std::min(1.0, (start + (cell + (delta > 0 ? 1 : 0)) * step - origin) / delta);
    };
    // The surface and ray are linear inside each receiver triangle. Checking
    // grid edges and diagonals catches narrow ridges without a dense ray march.
    for (double t = 0; t < 1 - 1e-8;) {
        double next = std::min({nextGrid(eye.x, ray.x, surface.start, surface.step, t),
                                nextGrid(eye.z, ray.z, 0, surface.zStep, t),
                                nextGrid(eye.x, ray.x, surface.hill.inrunStart(), 2 * surface.hill.scale(), t)});
        auto boundary = [&](double origin, double delta, double edge) {
            if (std::abs(delta) > 1e-10) {
                const double cut = (edge - origin) / delta;
                if (cut > t + 1e-8 && cut < next)
                    next = cut;
            }
        };
        boundary(eye.z, ray.z, -2.3);
        boundary(eye.z, ray.z, 2.3);
        boundary(eye.x, ray.x, surface.hill.inrunStart());
        boundary(eye.x, ray.x, 0);
        measure(t);
        measure(next);
        const Vec3 mid = eye + ray * ((t + next) * .5);
        const double x0 = surface.start + std::floor((mid.x - surface.start) / surface.step) * surface.step;
        const double z0 = std::floor(mid.z / surface.zStep) * surface.zStep;
        const double slope = ray.z / surface.zStep - ray.x / surface.step;
        if (std::abs(slope) > 1e-10) {
            const double diagonal = -((eye.z - z0) / surface.zStep - (eye.x - x0) / surface.step) / slope;
            if (diagonal > t && diagonal < next)
                measure(diagonal);
        }
        if (next <= t)
            break;
        t = next;
    }
    return lift;
}

// A hill-specific rail, built once and followed by distance along the hill.
// Smoothing progress rather than world-space positions keeps the camera on
// the curve, including through the steep landing-to-outrun transition.
class ChaseCameraPath {
  public:
    void build(const Hill &hill) {
        const CameraSurface surface(hill);
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
