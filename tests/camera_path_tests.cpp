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
    double height = 0;
    sjp::visual::snowCellTriangles(hill, x0, z0,
        [&](double xx, double zz) { return sjp::visual::arenaGround(hill, xx, zz); },
        [&](sjp::Vec3 a, sjp::Vec3 b, sjp::Vec3 c) {
            if (!sjp::visual::snowTriangleContains(a, b, c, x, z))
                return;
            const auto normal = sjp::cross(b - a, c - a);
            height = a.y - (normal.x * (x - a.x) + normal.z * (z - a.z)) / normal.y;
        });
    if (x >= hill.inrunStart() && x <= 0 && std::abs(z) <= 2.3) {
        const double step = sjp::visual::inrunSnowStep(hill);
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
        // Frozen before corridor refinement: preserve the established camera-2
        // composition while all live snow receivers gain the finer topology.
        struct RailSample { int hill; double x, y; };
        const std::array<RailSample, 15> established{{
            {2, -4.1491499999999997, 17.739504956914992},
            {2, 112.3515625, -51.705818095646435},
            {2, 283.08373301182951, -110.08630618619104},
            {3, -4.1491499999999997, 18.767489076376879},
            {3, 163.3289794921875, -113.33630890133477},
            {3, 233.62986333056421, -176.25111563504115},
            {11, -4.1491499999999997, 13.675814081938791},
            {11, 45.872047424316406, -4.3145461343807794},
            {11, 220.00164688873846, -27.225987399385676},
            {12, -4.1491499999999997, 11.138376638144324},
            {12, 101.12261962890625, -46.32786608442126},
            {12, 196.8273174360458, -88.135173829152691},
            {31, -4.1491499999999997, 20.272020684089306},
            {31, 203.14724731445312, -111.12440931460515},
            {31, 257.977621533042, -149.4831756140247},
        }};
        for (const auto &sample : established) {
            sjp::visual::ChaseCameraPath path;
            path.build(sjp::hills()[size_t(sample.hill)]);
            require(std::abs(path.eye(sample.x).y - sample.y) < .0001,
                    "Snow refinement changed the established camera-2 rail");
        }
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
