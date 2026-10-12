#pragma once
#include "physics.h"

namespace sjp {
// Original DSJ2 points1..21 from DS[0x4406..0x44fe], converted from the
// original lateral/travel/vertical order to the renderer's travel/up/lateral.
// Coordinates are relative to the simulation origin, in metres. Do not apply
// a foot-angle rotation or an additional telemark translation to these nodes.
enum class OriginalJoint : size_t {
    SkiRear1, SkiFront1, SkiRear2, SkiFront2,
    Ankle1, Knee1, Hip1, Ankle2, Knee2, Hip2,
    HipCenter, ShoulderCenter, Shoulder1, Hand1, Shoulder2, Hand2,
    Head, SkiTip2, SkiTip1, Elbow1, Elbow2
};
struct OriginalSkeleton {
    std::array<Vec3, 21> points{};
    Vec3 operator[](OriginalJoint joint) const { return points[size_t(joint)]; }
};

OriginalSkeleton originalSkeleton(const std::array<float, 22> &pose, int leadingLeg = 0);
} // namespace sjp
