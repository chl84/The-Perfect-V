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
