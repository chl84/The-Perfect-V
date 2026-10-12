#include "original_wind.h"
#include <algorithm>
#include <cmath>

namespace sjp {
namespace {
constexpr double PI = 3.141592653589793238462643383279502884;
}

uint32_t originalWindSeedFromDosTime(uint8_t hours, uint8_t minutes,
                                    uint8_t seconds, uint8_t hundredths) {
    return (uint32_t(seconds) << 24) | (uint32_t(hundredths) << 16) |
           (uint32_t(hours) << 8) | uint32_t(minutes);
}

void OriginalWind::initialize(OriginalRandom &random) {
    const float previousAngularCap = angularCap;
    // 0x24ee7..0x24efa consumes these draws even when weather is invisible.
    const int enabled = random.next(2);
    const uint8_t initialWeather = uint8_t(enabled * random.next(255));
    // 0x24efd..0x24f42: both integer terms remain on the x87 stack until
    // the centered five-draw term has been added; only then a DWORD store.
    const int first = random.next(20);
    const int second = random.next(20);
    const double noise = random.centeredAverage(5);
    const float initialSpeed = float(first / 10.0 + second / 10.0 + noise * 2);
    const float boundedSpeed = std::clamp(initialSpeed, 0.f, 4.5f);
    // 0x24f91..0x24fab: direction is uniform on {0,.001,...,6.281} rad.
    const float initialAngle = float(random.next(6282) / 1000.0);
    reset(boundedSpeed, initialAngle, random);
    weatherIntensity = initialWeather;
    // DS4cdc is absent from0x24ee7..0x25017; its first update writes.2.
    angularCap = previousAngularCap;
}

void OriginalWind::reset(double initialSpeed, double initialAngle, OriginalRandom &random) {
    // Initializer 0x24fb2..0x25017. Retain the original float32 globals.
    speed = float(initialSpeed);
    angle = displayAngle = float(initialAngle);
    x = float(std::cos(double(angle)) * double(speed));
    y = float(std::sin(double(angle)) * double(speed));
    const double first = std::abs(double(random.centeredAverage(4)));
    const double second = std::abs(double(random.centeredAverage(4)));
    variationStep = float((first + second) / 3 + .05);
    angularStep = 0;
    angularCap = .2f;
    frameCounter = 0;
    weatherIntensity = 0;
}

bool OriginalWind::advance(uint32_t sourceFrameCounter, OriginalRandom &random, double dt) {
    frameCounter = sourceFrameCounter;
    // 0x25e0d..0x25e34: the x87 FISTP helper rounds to nearest/even.
    const auto roundedFrame = int64_t(std::nearbyint(double(int32_t(frameCounter)) *
                                                    double(float(dt)) * 70));
    if (roundedFrame % 10 != 0)
        return false;
    // 0x25e37..0x25e60: original draw order is Y before X.
    y = float(double(y) + double(random.centeredAverage(5)) * double(variationStep));
    x = float(double(x) + double(random.centeredAverage(5)) * double(variationStep));
    // 0x25e67..0x25f15, preserving its unnormalised angle representation.
    if (x > 0)
        angle = float(std::atan(double(y) / double(x)));
    else if (x < 0)
        angle = float(std::atan(double(y) / double(x)) + PI);
    else
        angle = float(y > 0 ? PI / 2 : -PI / 2);
    speed = float(std::sqrt(double(y) * double(y) + double(x) * double(x)));
    // 0x25f3b..0x25f97: attenuation changes next update's vector, not the
    // magnitude already stored for this update's physical forces.
    if (double(speed) > 3.8) {
        const double attenuation = 1 - (double(speed) - 3.8) / 50;
        y = float(double(y) * attenuation);
        x = float(double(x) * attenuation);
    }
    angularCap = .2f;
    // 0x25fa5..0x260ce: separate IFs; the second re-evaluates the sine after
    // the first moves displayAngle. There is no angle normalisation.
    if (std::sin(double(angle) - double(displayAngle)) > .05) {
        if (angularStep < 0)
            angularStep = float(double(angularStep) + .05);
        angularStep = float(double(angularStep) + .05);
        if (angularStep > angularCap)
            angularStep = angularCap;
        displayAngle = float(double(displayAngle) + double(angularStep));
    }
    if (std::sin(double(angle) - double(displayAngle)) < -.05) {
        if (angularStep > 0)
            angularStep = float(double(angularStep) - .05);
        angularStep = float(double(angularStep) - .05);
        if (angularStep < -angularCap)
            angularStep = -angularCap;
        displayAngle = float(double(displayAngle) + double(angularStep));
    }
    return true;
}
} // namespace sjp
