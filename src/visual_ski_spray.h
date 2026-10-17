#pragma once
#include "original_skeleton.h"
#include "visual_snow_mesh.h"

namespace sjp::visual {
struct BrakingSkiSpray {
    Vec3 ankle, front, outward;
    double strength = 0;
};

// Read the displayed skis, including replay interpolation. This is cosmetic:
// no braking force, pose, simulation state or random stream is changed.
inline BrakingSkiSpray brakingSkiSpray(const Hill &hill, const Frame &frame,
                                      const OriginalSkeleton &skeleton, size_t leg) {
    BrakingSkiSpray spray;
    if (Phase(frame.phase) != Phase::Outrun || frame.crashed)
        return spray;
    const Vec3 origin{frame.x, frame.y, 0};
    spray.ankle = skeleton[leg == 0 ? OriginalJoint::Ankle1 : OriginalJoint::Ankle2] + origin;
    spray.front = skeleton[leg == 0 ? OriginalJoint::SkiFront1 : OriginalJoint::SkiFront2] + origin;
    const Vec3 axis = spray.front - spray.ankle;
    const double length = std::hypot(axis.x, axis.z);
    if (length < .1)
        return spray;
    const double side = leg == 0 ? 1.0 : -1.0;
    const Vec3 direction{axis.x / length, 0, axis.z / length};
    spray.outward = {-direction.z * side, 0, direction.x * side};
    const double wedge = clamp((-side * direction.z - .025) / .25, 0.0, 1.0);
    const double motion = clamp((std::hypot(frame.vx, frame.vy) - 1.2) / 18, 0.0, 1.0);
    const Vec3 contact = spray.ankle + axis * .6;
    const double gap = (contact.y - terrainSnowHeight(hill, contact.x, contact.z)) /
                       std::hypot(1.0, hill.slope(contact.x));
    const double grounded = clamp((.24 - gap) / .14, 0.0, 1.0) *
                            clamp((gap + .28) / .14, 0.0, 1.0);
    spray.strength = wedge * motion * grounded;
    return spray;
}
} // namespace sjp::visual
