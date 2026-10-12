#pragma once
#include "original_scoring.h"
#include <array>
#include <cstdint>

namespace sjp {
// Original per-jumper DS[0x5bfb + 17*player] record: four float32 values,
// followed by the selected skill level. These are not modern pitch targets.
struct OriginalAiSkills {
    std::array<float, 4> values{};
    int level = 0; // 0 Poorest, 1 Poor, 2 Average, 3 Good, 4 Superb; 5 Random.
};

// Generate the persistent traits once per cup; refresh form each new hill.
// With doubleTeamRandomLevel, retain DS5ba6's original stale-level behaviour
// in Double Team Cup. The ordinary World Cup uses the selected level's table.
OriginalAiSkills generateOriginalAiSkills(int selectedLevel, OriginalRandom &random,
                                         int *doubleTeamRandomLevel = nullptr);
void refreshOriginalAiHillForm(OriginalAiSkills &skills, OriginalRandom &random);

struct OriginalAiObservation {
    uint32_t tick = 1; // Original DS4c50 count, initialized to one each jump.
    int stage = 1;
    double q = 0, telemarkWidth = 0;
    double distanceToTakeoff = 0;
    double speed = 1, motionAngle = 0;
    double vx = 0, vyDown = 0;
    // Original positive-up y - G(x), G'(x), and terrain angle at f32(x+5).
    double clearance = 0, landingSlope = 0, terrainAngleAhead = 0;
};

struct OriginalAiInput {
    int mouseDy = 0;
    int buttonMask = 0; // Bit1 left; bit2 right. Held state, not edge events.
};

class OriginalAi {
  public:
    // Recovered DS3f0c..3f28 fields are exposed for retained comparison traces.
    float leftTakeoffTime = 0, rightTakeoffTime = 0;
    float pitchDelay = 0, pitchDuration = 0, pitchRemaining = 0, targetPitch = 0;
    float landingLead = 0, landingWidth = 0;
    bool assessImpactOnce = true, unusualLanding = false;

    // Initialization consumes the shared RNG. Each mouse poll copies it and
    // restores it, matching main260d1's DS56a4/56a6 seed preservation.
    void initialize(const OriginalAiSkills &skills, int hillK, OriginalRandom &random);
    OriginalAiInput poll(const OriginalAiObservation &observation,
                         const OriginalRandom &globalRandom, double dt = 1.0/70);
};
} // namespace sjp
