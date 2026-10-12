#pragma once
#include "original_scoring.h"

namespace sjp {
// Original DS[4c5a], DS[4c5f], DS[4c63]. Accumulate before this tick's
// mouse/noise update and before a stage-7 landing click changes the stage.
struct OriginalFlightStyle {
    float integral = 0;
    float duration = 0;
    float landingTimer = 0;

    void accumulate(int originalStage, bool flightFlag, double q,
                    const OriginalPose &pose, double dt);
};

// Original signed mouse mickeys, clamped by the input wrapper to -128..127.
// The returned delta is in original q units, ready to add to q.
double originalMousePitchDelta(int mouseDy, double dt);

// Original stages 6/7 only. Preserve one global RNG shared with wind and
// scoring. pitchDelta is already in q units (mouse, keyboard, or autopilot).
void updateOriginalFlightInput(int originalStage, double &q, double pitchDelta,
                               double dt, double windSpeed, double windAngle,
                               OriginalRandom &random, OriginalPose &pose);
} // namespace sjp
