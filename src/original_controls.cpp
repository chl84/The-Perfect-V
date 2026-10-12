#include "original_controls.h"
#include <algorithm>
#include <cmath>

namespace sjp {
namespace {
double square(double value) { return value * value; }
}

void OriginalFlightStyle::accumulate(int originalStage, bool flightFlag,
                                    double q, const OriginalPose &pose, double dt) {
    if (!flightFlag)
        return;
    const double tick = float(dt);
    const double pitchCost = square(double(float(q)) + .35) * tick * 1.5;
    const double trimCost = (square(double(pose.footTrims[0])) +
                             square(double(pose.footTrims[1]))) * .3 * tick;
    const double auxiliaryCost = (square(double(pose.auxiliary[0])) +
                                  square(double(pose.auxiliary[1]))) * .3 * tick;
    // 0x26911..0x26986 retains the accumulator on the x87 stack while adding
    // the three terms, then performs one DWORD store.
    if (originalStage < 8) {
        integral = float(double(integral) + pitchCost + trimCost + auxiliaryCost);
    } else {
        // 0x26990..0x26a64: grace is not clamped at .4; it freezes at the
        // first float32 tick at or above .4. Error continues accumulating.
        if (double(landingTimer) > .1)
            integral = float(double(integral) +
                             (double(landingTimer) * 3 - .1) *
                             (pitchCost + trimCost + auxiliaryCost));
        if (double(landingTimer) < .4)
            landingTimer = float(double(landingTimer) + tick);
    }
    duration = float(double(duration) + tick);
}

double originalMousePitchDelta(int mouseDy, double dt) {
    return -double(std::clamp(mouseDy, -128, 127)) * double(float(dt)) * 1.5;
}

void updateOriginalFlightInput(int originalStage, double &q, double pitchDelta,
                               double dt, double windSpeed, double windAngle,
                               OriginalRandom &random, OriginalPose &pose) {
    if (originalStage < 6 || originalStage > 7)
        return;
    const double tick = float(dt);
    q = float(double(float(q)) + pitchDelta);
    // 0x26aaf..0x26b22: each shared decision damps BOTH fields of that pair.
    if (random.next(5) == 1)
        for (float &trim : pose.footTrims)
            trim = float(double(trim) * tick * 65);
    if (random.next(5) == 1)
        for (float &auxiliary : pose.auxiliary)
            auxiliary = float(double(auxiliary) * tick * 65);
    if (random.next(30) == 1)
        q = float(q + (7 - std::cos(double(float(windAngle))) *
                         double(float(windSpeed))) * tick / 10);
    // Left/right decisions are separate, with the conditional magnitude
    // draws inserted immediately. Reordering draws changes wind and judges.
    for (float &trim : pose.footTrims)
        if (random.next(60) == 1)
            trim = float(double(trim) + random.next(5) * tick);
    for (float &auxiliary : pose.auxiliary)
        if (random.next(100) == 1)
            auxiliary = float(double(auxiliary) + random.next(10) * tick);
    q = float(std::clamp(q, -.6, .6));
}
} // namespace sjp
