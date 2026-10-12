#pragma once

namespace sjp {
inline constexpr double ORIGINAL_GRAVITY = 9.81;
inline constexpr double ORIGINAL_MASS = 60;
inline constexpr float ORIGINAL_TICK = 1.f / 70.f;

// Original positive-up world coordinates, with the stored vertical velocity
// explicitly named down. Angles and q use the original radian convention.
struct OriginalAirState {
    double vx = 0, vyDown = 0, q = 0, footAngle1 = 0, footAngle2 = 0;
    double windSpeed = 0, windAngle = 0, bodyFactor = 1, mass = ORIGINAL_MASS;
};
struct OriginalForces {
    double relativeX = 0, theta = 0, speed = 0;
    double body = 0, foot1 = 0, foot2 = 0, up = 0, resistance = 0;
    double ax = 0, ay = 0;
};
struct OriginalFlightStep {
    double x = 0, y = 0, vx = 0, vyDown = 0;
    OriginalForces forces;
};
OriginalForces originalFlightForces(OriginalAirState);
OriginalFlightStep originalFlightStep(double x, double y, OriginalAirState);

struct OriginalInrunState {
    double x = 0, y = 0, speed = 1, startY = 0, initialSpeed = 1, work = 0;
    double angle = 0, windSpeed = 0, windAngle = 0, bodyFactor = .5;
    double friction = .02, mass = ORIGINAL_MASS;
    int poseState = 3;
};
struct OriginalInrunStep {
    double x = 0, speed = 0, work = 0, vx = 0, vyDown = 0;
};
OriginalInrunStep originalInrunStep(OriginalInrunState);
struct OriginalImpulse { double x = 0, y = 0; };
OriginalImpulse originalTakeoffDose(OriginalImpulse accumulated, double height, double groundAngle,
                                    int buttonCount);
} // namespace sjp
