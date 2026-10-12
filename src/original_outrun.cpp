#include "original_outrun.h"
#include <algorithm>
#include <cmath>

namespace sjp {
namespace {
constexpr double PI = 3.141592653589793238462643383279502884;
int sourceRound(double value) { return int(std::nearbyint(value)); }
int parity(float angle) { return sourceRound(double(angle) / PI) % 2; }
double sourceFloat(double value) { return float(value); }
}

void originalSettleTargets(OriginalPose &p, double width, double terrainAngle) {
    width = sourceFloat(width);
    terrainAngle = sourceFloat(terrainAngle);
    p.angleTargets[6] = p.angleTargets[15] = float(.2 * terrainAngle + 3.5);
    p.angleRates[6] = p.angleRates[15] = 3;
    p.offsetTargets[1] = p.offsetTargets[3] = float(.518);
    p.offsetRates[1] = p.offsetRates[3] = 1;
    p.angleTargets[7] = p.angleTargets[16] = float(1.3);
    p.angleRates[7] = p.angleRates[16] = 3;
    p.offsetTargets[0] = p.offsetTargets[2] = float((.5 * width + .6) * 1.08);
    p.offsetRates[0] = p.offsetRates[2] = 2;
    size_t front = p.leadingLeg ? 11 : 2, rear = p.leadingLeg ? 2 : 11;
    p.angleTargets[front] = float(1.3 * width + 1.5 + terrainAngle * width * 1.6);
    p.angleRates[2] = 2;
    p.angleTargets[rear] = float(1.49 - 1.3 * width + terrainAngle * width * 1.3);
    p.angleRates[11] = 2;
    // 0x2e969..0x2e98b retains all three REAL80 constants and the two
    // intermediate terms until the final float32 torso target store.
    p.angleTargets[4] = float(1.2L - .6L * width + .3L * terrainAngle);
    p.angleRates[4] = 1;
    p.angleTargets[3] = p.angleTargets[12] = float(.18);
    p.angleRates[3] = p.angleRates[12] = float(1.3);
    p.angleTargets[0] = p.angleTargets[9] = 0;
    p.angleRates[0] = p.angleRates[9] = 1;
}

void originalInitializeImpact(OriginalPose &p, const OriginalLandingAssessment &assessment,
                              double width, double terrainAngle) {
    if (assessment.crashKind < 4)
        originalSettleTargets(p, width, terrainAngle);
    if (assessment.crashKind >= 1 && assessment.crashKind <= 2) {
        p.offsetTargets[0] = p.offsetTargets[2] = float(.432);
        p.offsetRates[0] = p.offsetRates[2] = 1;
    }
    const double a = assessment.perturbations[0], b = assessment.perturbations[1];
    for (size_t leg = 0; leg < 2; ++leg) {
        const double noise = assessment.perturbations[leg];
        p.angleTargets[leg * 9] = float(noise);
        p.angleRates[leg * 9] = float(5 * noise + .3);
        // Original DS5318+36*leg(1/2), i.e. target indices3/12.
        p.angleTargets[3 + leg * 9] = float(.24 + noise);
        p.angleRates[3 + leg * 9] = 3;
    }
    const double total = std::abs(a) + std::abs(b);
    p.angleTargets[4] = float(double(p.angleTargets[4]) - total);
    p.angleRates[4] = 3;
    for (size_t leg = 0; leg < 2; ++leg) {
        const size_t arm = 6 + leg * 9;
        p.angleTargets[arm] = float(double(p.angleTargets[arm]) + total * assessment.armFactors[leg]);
        p.angleRates[arm] = 5;
    }
    p.modeTime = 0;
}

void originalRandomizeFallPose(OriginalPose &p, double targetBase, double targetRange,
                               double rateBase, double rateRange, OriginalRandom &random) {
    targetBase = sourceFloat(targetBase); targetRange = sourceFloat(targetRange);
    rateBase = sourceFloat(rateBase); rateRange = sourceFloat(rateRange);
    const int targetBound = sourceRound(targetRange * 100), rateBound = sourceRound(rateRange * 100);
    for (size_t axis = 0; axis < 7; ++axis) {
        if (axis == 3)
            continue;
        for (size_t leg = 0; leg < 2; ++leg) {
            const size_t i = axis + leg * 9;
            double perturb = random.next(targetBound) / 100.0 + targetBase;
            if (axis == 0)
                perturb *= (random.next(2) * 2 - 1) * .1;
            p.angleTargets[i] = float(double(p.angleTargets[i]) * .5 + double(p.angles[i]) * .5 - perturb);
        }
        for (size_t leg = 0; leg < 2; ++leg)
            p.angleRates[axis + leg * 9] = float(random.next(rateBound) / 100.0 + rateBase);
    }
    p.angleTargets[4] = float((double(p.angleTargets[2]) + double(p.angleTargets[11])) * .5);
    p.angleRates[2] = p.angleRates[11] = p.angleRates[4];
    p.angleTargets[1] = float(double(p.angleTargets[2]) - 1.5);
    p.angleTargets[10] = float(double(p.angleTargets[11]) - 1.5);
    p.angleRates[1] = p.angleRates[10] = float(.4);
    p.angleTargets[5] = float((double(p.angleTargets[2]) + double(p.angleTargets[11])) * .5);
    p.angleRates[5] = p.angleRates[4];
    p.angleTargets[3] = float((random.next(4) + 8) / 10.0);
    p.angleTargets[12] = float((random.next(4) + 8) / 10.0);
    p.angleTargets[7] = float((random.next(4) + 9) / 10.0);
    p.angleTargets[16] = float((random.next(4) + 9) / 10.0);
    p.offsetTargets[0] = float((random.next(40) / 100.0 + .6) * 1.08);
    p.offsetTargets[2] = float((random.next(40) / 100.0 + .6) * 1.08);
    p.offsetTargets[1] = float((random.next(30) / 100.0 + .7) * .74);
    p.offsetTargets[3] = float((random.next(30) / 100.0 + .7) * .74);
    for (size_t i : {size_t(0), size_t(2), size_t(1), size_t(3)})
        p.offsetRates[i] = float((random.next(targetBound) / 100.0 + targetBase) * .1);
}

void originalNearSurfaceHeights(OriginalPose &p, int originalStage, double y,
                               double surfaceAhead, double terrainSlope) {
    if (originalStage < 6 || originalStage > 10 || float(surfaceAhead) <= float(y))
        return;
    const size_t front = p.leadingLeg ? 1 : 0, rear = 1 - front;
    const double slope = float(terrainSlope);
    const double frontAngle = float(double(p.angles[2 + front * 9]) + p.angles[8]);
    const double rearAngle = float(double(p.angles[2 + rear * 9]) + p.angles[8]);
    double frontCos = float(std::cos(double(p.angles[3 + front * 9])));
    double rearCos = float(std::cos(double(p.angles[3 + rear * 9])));
    if (frontCos == 0)
        frontCos = float(.001);
    if (rearCos == 0)
        rearCos = float(.001);
    const double frontProjection = std::sin(frontAngle) - std::cos(frontAngle) * slope;
    const double rearProjection = std::sin(rearAngle) - std::cos(rearAngle) * slope;
    float frontHeight = p.offsets[front * 2], rearHeight = p.offsets[rear * 2];
    if (rearProjection == 0) {
        rearHeight = float(1.08);
    } else {
        rearHeight = float(frontProjection * frontHeight / rearProjection * frontCos / rearCos);
        if (double(rearHeight) > 1.08 || double(rearHeight) < .216) {
            rearHeight = float(std::clamp(double(rearHeight), .216, 1.08));
            frontHeight = frontProjection == 0 ? float(1.08)
                : float(rearProjection * rearHeight / frontProjection * rearCos / frontCos);
            frontHeight = float(std::clamp(double(frontHeight), .216, 1.08));
        }
    }
    p.offsetTargets[front * 2] = frontHeight;
    p.offsetTargets[rear * 2] = rearHeight;
    p.offsetRates[0] = p.offsetRates[2] = float(1.5);
}

OriginalOutrunAction originalOutrunUpdate(OriginalPose &p, const OriginalOutrunInput &input,
                                         OriginalRandom &random) {
    OriginalOutrunAction action;
    action.originalStage = input.originalStage;
    action.fallBounceCount = uint8_t(input.fallBounceCount);
    action.impulseX = float(input.existingImpulseX);
    action.impulseYUp = float(input.existingImpulseYUp);
    const double speed = sourceFloat(input.speed), vx = sourceFloat(input.vx);
    const double vyDown = sourceFloat(input.vyDown), mass = sourceFloat(input.mass);
    const double impact = sourceFloat(input.impactAngle);
    const double terrainSlope = sourceFloat(input.terrainSlope), aheadSlope = sourceFloat(input.aheadSlope);
    // 0x27df8..0x27fd2. mode100 is a transient choice; stage12 latches that
    // this first fall trigger has happened even after selecting other modes.
    if (p.mode != 100 && input.crashKind && input.originalStage != 12 &&
        double(p.modeTime) > (1 - int16_t(input.roughness) / 900.0) * .8) {
        p.modeTime = 0;
        p.mode = 100;
        p.bodyFactor = float(.6);
        action.pitchChanged = true;
        p.angleRates[0] = p.angleRates[9] = 3;
        p.angleTargets[0] = float((random.next(60) / 100.0 + 1.8) * double(p.angleTargets[0]));
        p.angleTargets[9] = float((random.next(60) / 100.0 + 1.8) * double(p.angleTargets[9]));
        p.angleTargets[3] = float(.2 + double(p.angleTargets[0]));
        p.angleTargets[12] = float(.2 + double(p.angleTargets[9]));
        p.angleRates[3] = p.angleRates[12] = 3;
        float impulse = float(.4 * vyDown * mass * (std::cos(impact) + .5));
        impulse = std::clamp(impulse, 150.f, 300.f);
        action.impulseYUp = impulse;
        action.impulseX = float(-double(impulse) / 5);
        action.pendingLaunch = true;
        originalRandomizeFallPose(p, float(3.4), float(2.5), float(.3), float(2.2), random);
        action.originalStage = 12;
    }
    // 0x2853b..0x28659, normal standing settle/brake transitions.
    if (!input.crashKind && action.originalStage >= 9 && action.originalStage <= 11) {
        if (!input.airborne && p.mode != 7 && p.mode != 8 && p.mode != 10) {
            if (p.modeTime >= .5 && p.modeTime <= 2)
                originalSettleTargets(p, input.width, input.terrainAngle);
            if (p.modeTime > 2) {
                p.selectMode(7, input.terrainAngle);
                p.modeTime = 0;
            }
        }
        if (p.modeTime > .5 && terrainSlope > -.6 && (p.mode == 7 || p.mode == 10) &&
            aheadSlope >= terrainSlope) {
            // fcompp compares ahead slope with current slope: JBE would retain
            // the mode; original JB skips only when ahead<current.
            p.selectMode(8, input.terrainAngle);
            p.modeTime = 0;
        }
    }
    if (input.contactEvent) {
        // 0x28668..0x286a0: cosmetic sound frequency shares the physical RNG.
        if (!input.fastAdvance)
            action.impactSoundFrequency = random.next(3000) + 10000;
        p.mode = 100;
        p.modeTime = speed < 5 ? -100.f : float(-8 / (speed - 4.2) + random.next(10) / 30.0);
        p.angleTargets[0] = p.angles[0];
        p.angleTargets[9] = p.angles[9];
        originalRandomizeFallPose(p, float(.01 * speed), float(.2 * speed),
                                 float(2 * speed / 100), float(3 * speed / 100), random);
        const size_t front = p.leadingLeg ? 11 : 2, rear = p.leadingLeg ? 2 : 11;
        p.angleRates[front] = 4;
        p.angleRates[rear] = float(2.5);
        p.angleRates[4] = p.angleRates[14] = 2;
        p.angleTargets[4] = float(sourceRound((double(p.angles[4]) - (.5 * vx + random.next(7)) / 10) / PI) * PI);
        if (std::abs(double(p.angleTargets[4]) - double(p.angles[4])) > PI / 4) {
            float impulse = float(vyDown * mass * (std::cos(impact) + .5));
            if (impulse > 230)
                impulse = 230;
            if (.5 * vyDown * mass < impulse)
                impulse = float(.5 * vyDown * mass);
            action.impulseYUp = impulse;
            action.impulseX = float(-double(impulse) / 5);
            action.pendingLaunch = true;
            originalRandomizeFallPose(p, float(.6), float(1.1), float(.5), float(1.6), random);
            if (parity(p.angleTargets[4]) == 0) {
                p.angleRates[4] = float(random.next(3) + 4);
                p.angleRates[5] = float(random.next(3) + 4);
                p.angleRates[2] = float(random.next(3) + 3);
                p.angleRates[11] = float(random.next(3) + 3);
            } else {
                p.angleRates[4] = float(random.next(2) + 2);
                p.angleRates[5] = float(random.next(2) + 2);
                p.angleRates[2] = float(random.next(2) + 2);
                p.angleRates[11] = float(random.next(2) + 2);
                // Source changes only Iy here; Ix retains its earlier projection.
                action.impulseYUp = float(1.4 * double(action.impulseYUp));
            }
        }
        p.angleTargets[5] = float(random.next(15) / 10.0 + double(p.angleTargets[4]));
        for (size_t i : {front, rear}) {
            const double magnitude = random.next(5) / 20.0;
            const int sign = random.next(3) - 1;
            p.angleTargets[i] = float(magnitude * sign + double(p.angleTargets[4]));
        }
        if (speed > 8) {
            const double direction = parity(p.angleTargets[4]) == 0 ? -1 : 1;
            p.angleTargets[4] = float(double(p.angleTargets[4]) + direction * random.next(15) / 20.0);
            p.angleTargets[6] = float(sourceRound((double(p.angles[6]) - (random.next(10) + 7) / 10.0) / PI) * PI);
            p.angleTargets[15] = float(sourceRound((double(p.angles[15]) - (random.next(10) + 7) / 10.0) / PI) * PI);
            p.angleTargets[7] = float(random.next(5) / 10.0 + .8);
            p.angleTargets[16] = float(random.next(5) / 10.0 + .8);
            p.angleTargets[3] = float(random.next(5) / 10.0 + .5);
            p.angleTargets[12] = float(random.next(5) / 10.0 + .5);
        }
    }
    // 0x28c88..0x28dac: a fallen jumper can bounce again after resting
    // against the slope, independently of a new point-contact event.
    if (speed > 8 && action.originalStage == 12 && input.crashKind &&
        !input.airborne && p.modeTime > 0) {
        if (action.fallBounceCount < 3) {
            ++action.fallBounceCount;
            action.impulseYUp = float(.24 * vyDown * mass * (std::cos(impact) + .5));
        }
        if (action.impulseYUp > 20) {
            action.impulseYUp = std::clamp(action.impulseYUp, 100.f, 250.f);
            action.impulseX = float(-double(action.impulseYUp) / 5);
            action.pendingLaunch = true;
            originalRandomizeFallPose(p, 0, float(.9), float(.5), float(1.6), random);
        }
    }
    // 0x28dac..0x28dcc: this final joint follows the slope throughout a
    // fall, including frames without any contact or bounce.
    if (input.crashKind) {
        p.angleTargets[8] = float(input.terrainAngle);
        p.angleRates[8] = 1;
    }
    return action;
}

bool originalOutrunStopped(bool airborne, double speed, bool landingComplete) {
    return !airborne && sourceFloat(speed) <= .01 && landingComplete;
}
} // namespace sjp
