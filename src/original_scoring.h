#pragma once
#include "original_pose.h"
#include <array>
#include <cstdint>

namespace sjp {
// Borland Random's recovered 32-bit state and bounded integer output.
struct OriginalRandom {
    uint32_t state;
    explicit OriginalRandom(uint32_t seed = 1) : state(seed) {}
    int next(int upperBound);
    float centeredAverage(int samples);
};

struct OriginalLandingState {
    double impactSpeed = 0, impactAngle = 0;
    double telemarkWidth = 0;
    double bodyAngle = 0, bodyTarget = 0;
    // Original code repeats the anatomical first leg's error twice, including
    // when the player chooses the other leg to lead the telemark.
    double leg1Angle = 0, leg1Target = 0;
    double sideAngle1 = 0, sideAngle2 = 0;
    double height1 = 1.08, height2 = 1.08;
    double takeoffDuration = .2;
    double flightErrorIntegral = 0, flightDuration = 0;
    bool beforeTakeoff = false;
    int leadingLeg = 0;
};

struct OriginalLandingAssessment {
    std::array<float, 5> penalties{};
    float mean = 0;
    int crashKind = 0;
    int roughness = 0;
    std::array<float, 2> perturbations{};
    // Impact arm targets consume two Random(5)+1 draws before any judge.
    std::array<int, 2> armFactors{};
    float normalizedFlightError = 0;
    std::array<double, 5> judges{};
    double stylePoints = 0;
    bool crashed() const { return crashKind != 0; }
};

OriginalLandingState originalLandingStateFromPose(const OriginalPose &pose);
OriginalLandingAssessment classifyOriginalLanding(const OriginalLandingState &state);
OriginalLandingAssessment assessOriginalLanding(const OriginalLandingState &state, OriginalRandom &random,
                                                bool includeJudges = true);
} // namespace sjp
