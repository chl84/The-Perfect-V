#include "original_skeleton.h"
#include <algorithm>
#include <cmath>

namespace sjp {
namespace {
constexpr std::array<float, 101> ACOS = {{
#include "original_skeleton_acos.inc"
}};
long double sinOriginal(long double value) {
#if (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__)
    // Original runtime0x21fb:0xc9d calls the x87 SIN operation. libm's
    // correctly rounded extended result can differ from hardwareFSIN by one
    // final significand bit, visible after ankle-origin cancellation.
    __asm__("fsin" : "+t"(value));
    return value;
#else
    return std::sin(value);
#endif
}
long double cosOriginal(long double value) {
#if (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__)
    __asm__("fcos" : "+t"(value));
    return value;
#else
    return std::cos(value);
#endif
}
double store(long double value) {
    // Materialize the original DWORD store even when the optimizer carries
    // surrounding calculations in x87 registers at excess precision.
    volatile float stored = float(value);
    return stored;
}
Vec3 point(long double x, long double y, long double z) {
    return {store(x), store(y), store(z)};
}
// Source Round(100*height/1.08) and Round(100*extent/.74), with nearest-even
// integer conversion and the REAL80 denominator. The original table has101
// samples including both endpoints. Protect external/replay input bounds.
long double bend(float extent, long double maximum) {
    long double scaled = std::clamp(100.L * extent / maximum, 0.L, 100.L);
    int index = int(std::floor(scaled));
    long double remainder = scaled - index;
    if (remainder > .5L || (remainder == .5L && (index & 1)))
        ++index;
    return ACOS[size_t(index)];
}
} // namespace

OriginalSkeleton originalSkeleton(const std::array<float, 22> &p, int leadingLeg) {
    OriginalSkeleton skeleton;
    auto &nodes = skeleton.points;
    auto set = [&](OriginalJoint joint, Vec3 value) { nodes[size_t(joint)] = value; };
    auto get = [&](OriginalJoint joint) { return nodes[size_t(joint)]; };
    const long double rotation = p[8];
    const size_t lead = leadingLeg == 1 ? 9 : 0;
    const long double leadHeight = p[leadingLeg == 1 ? 20 : 18];
    const long double leadAxis = p[lead + 2] + rotation;
    const long double leadRoll = p[lead + 3];
    const Vec3 hip = point(cosOriginal(leadAxis) * leadHeight * cosOriginal(leadRoll),
                          sinOriginal(leadAxis) * leadHeight * cosOriginal(leadRoll), 0);
    set(OriginalJoint::HipCenter, hip);
    set(OriginalJoint::Hip1, hip);
    set(OriginalJoint::Hip2, hip);
    const long double bodyAxis = p[4] + rotation;
    const Vec3 shoulder = point(hip.x + cosOriginal(bodyAxis) * .49L,
                               hip.y + sinOriginal(bodyAxis) * .49L, hip.z);
    set(OriginalJoint::ShoulderCenter, shoulder);
    set(OriginalJoint::Shoulder1, point(shoulder.x, shoulder.y, hip.z + .22L));
    set(OriginalJoint::Shoulder2, point(shoulder.x, shoulder.y, hip.z - .22L));
    const long double headAxis = (static_cast<long double>(p[5]) - .3L) + rotation;
    set(OriginalJoint::Head, point(shoulder.x + cosOriginal(headAxis) * .24L,
                                 shoulder.y + sinOriginal(headAxis) * .24L, hip.z));
    for (size_t leg = 0; leg < 2; ++leg) {
        const size_t angle = 9 * leg;
        const long double sign = leg == 0 ? 1 : -1;
        const long double height = p[18 + 2 * leg];
        const long double axis = p[angle + 2] + rotation;
        const long double roll = p[angle + 3];
        const Vec3 ankle = point(hip.x - cosOriginal(axis) * height * cosOriginal(roll),
                                 hip.y - sinOriginal(axis) * height * cosOriginal(roll),
                                 hip.z + sign * sinOriginal(roll) * height);
        set(leg == 0 ? OriginalJoint::Ankle1 : OriginalJoint::Ankle2, ankle);
        // Each derived bend angle is stored as float32 before geometry uses it.
        const long double kneeAxis = static_cast<long double>(float(p[angle + 2] - bend(p[18 + 2 * leg], 1.08L))) + rotation;
        const Vec3 knee = point(ankle.x + cosOriginal(kneeAxis) * .54L,
                                ankle.y + sinOriginal(kneeAxis) * .54L * cosOriginal(roll),
                                (ankle.z + hip.z) / 2.L);
        set(leg == 0 ? OriginalJoint::Knee1 : OriginalJoint::Knee2, knee);
        const Vec3 armShoulder = get(leg == 0 ? OriginalJoint::Shoulder1 : OriginalJoint::Shoulder2);
        const long double extent = p[19 + 2 * leg];
        const long double armAxis = p[angle + 6] + rotation;
        const long double armRoll = p[angle + 7];
        const Vec3 hand = point(armShoulder.x - cosOriginal(armAxis) * extent * cosOriginal(armRoll),
                               armShoulder.y - sinOriginal(armAxis) * extent * cosOriginal(armRoll),
                               armShoulder.z + sign * sinOriginal(armRoll) * extent);
        set(leg == 0 ? OriginalJoint::Hand1 : OriginalJoint::Hand2, hand);
        const long double elbowAxis = static_cast<long double>(float(p[angle + 6] + bend(p[19 + 2 * leg], .74L))) + rotation;
        const Vec3 elbow = point(hand.x + cosOriginal(elbowAxis) * .37L,
                                 hand.y + sinOriginal(elbowAxis) * .37L * cosOriginal(armRoll),
                                 (armShoulder.z + hand.z) / 2.L);
        set(leg == 0 ? OriginalJoint::Elbow1 : OriginalJoint::Elbow2, elbow);
        const long double yaw = p[angle];
        const long double pitch = p[angle + 1];
        const Vec3 rear = point(ankle.x - cosOriginal(yaw) * cosOriginal(pitch),
                               ankle.y - sinOriginal(pitch), ankle.z - sign * sinOriginal(yaw));
        const Vec3 front = point(ankle.x + cosOriginal(yaw) * 1.628L * cosOriginal(pitch),
                                ankle.y + 1.628L * sinOriginal(pitch),
                                ankle.z + sign * sinOriginal(yaw) * 1.628L);
        const Vec3 tip = point(ankle.x + cosOriginal(yaw) * 1.648L * cosOriginal(pitch + .04L),
                              ankle.y + 1.648L * sinOriginal(pitch + .04L),
                              ankle.z + sign * sinOriginal(yaw) * 1.648L);
        set(leg == 0 ? OriginalJoint::SkiRear1 : OriginalJoint::SkiRear2, rear);
        set(leg == 0 ? OriginalJoint::SkiFront1 : OriginalJoint::SkiFront2, front);
        set(leg == 0 ? OriginalJoint::SkiTip1 : OriginalJoint::SkiTip2, tip);
    }
    return skeleton;
}
} // namespace sjp
