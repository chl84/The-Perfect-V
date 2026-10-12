#pragma once
#include <array>

namespace sjp {
// Recovered DSJ2 pose state. Angles use original radians: negative on a
// descending ramp, positive for nose-up flight. Offsets use world metres.
class OriginalPose {
  public:
    std::array<float, 18> angles{}, angleTargets{}, angleRates{};
    std::array<float, 4> offsets{}, offsetTargets{}, offsetRates{};
    std::array<float, 2> auxiliary{};
    std::array<float, 2> footTrims{};
    int leadingLeg = 0;
    float bodyFactor = .5f;
    float modeTime = 0;
    int mode = 1;
    int stage = 1;

    void reset(double motionAngle = 0);
    // Stages are original DS[0x4c6b], not the modern Phase enum. The explicit
    // overload supports flight that begins while takeoff stage 4/5 is active.
    void update(int originalStage, double q, double width, double dt);
    void update(int originalStage, double q, double width, double dt,
                double motionAngle, bool airborne, double flightBlend = 0);
    // Set refreshLandingTargets=false on the terrain-contact controller tick.
    // landingClick distinguishes a same-tick click/width freeze at stage9
    // from an unprepared contact that also changes the stage to9.
    void prepare(int originalStage, double q, double width,
                 double motionAngle, bool airborne, double flightBlend = 0,
                 bool refreshLandingTargets = true, bool landingClick = false);
    void advance(double dt);
    // The original table setter preserves modeTime; transition callers
    // explicitly reset it where the source controller does so.
    void selectMode(int originalMode, double motionAngle);
    // Source 0x26c63 refreshes these after flight input, before a landing
    // click can change the stage. A contact on that same tick retains these
    // targets because the landing-target callback is skipped.
    void flightTargets(double q);

    double height() const { return offsets[0]; }
    double actualHeight() const { return offsets[0]; }
    double footAngle1() const { return angles[1]; }
    double footAngle2() const { return angles[10]; }
    double bodyAngle() const { return angles[4]; }

  private:
    double previousMotionAngle = 0;
    void landingTargets(double width, double motionAngle);
    void groundedFootTargets(double motionAngle);
    void airborneFootTargets(int originalStage, double q, double motionAngle, double blend);
};
} // namespace sjp
