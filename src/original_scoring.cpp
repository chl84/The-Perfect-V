#include "original_scoring.h"
#include <algorithm>
#include <cmath>

namespace sjp {
namespace {
double asOriginalFloat(double value) { return float(value); }
int originalRound(double value) { return int(std::nearbyint(value)); }
}

int OriginalRandom::next(int upperBound) {
    // 0x33e35 updates low/high seed words with multiplier 0x08088405 and +1;
    // 0x33dd8 returns the high 32 bits of seed * uint16(range).
    state = state * 134775813u + 1u;
    return int((uint64_t(state) * uint16_t(upperBound)) >> 32);
}

float OriginalRandom::centeredAverage(int samples) {
    uint32_t total = 0;
    for (int i = 0; i < samples; ++i)
        total += uint32_t(next(65535));
    return float(double(total) / samples / 65534 * 2 - 1);
}

OriginalLandingState originalLandingStateFromPose(const OriginalPose &pose) {
    OriginalLandingState state;
    state.bodyAngle = pose.angles[4];
    state.bodyTarget = pose.angleTargets[4];
    state.leg1Angle = pose.angles[2];
    state.leg1Target = pose.angleTargets[2];
    state.sideAngle1 = pose.angles[0];
    state.sideAngle2 = pose.angles[9];
    state.height1 = pose.offsets[0];
    state.height2 = pose.offsets[2];
    state.leadingLeg = pose.leadingLeg;
    return state;
}

OriginalLandingAssessment classifyOriginalLanding(const OriginalLandingState &state) {
    OriginalLandingAssessment result;
    // Original contact event 0x26e64..0x26f42. Each component is stored as DWORD.
    result.penalties[0] = float(14 * std::sqrt(std::abs(std::sin(asOriginalFloat(state.impactAngle)) * asOriginalFloat(state.impactSpeed))));
    result.penalties[1] = float(92 * asOriginalFloat(state.telemarkWidth));
    result.penalties[2] = float(30 * std::abs(asOriginalFloat(state.bodyAngle) - asOriginalFloat(state.bodyTarget)) +
                                 40 * std::abs(asOriginalFloat(state.leg1Angle) - asOriginalFloat(state.leg1Target)));
    result.penalties[3] = float(140 * asOriginalFloat(state.sideAngle1) + 140 * asOriginalFloat(state.sideAngle2));
    result.penalties[4] = float(50 * (2.16 - asOriginalFloat(state.height1) - asOriginalFloat(state.height2)));
    double sum = 0;
    for (float penalty : result.penalties)
        sum += penalty;
    result.mean = float(sum / 5);
    if (state.beforeTakeoff) {
        result.crashKind = 2;
        result.mean = 40;
    }
    // 0x26f83..0x26fea: no early exit; the last excessive component wins.
    for (size_t i = 0; i < result.penalties.size(); ++i)
        if (double(result.penalties[i]) + result.mean > 60)
            result.crashKind = int(i + 1);
    return result;
}

OriginalLandingAssessment assessOriginalLanding(const OriginalLandingState &state, OriginalRandom &random, bool includeJudges) {
    OriginalLandingAssessment result = classifyOriginalLanding(state);
    int maximum = 0;
    for (float penalty : result.penalties)
        if (maximum < double(penalty) + result.mean)
            maximum = originalRound(double(penalty) + result.mean);
    int roughness = std::max(0, maximum - 42 - random.next(10));
    // Preserve the original signed 16-bit square before its upper cap.
    roughness = int16_t(uint16_t(uint32_t(roughness) * uint32_t(roughness)));
    result.roughness = std::min(700, roughness);
    if (result.crashKind == 1)
        result.roughness = 500;
    int front = state.leadingLeg == 1 ? 1 : 0, rear = 1 - front;
    for (int leg : {rear, front}) {
        double amount = result.roughness + random.next(result.roughness) / 2.0;
        amount *= 2 * random.next(2) - 1;
        result.perturbations[size_t(leg)] = float(amount / 1300);
    }
    // 0x2713f..0x27186: these crash types retain only positive perturbations.
    if (result.crashKind == 3 || result.crashKind == 4)
        for (float &amount : result.perturbations)
            amount = std::abs(amount);
    // 0x27301/0x27358 change arm targets before distance/judge calculation.
    for (int &factor : result.armFactors)
        factor = random.next(5) + 1;
    // 0x27446..0x274b0: contact before the board retains the five initial
    // marks and returns before the judge block. Its omitted draws change
    // every later gust and fall impulse, so preserve the shared RNG here.
    if (!includeJudges) {
        result.judges.fill(20);
        result.stylePoints = 0;
        return result;
    }
    // 0x27564..0x275f5 converts the accumulated flight imbalance into a capped
    // style deduction. Extra preparation time is part of flightDuration.
    float imbalance = float(asOriginalFloat(state.flightErrorIntegral) /
                            (asOriginalFloat(state.flightDuration) + .1) * 3.5 - .4);
    result.normalizedFlightError = float(std::clamp(double(imbalance), 0.0, .35));
    double width = asOriginalFloat(state.telemarkWidth);
    // Source 0x2758a..0x275a7 clamps negative early-takeoff duration before judging.
    double duration = std::max(0.0, asOriginalFloat(state.takeoffDuration));
    for (double &judge : result.judges) {
        double score = 20;
        score -= std::sqrt(std::abs(double(result.perturbations[0]))) * (40 + random.next(75)) / 100;
        score -= std::sqrt(std::abs(double(result.perturbations[1]))) * (40 + random.next(75)) / 100;
        // Random still advances for this term on a standing landing.
        score -= (result.crashKind > 0 ? 10 : 0) * (70 + random.next(35)) / 100.0;
        score -= (.4 - width) * (.4 - width) * 18 * (70 + random.next(45)) / 100;
        score -= (.2 - duration) * 12.5 * (60 + random.next(55)) / 100;
        score -= result.normalizedFlightError * 10 * (60 + random.next(55)) / 100;
        score += random.centeredAverage(6);
        score += double(random.centeredAverage(2)) * .4;
        float storedScore = float(score - .3);
        if (storedScore > 20)
            storedScore = 20;
        if (storedScore > 10 && result.crashKind > 0)
            storedScore = float(10 - random.next(15) / 10.0);
        if (storedScore < 0)
            storedScore = 0;
        // 0x2781d..0x2784f: round-to-nearest after a +0.24999 bias,
        // effectively rounding positive marks up to the next half point.
        judge = originalRound((double(storedScore) + .24999) * 2) / 2.0;
    }
    auto selected = result.judges;
    std::sort(selected.begin(), selected.end());
    result.stylePoints = selected[1] + selected[2] + selected[3];
    return result;
}
} // namespace sjp
