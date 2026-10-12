#include "original_skeleton.h"
#include "physics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char *message) {
    require(std::abs(actual - expected) <= tolerance, message);
}
void sampleCadence(const std::vector<double> &cadence, const char *label) {
    Simulation simulation(0, 0);
    simulation.setWind(0, 0);
    simulation.phase = Phase::Flight;
    simulation.originalStage = 7;
    simulation.x = 10;
    simulation.y = 100;
    simulation.vx = 30;
    simulation.pitch = -.35f;
    Input input;
    input.buttonMask = 0;
    double accumulator = 0, elapsed = 0;
    float lastRaw = 0, lastSmooth = 0;
    int samples = 0, repeatedRaw = 0, repeatedSmooth = 0;
    for (int i = 0; elapsed < .8; ++i) {
        double dt = cadence[size_t(i) % cadence.size()];
        elapsed += dt;
        accumulator += dt;
        while (accumulator >= ORIGINAL_TICK) {
            simulation.step(ORIGINAL_TICK, input);
            accumulator -= ORIGINAL_TICK;
        }
        auto raw = simulation.frame();
        auto smooth = simulation.renderFrame(accumulator);
        if (elapsed > .1) {
            near(smooth.elapsed, elapsed - ORIGINAL_TICK, 2e-6,
                 "Display time must advance continuously between physical ticks");
            if (samples) {
                repeatedRaw += raw.x == lastRaw;
                repeatedSmooth += smooth.x == lastSmooth;
                require(smooth.x > lastSmooth, "A moving skier must advance on every rendered frame");
                require(smooth.x - lastSmooth < 1, "Render interpolation must not create position jumps");
            }
            ++samples;
        }
        lastRaw = raw.x;
        lastSmooth = smooth.x;
    }
    require(repeatedSmooth == 0, "Interpolated skier should never hold a stale moving frame");
    if (cadence.size() == 1 && cadence[0] < ORIGINAL_TICK)
        require(repeatedRaw > 0, "High-refresh test must reproduce the original unsmoothed stutter");
    std::cout << label << ": " << samples << " samples, repeated raw=" << repeatedRaw
              << ", interpolated=" << repeatedSmooth << '\n';
}
} // namespace
int main() {
    try {
        Frame previous, current;
        previous.x = 10;
        current.x = 14;
        previous.y = 4;
        current.y = 2;
        previous.vx = 20;
        current.vx = 24;
        previous.elapsed = 1;
        current.elapsed = 2;
        previous.leadingLeg = 0;
        current.leadingLeg = 1;
        previous.phase = int(Phase::Flight);
        current.phase = int(Phase::Outrun);
        previous.windAngle = float(2 * PI - .1);
        current.windAngle = .1f;
        for (size_t i = 0; i < previous.pose.size(); ++i) {
            previous.pose[i] = float(i);
            current.pose[i] = float(i + 2);
        }
        auto midpoint = interpolateFrame(previous, current, .5);
        near(midpoint.x, 12, 0, "Midpoint location");
        near(midpoint.y, 3, 0, "Midpoint height");
        near(midpoint.vx, 22, 0, "Midpoint speed");
        near(midpoint.elapsed, 1.5, 0, "Interpolated display timestamp");
        for (size_t i = 0; i < previous.pose.size(); ++i)
            near(midpoint.pose[i], i + 1, 0, "Every original pose component is interpolated");
        near(midpoint.windAngle, 2 * PI, 1e-6, "Wind wraps through zero without reversing direction");
        require(midpoint.phase == previous.phase && midpoint.leadingLeg == previous.leadingLeg,
                "Discrete animation state changes only at its recorded boundary");
        require(interpolateFrame(previous, current, 1).leadingLeg == 1 &&
                    interpolateFrame(previous, current, -1).x == previous.x,
                "Interpolation clamps to exact endpoints");
        OriginalPose pose;
        pose.reset(0);
        std::copy(pose.angles.begin(), pose.angles.end(), previous.pose.begin());
        std::copy(pose.offsets.begin(), pose.offsets.end(), previous.pose.begin() + 18);
        current.pose = previous.pose;
        current.pose[18] = .7f;
        current.pose[20] = .85f;
        auto jointsBefore = originalSkeleton(previous.pose, previous.leadingLeg);
        auto jointsAfter = originalSkeleton(current.pose, current.leadingLeg);
        midpoint = interpolateFrame(previous, current, .5);
        require(midpoint.hasRenderSkeleton, "Animation uses interpolated original joint positions");
        for (size_t i = 0; i < midpoint.renderSkeleton.size(); ++i) {
            auto expected = (jointsBefore.points[i] + jointsAfter.points[i]) * .5;
            near((midpoint.renderSkeleton[i] - expected).length(), 0, 1e-12,
                 "All 21 joints move continuously between original table samples");
        }
        Simulation simulation(0, 0);
        require(simulation.renderFrame().x == simulation.frame().x, "Reset has no stale render history");
        Input input;
        input.start = true;
        for (int i = 0; i < 100; ++i)
            simulation.step(ORIGINAL_TICK, input);
        auto before = simulation.frame();
        simulation.step(ORIGINAL_TICK, {});
        auto after = simulation.frame();
        simulation.step(ORIGINAL_TICK * .25, {});
        auto displayed = simulation.renderFrame();
        near(displayed.x, before.x + (double(after.x) - before.x) * .25, 2e-6,
             "Internal fractional steps use the original 70 Hz snapshots");
        for (int i = 0; i < 100; ++i)
            simulation.renderFrame(ORIGINAL_TICK * i / 100);
        require(simulation.frame().x == after.x && simulation.originalCounter == 102,
                "Rendering must not advance physics or alter its authoritative position");
        sampleCadence({1. / 60}, "60 Hz");
        sampleCadence({1. / 120}, "120 Hz");
        sampleCadence({1. / 144}, "144 Hz");
        sampleCadence({.004, .013, .007, .021, .005}, "Variable frame time");
        std::cout << "Render interpolation, animation boundaries and unchanged physics passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
