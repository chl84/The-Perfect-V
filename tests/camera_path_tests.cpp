#include "camera_path.h"
#include "presentation_timing.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

// Independent plane interpolation of the existing renderer's receiver mesh.
double snow(const sjp::Hill &hill, double x, double z) {
    const double s = hill.scale(), dx = 3 * s, dz = 10 * s;
    const double start = std::min(-170 * s, hill.inrunStart() - 60 * s);
    const double x0 = start + std::floor((x - start) / dx) * dx;
    const double z0 = -200 * s + std::floor((z + 200 * s) / dz) * dz;
    auto node = [&](double xx, double zz) {
        const double edge = std::max(0.0, std::abs(zz) - 19 * s);
        return sjp::Vec3{xx, hill.ground(xx) - edge * .08 +
                                std::sin(xx * .017 + zz * .013) * edge * .018, zz};
    };
    const auto a = node(x0, z0), c = node(x0 + dx, z0 + dz);
    const auto b = (z - z0) * dx >= (x - x0) * dz ? node(x0, z0 + dz) : node(x0 + dx, z0);
    const auto normal = sjp::cross(b - a, c - a);
    double height = a.y - (normal.x * (x - a.x) + normal.z * (z - a.z)) / normal.y;
    if (x >= hill.inrunStart() && x <= 0 && std::abs(z) <= 2.3) {
        const double step = 2 * s;
        const double left = hill.inrunStart() + std::floor((x - hill.inrunStart()) / step) * step;
        const double right = std::min(0.0, left + step);
        const double t = right > left ? (x - left) / (right - left) : 0;
        height = std::max(height, hill.inrun(left) + (hill.inrun(right) - hill.inrun(left)) * t);
    }
    return height;
}
} // namespace

int main() {
    try {
        size_t frames = 0;
        double minimumEye = 1e9, minimumSight = 1e9;
        for (int hi = 0; hi < int(sjp::HILL_COUNT); ++hi) {
            const auto &hill = sjp::hills()[hi];
            sjp::visual::ChaseCameraPath path;
            path.build(hill);
            for (double x = hill.start(); x < hill.end() + 40; x += .25) {
                const auto eye = path.eye(x);
                require(eye.y >= snow(hill, eye.x, eye.z) + 1.75,
                        "A finished camera rail enters the snow surface");
                // Continuous velocity across the spline knots, including the table.
                constexpr double delta = 1e-4;
                const auto left = (eye - path.eye(x - delta)) * (1 / delta);
                const auto right = (path.eye(x + delta) - eye) * (1 / delta);
                require((left - right).length() < .005, "Camera rail has a velocity discontinuity");
                require(eye.y - snow(hill, x, 0) < 45,
                        "Camera rail rises excessively above the jumper's corridor");
            }
            for (bool healthy : {false, true})
                for (double wind : {-5.0, 0.0, 5.0})
                    for (const auto &cadence : {std::vector<double>{1. / 30}, {1. / 144},
                                               {.004, .013, .007, .021, .005, .08}}) {
                        sjp::Simulation sim(hi, wind);
                        sim.resetOriginal(hi, 12345, 98765);
                        sim.setWind(std::abs(wind), wind < 0 ? 0 : sjp::PI);
                        double progress = sim.x, accumulator = 0;
                        for (size_t frame = 0; frame < 10000 && sim.phase != sjp::Phase::Finished; ++frame) {
                            const double dt = cadence[frame % cadence.size()];
                            accumulator += dt;
                            while (accumulator >= sjp::ORIGINAL_TICK) {
                                sjp::Input in;
                                in.start = sim.phase == sjp::Phase::Ready;
                                sim.step(sjp::ORIGINAL_TICK, healthy ? sjp::autopilot(sim) : in);
                                accumulator -= sjp::ORIGINAL_TICK;
                            }
                            const auto f = sim.renderFrame(accumulator);
                            progress += (f.x - progress) * sjp::visual::cameraBlend(dt);
                            const auto eye = path.eye(progress);
                            const sjp::Vec3 focus{f.x, f.y + 1.1, 0};
                            const double clearance = eye.y - snow(hill, eye.x, eye.z);
                            minimumEye = std::min(minimumEye, clearance);
                            require(clearance >= 1.75, "Following the rail lost camera clearance");
                            for (int i = 0; i <= 32; ++i) {
                                const auto p = eye + (focus - eye) * (i / 32.0);
                                const double visible = p.y - snow(hill, p.x, p.z);
                                minimumSight = std::min(minimumSight, visible);
                                if (visible < .2) {
                                    std::cerr << hill.id << " x=" << f.x << " wind=" << wind
                                              << " healthy=" << healthy << " clearance=" << visible << '\n';
                                    require(false, "Finished rail loses sight of the jumper");
                                }
                            }
                            ++frames;
                        }
                    }
        }
        std::cout << "32 camera rails; frames=" << frames << " minimum_eye=" << minimumEye
                  << " minimum_sight=" << minimumSight << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
