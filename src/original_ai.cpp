#include "original_ai.h"
#include <algorithm>
#include <cmath>

namespace sjp {
OriginalAiSkills generateOriginalAiSkills(int selectedLevel, OriginalRandom &random,
                                         int *doubleTeamRandomLevel) {
    // World Cup 0x153a5..0x155b8; Double Team Cup 0x140a4..0x142af.
    // Exact original float32 lookup at DS0x1844 (five entries).
    constexpr std::array<float, 5> spread = {4, 3, 2, 1, .2f};
    OriginalAiSkills skills;
    skills.level = selectedLevel;
    if (selectedLevel > 4) {
        skills.level = random.next(5);
        if (doubleTeamRandomLevel) *doubleTeamRandomLevel = skills.level;
    }
    // Valid original settings are 0..5. Keep invalid caller input bounded.
    skills.level = std::clamp(skills.level, 0, 4);
    const int noiseLevel = doubleTeamRandomLevel
        ? std::clamp(*doubleTeamRandomLevel, 0, 4) : skills.level;
    for (size_t index : {size_t(2), size_t(3)}) {
        const int sign = 2 * random.next(2) - 1;
        const double noise = random.centeredAverage(6);
        skills.values[index] = float(sign * double(spread[size_t(skills.level)]) * .3 +
                                     noise * (double(spread[size_t(noiseLevel)]) / 10 + .3));
    }
    return skills;
}

void refreshOriginalAiHillForm(OriginalAiSkills &skills, OriginalRandom &random) {
    // World Cup 0x15796..0x157dc; Double Team Cup 0x1440f..0x14455.
    skills.values[0] = random.centeredAverage(6);
    skills.values[1] = random.centeredAverage(6);
}

void OriginalAi::initialize(const OriginalAiSkills &skills, int hillK, OriginalRandom &random) {
    // Main 0x25180..0x25540. The four inputs retain the original float32 record
    // order. Each field below corresponds to an original DWORD store.
    assessImpactOnce = true;
    const double levelGap = 5 - skills.level;
    leftTakeoffTime = float(double(skills.values[2]) / 12 * .8 + .18 +
                            double(skills.values[0]) / 20 * .8 + random.centeredAverage(6) / 25.0);
    leftTakeoffTime = float(double(leftTakeoffTime) + (250 - hillK) / 10000.0);
    if (double(leftTakeoffTime) < .108)
        leftTakeoffTime = float((.65 - random.next(15) / 100.0) * .18);
    if (double(leftTakeoffTime) > .252)
        leftTakeoffTime = float((random.next(15) / 100.0 + 1.35) * .18);
    rightTakeoffTime = float(levelGap / 70 * double(random.centeredAverage(4)) * .5 +
                             double(leftTakeoffTime) - .01);
    if (rightTakeoffTime < leftTakeoffTime)
        rightTakeoffTime = leftTakeoffTime;
    pitchDelay = float(random.next(10) / 100.0 + .05);
    pitchDuration = float(random.next(30) / 100.0 + .5);
    pitchRemaining = pitchDuration;
    targetPitch = float((double(skills.values[3]) / 12 * 1.3 +
                         double(skills.values[1]) / 10 + random.centeredAverage(6) / 15.0) / 1.7 - .38);
    double lead = double(random.centeredAverage(3)) * .3 + .6;
    lead += levelGap * std::abs(double(random.centeredAverage(3)) + .7) * .15;
    landingLead = float(lead);
    const double widthNoise = random.centeredAverage(3);
    const double extraWidthNoise = random.centeredAverage(2);
    landingWidth = float(widthNoise * .4 + .35 + extraWidthNoise * .1);
    landingWidth = float(std::clamp(double(landingWidth), 0.0, .42));
    unusualLanding = random.next(40) == 1;
    if (unusualLanding) {
        if (random.next(2) == 1) {
            landingWidth = float(double(random.centeredAverage(3)) * .2 + .7);
            landingLead = float(double(landingLead) + .7);
        } else {
            landingLead = float(double(random.centeredAverage(2)) * .3);
        }
    }
}

OriginalAiInput OriginalAi::poll(const OriginalAiObservation &observation,
                                const OriginalRandom &globalRandom, double dt) {
    // Main 0x25579 clears all synthetic mouse fields on EVERY poll.
    OriginalAiInput input;
    OriginalRandom random = globalRandom;
    const double tick = float(dt);
    const double q = float(observation.q);
    const double vx = float(observation.vx), vyDown = float(observation.vyDown);
    if (observation.tick == 70)
        input.buttonMask |= 1;
    if (observation.stage == 3 || observation.stage == 4) {
        const float remainingTime = float(observation.distanceToTakeoff /
                                           std::cos(double(float(observation.motionAngle))) /
                                           double(float(observation.speed)));
        if (remainingTime < leftTakeoffTime) input.buttonMask |= 1;
        if (remainingTime < rightTakeoffTime) input.buttonMask |= 2;
    }
    if (observation.stage == 6 || observation.stage == 7) {
        if (pitchDelay > 0) {
            pitchDelay = float(double(pitchDelay) - tick);
        } else {
            pitchDelay = 0;
            pitchRemaining = float(double(pitchRemaining) - tick);
            const double elapsedFraction = (double(pitchDuration) - pitchRemaining) / pitchDuration;
            if (elapsedFraction > std::abs(q / targetPitch) || pitchRemaining < 0) {
                // Both draws occur, and the second test may overwrite dy.
                if (q - .05 - random.next(30) / 100.0 > targetPitch) input.mouseDy = 1;
                if (q + .05 + random.next(30) / 100.0 < targetPitch) input.mouseDy = -1;
            }
        }
    }
    if (observation.stage >= 6 && observation.stage <= 8) {
        // 0x25738..0x25784 retains BOTH original local DWORD stores rather
        // than replacing the expression with its algebraic simplification.
        float contactTime = float(observation.clearance / (vyDown / vx + observation.landingSlope) * vyDown / vx);
        contactTime = float(double(contactTime) / vyDown);
        // Original unordered FCOMP also takes this fallback (0x25786).
        if (!(contactTime >= 0)) contactTime = 10;
        if (vyDown > 0) {
            if (contactTime < landingLead) input.buttonMask |= 2;
            if ((input.buttonMask & 2) && assessImpactOnce && !unusualLanding) {
                float difference = vx == 0
                    ? float(3.14159265358979323846 - double(float(observation.motionAngle)))
                    : float(std::abs(std::atan(-vyDown / vx) - float(observation.terrainAngleAhead)));
                const float speed = float(std::sqrt(vx * vx + vyDown * vyDown));
                const float impact = float(std::sqrt(std::abs(std::sin(double(difference)) * double(speed))));
                if (impact > 2)
                    landingWidth = float(double(landingWidth) - (double(impact) - 2) * 1.75);
                assessImpactOnce = false;
            }
            if (landingWidth < float(observation.telemarkWidth)) input.buttonMask |= 1;
        }
        if (observation.clearance < 1.3 - int(unusualLanding)) input.buttonMask |= 2;
        if (observation.clearance > 3 + int(unusualLanding)) input.buttonMask = 0;
    }
    return input;
}
} // namespace sjp
