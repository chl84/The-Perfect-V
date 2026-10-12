#include "original_physics.h"
#include <algorithm>
#include <cmath>

namespace sjp {
OriginalForces originalFlightForces(OriginalAirState s) {
    s.vx = float(s.vx);
    s.vyDown = float(s.vyDown);
    s.q = float(s.q);
    s.footAngle1 = float(s.footAngle1);
    s.footAngle2 = float(s.footAngle2);
    s.windSpeed = float(s.windSpeed);
    s.windAngle = float(s.windAngle);
    s.bodyFactor = float(s.bodyFactor);
    s.mass = float(s.mass);
    OriginalForces f;
    f.relativeX = s.vx - s.windSpeed * std::cos(s.windAngle);
    // Original uses atan of the quotient and discards the relative-X quadrant.
    f.theta = float(std::abs(f.relativeX) > .0001 ? std::atan(s.vyDown / f.relativeX)
                                                : 1.57079632679489661923);
    f.speed = float(std::hypot(f.relativeX, s.vyDown));
    double speed2 = f.speed * f.speed, bodyAngle = .35 + s.q;
    f.body = float(1.1 * s.bodyFactor * speed2 * std::sin(bodyAngle + f.theta) * .53);
    f.foot1 = float(.139284 * speed2 * std::sin(f.theta + s.footAngle1));
    f.foot2 = float(.139284 * speed2 * std::sin(f.theta + s.footAngle2));
    f.up = float(std::abs(std::cos(bodyAngle) * f.body) + std::abs(std::cos(s.footAngle1) * f.foot1) +
                 std::abs(std::cos(s.footAngle2) * f.foot2));
    f.resistance = float(std::abs(std::sin(bodyAngle) * f.body) + std::abs(std::sin(s.footAngle1) * f.foot1) +
                         std::abs(std::sin(s.footAngle2) * f.foot2));
    f.resistance = float(f.resistance + .1166 * s.vx * s.vx);
    f.up = float(f.up + .1166 * s.vyDown * s.vyDown);
    if (f.up > ORIGINAL_GRAVITY * s.mass)
        f.up = float(ORIGINAL_GRAVITY * s.mass);
    f.ax = -f.resistance / s.mass;
    f.ay = f.up / s.mass - ORIGINAL_GRAVITY;
    return f;
}
OriginalFlightStep originalFlightStep(double x, double y, OriginalAirState s) {
    OriginalFlightStep next;
    next.forces = originalFlightForces(s);
    next.vyDown = float(s.vyDown - next.forces.ay * ORIGINAL_TICK);
    next.vx = float(s.vx + next.forces.ax * ORIGINAL_TICK);
    next.x = float(x + next.vx * ORIGINAL_TICK);
    next.y = float(y - next.vyDown * ORIGINAL_TICK);
    return next;
}
OriginalInrunStep originalInrunStep(OriginalInrunState s) {
    s.x = float(s.x);
    s.y = float(s.y);
    s.speed = float(s.speed);
    s.startY = float(s.startY);
    s.initialSpeed = float(s.initialSpeed);
    s.work = float(s.work);
    s.angle = float(s.angle);
    s.windSpeed = float(s.windSpeed);
    s.windAngle = float(s.windAngle);
    s.bodyFactor = float(s.bodyFactor);
    s.friction = float(s.friction);
    s.mass = float(s.mass);
    double downAngle = float(-s.angle);
    if (s.speed == 0)
        s.speed = float(.0001);
    double relative = float(s.speed - std::cos(s.windAngle) * s.windSpeed * std::cos(downAngle) * .2);
    relative = std::max(0.0, relative);
    double work = float(s.work + std::abs(s.speed * ORIGINAL_TICK *
                       (s.friction * s.mass * ORIGINAL_GRAVITY * std::cos(downAngle) +
                        .583 * s.bodyFactor * relative * relative)));
    double extraFriction = s.poseState == 8 || s.poseState == 100 ? 10 : s.poseState == 9 ? 20 : 0;
    if (extraFriction)
        work = float(work + std::abs(s.speed * ORIGINAL_TICK * s.friction * extraFriction * s.mass *
                                    ORIGINAL_GRAVITY * std::cos(downAngle)));
    double available = s.mass * ORIGINAL_GRAVITY * (s.startY - s.y) +
                       .5 * s.mass * s.initialSpeed * s.initialSpeed;
    if (work > available)
        work = float(available);
    OriginalInrunStep next;
    next.work = work;
    // 0x2d738..0x2d78b keeps these cancelling energy terms on the x87
    // stack. Preserve its REAL80 9.81 constant and intermediate precision;
    // rounding them to double changes the final slow outrun speed by 1 ULP.
    const long double sourceGravity = 9.81L;
    const long double sourceMass = s.mass;
    next.speed = float(std::sqrt(std::abs((sourceMass * sourceGravity * s.startY +
                         .5L * sourceMass * s.initialSpeed * s.initialSpeed - work -
                         sourceMass * sourceGravity * s.y) * 2 / sourceMass)));
    next.x = float(s.x + std::cos(downAngle) * next.speed * ORIGINAL_TICK);
    next.vx = float(std::cos(downAngle) * next.speed);
    next.vyDown = float(std::sin(downAngle) * next.speed);
    return next;
}
OriginalImpulse originalTakeoffDose(OriginalImpulse impulse, double height, double groundAngle,
                                    int buttonCount) {
    double dose = float(1275.0 * buttonCount * ORIGINAL_TICK *
                       std::cos((double(float(height)) / 1.08 - .5) / .5 * 1.57079632679489661923));
    impulse.x = float(impulse.x - std::sin(double(float(groundAngle))) * dose);
    impulse.y = float(impulse.y + std::cos(double(float(groundAngle))) * dose);
    return impulse;
}
} // namespace sjp
