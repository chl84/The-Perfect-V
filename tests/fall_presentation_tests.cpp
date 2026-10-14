#include "renderer.h"
#include "original_skeleton.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
int drawCalls = 0;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void unchangedFields(const sjp::Frame &a, const sjp::Frame &b) {
    require(a.x == b.x && a.vx == b.vx && a.vy == b.vy && a.pitch == b.pitch && a.elapsed == b.elapsed &&
                a.phase == b.phase && a.landing == b.landing && a.crashed == b.crashed && a.pose == b.pose &&
                a.telemarkWidth == b.telemarkWidth && a.windSpeed == b.windSpeed &&
                a.windAngle == b.windAngle && a.leadingLeg == b.leadingLeg &&
                a.hasRenderSkeleton == b.hasRenderSkeleton,
            "Presentation altered an input, pose or simulation field");
    for (size_t i = 0; i < a.renderSkeleton.size(); ++i)
        require(a.renderSkeleton[i].x == b.renderSkeleton[i].x &&
                    a.renderSkeleton[i].y == b.renderSkeleton[i].y &&
                    a.renderSkeleton[i].z == b.renderSkeleton[i].z,
                "Presentation deformed the interpolated skeleton");
}
sjp::Frame check(const sjp::Hill &h, const sjp::Frame &f) {
    std::array<unsigned char, sizeof(f)> bytes{};
    std::memcpy(bytes.data(), &f, sizeof(f));
    const auto result = sjp::displayedAthleteFrame(h, f);
    require(std::memcmp(bytes.data(), &f, sizeof(f)) == 0, "Presentation mutated its source Frame");
    require(drawCalls == 0, "Contact calculation issued drawing commands");
    unchangedFields(f, result);
    require(std::isfinite(result.y) && result.y >= f.y, "Invalid presentation lift");
    const auto repeat = sjp::displayedAthleteFrame(h, f);
    require(result.y == repeat.y, "Paused/repeated presentation is not deterministic");
    const auto settled = sjp::displayedAthleteFrame(h, result);
    require(std::abs(double(settled.y) - result.y) < .0001, "Fall still needs contact correction");
    return result;
}
void skiEndContact() {
    const auto &hill = sjp::hills()[0];
    sjp::Frame frame = sjp::Simulation(0, 0).frame();
    frame.x = float(hill.outrunBottom());
    frame.y = float(sjp::visual::CameraSurface(hill).height(frame.x, 0));
    frame.phase = int(sjp::Phase::Finished);
    frame.crashed = 1;
    frame.hasRenderSkeleton = true;
    frame.renderSkeleton = sjp::originalSkeleton(frame.pose, frame.leadingLeg).points;
    for (auto &point : frame.renderSkeleton)
        point.y += 2;
    using J = sjp::OriginalJoint;
    auto point = [&](sjp::Frame &f, J joint) -> sjp::Vec3 & { return f.renderSkeleton[size_t(joint)]; };
    // Isolate shallow ski-end penetration with the body/bindings clear. The
    // unchanged ski mesh should enter the snow instead of lifting the body.
    for (int leg = 0; leg < 2; ++leg) {
        const double z = leg == 0 ? .35 : -.35;
        point(frame, leg == 0 ? J::SkiRear1 : J::SkiRear2) = {-1, .20, z};
        point(frame, leg == 0 ? J::SkiFront1 : J::SkiFront2) = {1.628, -.085, z};
        point(frame, leg == 0 ? J::SkiTip1 : J::SkiTip2) = {1.648, -.02, z};
    }
    require(check(hill, frame).y == frame.y, "Shallow ski ends still lift the whole jumper");
    auto deep = frame;
    point(deep, J::SkiFront1).y -= .35;
    point(deep, J::SkiTip1).y -= .35;
    const auto corrected = check(hill, deep);
    require(corrected.y > deep.y + .25 && corrected.y < deep.y + .40,
            "Ski-end snow allowance is absent or allows excessive burial");
    auto headContact = frame;
    point(headContact, J::Head).y = -.35;
    require(check(hill, headContact).y > headContact.y + .35,
            "Ski allowance also lets the helmet enter the snow");
    auto bootContact = frame;
    point(bootContact, J::Ankle1).y = -.20;
    require(check(hill, bootContact).y > bootContact.y + .15,
            "Ski allowance also lets boots/bindings enter the snow");
    frame.crashed = 0;
    require(check(hill, frame).y == frame.y, "Ski allowance affects non-falling jumps");
}
void inrunFalls() {
    size_t contactSamples = 0;
    using J = sjp::OriginalJoint;
    for (int hill = 0; hill < int(sjp::HILL_COUNT); ++hill) {
        sjp::Simulation sim(hill, 0), control(hill, 0);
        sim.resetOriginal(hill, 12345, 98765);
        control.resetOriginal(hill, 12345, 98765);
        const sjp::visual::CameraSurface snow(sim.hill());
        bool inrunFall = false;
        auto previous = sim.frame();
        for (int tick = 0; tick < 14000 && sim.phase != sjp::Phase::Finished; ++tick) {
            sjp::Input input;
            input.start = sim.phase == sjp::Phase::Ready;
            input.takeoff = sim.originalStage == 3 && sim.x > -std::max(1.0, sim.vx) * 3;
            sim.step(sjp::ORIGINAL_TICK, input);
            control.step(sjp::ORIGINAL_TICK, input);
            const auto frame = sim.frame(), untouched = control.frame();
            unchangedFields(frame, untouched);
            require(frame.y == untouched.y && sim.originalStage == control.originalStage &&
                        sim.result.total == control.result.total &&
                        sim.frames.size() == control.frames.size(),
                    "Inrun presentation changed physics, RNG, scoring or replay trajectory");
            if (frame.crashed && frame.x < 0)
                inrunFall = true;
            if (frame.crashed && (frame.x < 5 || tick % 47 == 0)) {
                for (const auto &sample : {frame, sjp::interpolateFrame(previous, frame, .5)}) {
                    const auto displayed = check(sim.hill(), sample);
                    const auto skeleton = sample.hasRenderSkeleton
                                              ? sjp::OriginalSkeleton{sample.renderSkeleton}
                                              : sjp::originalSkeleton(sample.pose, sample.leadingLeg);
                    for (J joint : {J::Head, J::ShoulderCenter, J::HipCenter, J::Knee1, J::Knee2, J::Elbow1,
                                    J::Elbow2, J::Hand1, J::Hand2}) {
                        const auto point = skeleton[joint] + sjp::Vec3{sample.x, displayed.y, 0};
                        if (point.x >= sim.hill().inrunStart() && point.x <= 0 && std::abs(point.z) < 2.3) {
                            require(point.y >= snow.height(point.x, point.z) - .0001,
                                    "Fallen body/head remains below the inrun snow");
                            ++contactSamples;
                        }
                    }
                    auto airborne = sample;
                    airborne.y += 100;
                    require(check(sim.hill(), airborne).y == airborne.y, "Airborne inrun fall was moved");
                }
            }
            previous = frame;
        }
        require(inrunFall && sim.phase == sjp::Phase::Finished, "Missing complete original inrun fall");
        // The same pose follows a continuous outgoing tangent on either side
        // of x=0. Contact uses any parts still over the actual takeoff deck.
        auto before = sim.frame(), after = before;
        before.x = -.0001f;
        before.y = float(sim.hill().inrun(before.x));
        after.x = .0001f;
        after.y = float(sim.hill().inrun(0) + sim.hill().inrunSlope(0) * after.x);
        require(std::abs(check(sim.hill(), before).y - check(sim.hill(), after).y) < .005,
                "Visual fall pops at the inrun-to-flight boundary");
    }
    require(contactSamples > 1000, "Inrun body/head contact was not exercised on all hills");
    std::cout << "Original inrun falls on all 32 hills: " << contactSamples
              << " body/head contact samples; unchanged physics/wind/RNG, continuous board crossing\n";
}
} // namespace
// The CPU contact calculation must work without a window or graphics context.
namespace sjp::gfx {
void begin(Enum) { ++drawCalls; }
void end() { ++drawCalls; }
void normal3d(double, double, double) { ++drawCalls; }
void vertex3d(double, double, double) { ++drawCalls; }
void color4f(float, float, float, float) { ++drawCalls; }
void pushAttrib(Flags) { ++drawCalls; }
void popAttrib() { ++drawCalls; }
void disable(Enum) { ++drawCalls; }
void materialfv(Enum, Enum, const float *) { ++drawCalls; }
void materialf(Enum, Enum, float) { ++drawCalls; }
void pushMatrix() { ++drawCalls; }
void popMatrix() { ++drawCalls; }
void translated(double, double, double) { ++drawCalls; }
} // namespace sjp::gfx

int main() {
    try {
        skiEndContact();
        inrunFalls();
        size_t samples = 0;
        double maximumLift = 0;
        for (int hill = 0; hill < int(sjp::HILL_COUNT); ++hill) {
            sjp::Simulation sim(hill, 0);
            sim.setWind(0, 0);
            const auto ready = sim.frame();
            require(check(sim.hill(), ready).y == ready.y, "Start position changed");
            auto previous = ready;
            for (int tick = 0; tick < 14000 && sim.phase != sjp::Phase::Finished; ++tick) {
                sjp::Input input;
                input.start = sim.phase == sjp::Phase::Ready;
                input.buttonMask = 0;
                sim.step(sjp::ORIGINAL_TICK, input);
                auto frame = sim.frame();
                if (tick % 47 == 0 || sim.phase == sjp::Phase::Finished) {
                    const auto interpolated = sjp::interpolateFrame(previous, frame, .5);
                    for (const auto &f : {frame, interpolated}) {
                        const auto display = check(sim.hill(), f);
                        maximumLift = std::max(maximumLift, double(display.y) - f.y);
                        if (!f.crashed || f.phase == int(sjp::Phase::Inrun) ||
                            f.phase == int(sjp::Phase::Flight))
                            require(display.y == f.y, "Standing or flying jumper moved");
                        auto airborne = f;
                        airborne.y += 100;
                        require(check(sim.hill(), airborne).y == airborne.y,
                                "Airborne fall moved despite no snow penetration");
                        ++samples;
                    }
                }
                previous = frame;
            }
            require(sim.phase == sjp::Phase::Finished && sim.crashed, "Missing complete original fall");
            auto finished = sim.frame();
            const auto savedY = check(sim.hill(), finished).y;
            check(sjp::hills()[(hill + 1) % sjp::HILL_COUNT], ready);
            require(check(sim.hill(), finished).y == savedY, "Replay seek/hill switch changed contact");
        }
        require(maximumLift > .5 && maximumLift < 5, "Deeply embedded fall poses were not covered");
        std::cout << "Passed all32 hills, " << samples
                  << " physical/interpolated samples; unchanged source and poses, zero GPU commands, max lift="
                  << maximumLift << " m\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
