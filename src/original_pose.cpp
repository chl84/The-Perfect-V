#include "original_pose.h"
#include <cmath>

namespace sjp {
namespace {
// Table source DS[0x0c14 + mode*176], EXE SHA256
// 4565152f18f3de25cae3fe220a2d19ce96744ba70b9b5c4eeb5ea5d338e65e30.
// Each record: four offsets, eighteen angles, eighteen angular rates,
// four offset rates. Values are original float32 promoted without alteration.
constexpr std::array<std::array<double, 44>, 10> MODES = {{
#include "original_pose_data.inc"
}};
constexpr double PI_ORIGINAL = 3.14159265358979323846;

void moveToward(float &actual, float target, float rate, double dt) {
    if (actual == target)
        return;
    double amount = double(rate) * dt;
    if (std::abs(amount) > std::abs(double(target) - actual))
        actual = target;
    else
        actual = float(double(actual) + (target > actual ? amount : -amount));
}
} // namespace

void OriginalPose::reset(double motionAngle) {
    motionAngle = float(motionAngle);
    angles.fill(0);
    angleTargets.fill(0);
    angleRates.fill(0);
    offsets.fill(0);
    offsetTargets.fill(0);
    offsetRates.fill(0);
    auxiliary.fill(0);
    footTrims.fill(0);
    leadingLeg = 0;
    bodyFactor = .5f;
    modeTime = 0;
    mode = stage = 1;
    previousMotionAngle = motionAngle;
    selectMode(1, motionAngle);
}

void OriginalPose::selectMode(int originalMode, double motionAngle) {
    if (originalMode < 1 || originalMode > int(MODES.size()))
        return;
    mode = originalMode;
    // Source table setter 0x2ea25 changes pose fields without resetting the
    // timer. Its callers reset it explicitly for selected transitions.
    const auto &record = MODES[size_t(mode - 1)];
    for (size_t i = 0; i < offsets.size(); ++i) {
        float value = float(record[i] * (i % 2 == 0 ? 1.08 : .74));
        float rate = float(record[40 + i]);
        if (rate > 0) {
            offsetTargets[i] = value;
            offsetRates[i] = rate;
        } else {
            offsets[i] = value;
            offsetRates[i] = 0;
        }
    }
    for (size_t i = 0; i < angles.size(); ++i) {
        float value = float(record[4 + i] == -99 ? motionAngle : record[4 + i]);
        float rate = float(record[22 + i]);
        if (rate > 0) {
            angleTargets[i] = value;
            angleRates[i] = rate;
        } else if (rate == 0) {
            angles[i] = value;
            angleRates[i] = 0;
        }
        // A negative table rate leaves the existing current/target/rate intact.
    }
}

void OriginalPose::flightTargets(double q) {
    q = float(q);
    angleTargets[0] = angleTargets[9] = float(.42 - q / 3);
    angleRates[0] = angleRates[9] = 2;
    angleTargets[2] = angleTargets[11] = float(.64 + q);
    angleRates[2] = angleRates[11] = 10;
    angleTargets[4] = float(.257 + .8 * q);
    angleTargets[5] = float(.8 + q);
    angleTargets[6] = float(.15 + .5 * q + auxiliary[0]);
    angleTargets[15] = float(.15 + .5 * q + auxiliary[1]);
    angleTargets[7] = angleTargets[16] = float(.3 + .5 * q);
    for (size_t i : {size_t(4), size_t(5), size_t(6), size_t(7), size_t(15), size_t(16)})
        angleRates[i] = 10;
}

void OriginalPose::landingTargets(double width, double motionAngle) {
    size_t front = leadingLeg == 1 ? 11 : 2, back = leadingLeg == 1 ? 2 : 11;
    angleTargets[front] = float(1.5 + .8 * width + 1.6 * motionAngle * width);
    angleTargets[back] = float(1.5 - .7 * width + 1.6 * motionAngle * width);
    angleRates[2] = angleRates[11] = 4;
    angleTargets[4] = float(1.4 - .3 * width + .3 * motionAngle);
    angleRates[4] = 4;
}

void OriginalPose::airborneFootTargets(int originalStage, double q, double motionAngle, double blend) {
    // Later control block 0x27fd2..281bf overrides the mode-table foot targets.
    for (size_t leg = 0; leg < 2; ++leg) {
        size_t pitch = 1 + 9 * leg;
        double sideCosine = std::cos(double(angles[pitch - 1]));
        angleTargets[pitch] = float(originalStage < 8
            ? motionAngle * blend + sideCosine * (q / 4 + footTrims[leg] + .2) * (1 - blend)
            : sideCosine * motionAngle);
        angleRates[pitch] = angleTargets[pitch] > angles[pitch] ? 5 : float(.25 + 10 * blend);
    }
}

void OriginalPose::groundedFootTargets(double motionAngle) {
    // Source 0x2e033..0x2e059 prepares the shared slope-following joint
    // before the two foot targets; the near-surface height solver reads it.
    angleTargets[8] = float(motionAngle / 3 + .2);
    angleRates[8] = 1;
    for (size_t i : {size_t(1), size_t(10)}) {
        int turns = int(std::trunc(double(angles[i]) / (2 * PI_ORIGINAL)));
        angleTargets[i] = float(PI_ORIGINAL * (turns % 2) + std::cos(double(angles[i - 1])) * motionAngle);
        angleRates[i] = 3;
    }
}

void OriginalPose::advance(double dt) {
    dt = float(dt);
    modeTime = float(double(modeTime) + dt);
    for (size_t i = 0; i < offsets.size(); ++i)
        moveToward(offsets[i], offsetTargets[i], offsetRates[i], dt);
    for (size_t i = 0; i < angles.size(); ++i)
        moveToward(angles[i], angleTargets[i], angleRates[i], dt);
}

void OriginalPose::update(int originalStage, double q, double width, double dt) {
    update(originalStage, q, width, dt, previousMotionAngle, originalStage >= 6 && originalStage <= 9);
}

void OriginalPose::update(int originalStage, double q, double width, double dt,
                          double motionAngle, bool airborne, double flightBlend) {
    prepare(originalStage, q, width, motionAngle, airborne, flightBlend);
    advance(dt);
}

void OriginalPose::prepare(int originalStage, double q, double width,
                          double motionAngle, bool airborne, double flightBlend,
                          bool refreshLandingTargets, bool landingClick) {
    q = float(q);
    width = float(width);
    motionAngle = float(motionAngle);
    flightBlend = float(flightBlend);
    bool changed = originalStage != stage;
    // Source 0x26cd1 selects mode6 only after the landing-click branch.
    // An unprepared point contact can also enter stage9 at0x26d8e.
    bool landingEntered = stage < 8 && (originalStage == 8 || landingClick);
    previousMotionAngle = motionAngle;
    // Ready/start and mode2->3 transitions precede the grounded callback.
    // Takeoff/flight/landing transitions follow it (265e8..266e9).
    if (changed && originalStage == 2) {
        selectMode(2, motionAngle);
        modeTime = 0;
    }
    if ((originalStage == 2 || originalStage == 3) && mode == 2 && modeTime >= .2) {
        selectMode(3, motionAngle);
        modeTime = 0;
    }
    if (!airborne && originalStage >= 2 && originalStage <= 12)
        groundedFootTargets(motionAngle);
    if (changed) {
        if (originalStage == 4) {
            selectMode(4, motionAngle);
            modeTime = 0;
            bodyFactor = 1;
        } else if (originalStage == 6 || originalStage == 7) {
            if (mode != 5) {
                selectMode(5, motionAngle);
                modeTime = 0;
            }
        }
        if (landingEntered) {
            selectMode(6, motionAngle);
            angleTargets[0] = angleTargets[9] = 0;
            angleRates[0] = angleRates[9] = 2;
        }
    }
    if (originalStage == 6 || originalStage == 7)
        flightTargets(q);
    // Main 0x26d5f skips the landing callback at the contact event, while
    // grounded foot targets and an immediately preceding mode change remain.
    // The second button freezes stage8 after the callback; a width cap
    // sets9 before it but still falls through the same callback (26d66).
    const bool landingFrozen = originalStage == 9 && stage == 8;
    if (refreshLandingTargets && (originalStage == 8 || landingEntered || landingFrozen))
        landingTargets(width, motionAngle);
    if (airborne && originalStage >= 6 && originalStage <= 11)
        airborneFootTargets(originalStage, q, motionAngle, flightBlend);
    stage = originalStage;
}
} // namespace sjp
