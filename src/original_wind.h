#pragma once
#include "original_scoring.h"
#include <cstdint>

namespace sjp {
// Borland Randomize at 0x33e6d stores DOS GetTime's CX as the low seed
// word and DX as the high word: hour/minute/second/hundredths, no date.
uint32_t originalWindSeedFromDosTime(uint8_t hours, uint8_t minutes,
                                    uint8_t seconds, uint8_t hundredths);

// Recovered DSJ 2.10 wind state. x/y are the evolving physical vector;
// angle is used by physics, while displayAngle smooths the visible arrow.
struct OriginalWind {
    float x = 0, y = 0;                       // DS4cc0 / DS4cc4
    float variationStep = .05f;               // DS4cc8
    float speed = 0, angle = 0;               // DS4ccc / DS4cd0
    float displayAngle = 0, angularStep = 0;   // DS4cd4 / DS4cd8
    float angularCap = 0;                     // DS4cdc, untouched by original initialization
    uint32_t frameCounter = 0;                // DS4c50
    uint8_t weatherIntensity = 0;             // cosmetic DS4ce0; retains RNG draws

    // Original randomized start, 0x24ee7..0x25017: 18 RNG draws including
    // the two cosmetic weather draws preceding strength and direction.
    // Supply the seed after Randomize via originalWindSeedFromDosTime or an
    // explicit reproducible seed; initialization does not reseed the caller.
    void initialize(OriginalRandom &random);
    // Explicit wind/replay override. Only the two variation draws are made.
    void reset(double initialSpeed, double initialAngle, OriginalRandom &random);
    // Advance at a supplied original frame count. Returns true only when
    // the source's Round(frame*float32(dt)*70) mod 10 gate opens.
    bool advance(uint32_t sourceFrameCounter, OriginalRandom &random,
                 double dt = 1.0 / 70.0);
    bool tick(OriginalRandom &random, double dt = 1.0 / 70.0) {
        return advance(frameCounter + 1, random, dt);
    }
};
} // namespace sjp
