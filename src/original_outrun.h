#pragma once
#include "original_scoring.h"

namespace sjp {
struct OriginalOutrunInput {
    int originalStage = 11, crashKind = 0, roughness = 0;
    int fallBounceCount = 0;
    double width = .3, terrainAngle = 0, terrainSlope = 0, aheadSlope = 0;
    double vx = 0, vyDown = 0, speed = 0, impactAngle = 0, mass = 60;
    double existingImpulseX = 0, existingImpulseYUp = 0;
    bool airborne = false, contactEvent = false, fastAdvance = false;
};

struct OriginalOutrunAction {
    int originalStage = 11;
    int fallBounceCount = 0;
    bool pendingLaunch = false, pitchChanged = false;
    float impulseX = 0, impulseYUp = 0, pitch = .5f;
    // Preserve the cosmetic draw on ordinary recontacts; fast advance skips it.
    int impactSoundFrequency = 0;
};

// 0x2e7ae..0x2ea08: complete original standing/settle targets.
void originalSettleTargets(OriginalPose &pose, double width, double terrainAngle);
// 0x27043..0x27398, after OriginalLandingAssessment's pre-judge draws.
// This operation must not consume additional random numbers.
void originalInitializeImpact(OriginalPose &pose, const OriginalLandingAssessment &assessment,
                              double width, double terrainAngle);
// Original random pose callback 0x2e167..0x2e72b, all four args are float32
// at its far-call boundary. All38 RNG calls retain the original draw order.
void originalRandomizeFallPose(OriginalPose &pose, double targetBase, double targetRange,
                               double rateBase, double rateRange, OriginalRandom &random);
// 0x281bf..0x2853b: balance height targets near the surface before landing.
// surfaceAhead is the source height at float32(xOriginal-1), and y is the
// athlete's current height in the same coordinate system.
void originalNearSurfaceHeights(OriginalPose &pose, int originalStage, double y,
                               double surfaceAhead, double terrainSlope);
// 0x27df8..0x27fd2 then0x2853b..0x28dcc. Call after physics and the
// grounded-foot prepare callback, before pose.advance(dt).
OriginalOutrunAction originalOutrunUpdate(OriginalPose &pose, const OriginalOutrunInput &input,
                                         OriginalRandom &random);
// Main0x291bd..0x291e0 and0x2949f..0x294c2: no elapsed-time limit.
bool originalOutrunStopped(bool airborne, double speed, bool landingComplete);
} // namespace sjp
