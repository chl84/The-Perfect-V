#pragma once
#include "environment.h"
#include "jumper_appearance.h"
#include "original_ai.h"
#include "original_controls.h"
#include "original_outrun.h"
#include "original_physics.h"
#include "original_pose.h"
#include "original_wind.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sjp {
constexpr double PI = 3.14159265358979323846;
inline constexpr size_t HILL_COUNT = 32;
template <class T> T clamp(T x, T a, T b) {
    return x < a ? a : x > b ? b : x;
}
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        double n = length();
        return n > 1e-8 ? *this * (1 / n) : Vec3{0, 1, 0};
    }
};
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
struct Hill {
    std::string id, name, country, subtitle;
    int k;
    double tint;
    // Original single-precision DSJ2 initializer values, promoted losslessly.
    std::array<double, 8> inrunParameters, landingParameters;
    double inrunB = 0, inrunC = 0, landingScale = 0, landingB = 0, landingC = 0;
    double kMarkerX = 0;
    std::vector<double> arcLengths{};
    std::vector<double> arcPositions{};
    double scale() const { return k / 120.0; }
    double inrun(double x) const;
    double inrunSlope(double x) const;
    double ground(double x) const;
    double slope(double x) const;
    double distance(double x) const;
    double atDistance(double d) const;
    double start() const { return inrunParameters[4] - inrunParameters[3]; }
    double inrunStart() const { return inrunParameters[0] - inrunParameters[3]; }
    double inrunTransition() const { return inrunParameters[2] - inrunParameters[3]; }
    double landingTransition() const { return landingParameters[4] - inrunParameters[3]; }
    double outrunBottom() const;
    double end() const;
    double takeoffAltitude() const;
    bool flying() const { return k >= 145; }
    double pointsPerMeter() const { return flying() ? 1.2 : k > 90 ? 1.8 : 2.0; }
};
const std::array<Hill, HILL_COUNT> &hills();
enum class Phase : int { Ready, Inrun, Flight, Outrun, Finished, Disqualified };
enum class Landing : int { None, Telemark, TwoFoot };
struct Input {
    bool start = false, takeoff = false;
    double pitchDelta = 0;
    Landing land = Landing::None;
    // -1 supplies virtual button presses for keyboard/CPU; mouse uses bits1/2.
    int buttonMask = -1;
    double landingDelay = .3;
    int mouseDy = 0;
    double mouseSensitivity = 1;
};
struct Result {
    double distance = 0, distancePoints = 0, stylePoints = 0, total = 0, takeoff = 0;
    std::array<double, 5> judges{};
    bool crashed = false, disqualified = false, beforeTakeoff = false;
    Landing landing = Landing::None;
    double telemarkWidth = 0;
    int crashKind = 0;
    std::string feedback;
};
struct Frame {
    float x = 0, y = 0, vx = 0, vy = 0, pitch = 0, elapsed = 0;
    int32_t phase = 0, landing = 0, crashed = 0;
    std::array<float, 22> pose{}; // eighteen angles, then four offsets
    float telemarkWidth = 0;
    float windSpeed = 0, windAngle = 0;
    int32_t leadingLeg = 0;
    // Temporary presentation data; replay storage keeps the original pose.
    std::array<Vec3, 21> renderSkeleton{};
    bool hasRenderSkeleton = false;
};
// Presentation only: never feed interpolated values back into simulation.
Frame interpolateFrame(const Frame &previous, const Frame &current, double alpha);
struct ReplayTick {
    uint32_t counter = 0;
    int stageBefore = 1, stageAfter = 1, mouseDy = 0, buttonMask = 0;
    bool representable = true;
};
struct Replay {
    int hill = 0;
    double wind = 0;
    Result result;
    std::vector<Frame> frames;
    std::vector<uint8_t> originalRpl; // Optional original input recording for lossless later export.
    bool hasEnvironment = false;
    environment::Conditions environment;
    JumperAppearance jumper = JumperAppearance::Pro; // Cosmetic replay metadata only.
};
double scoreStyle(std::array<double, 5> judges);
double distancePoints(const Hill &, double distance);
class Simulation {
  public:
    int hillIndex = 2;
    Phase phase = Phase::Ready;
    Landing landing = Landing::None;
    double x = 0, y = 0, vx = 0, vy = 0, pitch = 0, elapsed = 0, phaseTime = 0, wind = 2, baseWind = 2;
    double countdown = 15, takeoffQuality = 0, landingAge = 0, instability = 0;
    double takeoffDuration = 0, flightDuration = 0;
    double windSpeed = 2, windAngle = PI, telemarkWidth = 0;
    int originalStage = 1;
    uint32_t originalCounter = 1;
    OriginalPose pose;
    bool jumped = false, crashed = false;
    Result result;
    std::vector<Frame> frames;
    explicit Simulation(int hill = 2, double wind = 2);
    const Hill &hill() const { return hills()[hillIndex]; }
    void reset(int hill, double wind);
    // Original randomized hill weather, then the per-jump Randomize seed.
    void resetOriginal(int hill, uint32_t weatherSeed, uint32_t jumpSeed);
    void resetWithWeather(int hill, const OriginalWind &weather, uint32_t jumpSeed);
    const OriginalWind &weatherState() const { return windModel; }
    void configureAi(const OriginalAiSkills &skills, bool computer = true, bool fastAdvance = false);
    void step(double dt, const Input &input);
    void setWind(double speed, double angle); // fixed vector for measured comparisons
    double clearance() const { return y - hill().ground(x); }
    double optimalPitch() const;
    double efficiency() const;
    bool automaticLanding() const { return virtualLanding; }
    double displayWindAngle() const { return fixedWind ? windAngle : windModel.displayAngle; }
    Frame frame() const;
    Frame renderFrame(double remainingTime = 0) const;
    const std::vector<ReplayTick> &recordedInputs() const { return inputHistory; }
    const OriginalWind &initialWeather() const { return recordingWeather; }
    uint32_t initialRandomSeed() const { return recordingSeed; }
    float originalRecordedDistance() const { return recordingDistance; }
    Replay replay() const { return {hillIndex, baseWind, result, frames, {}, false, {}}; }

  private:
    friend struct RuntimeComparisonAccess;
    Frame previousRenderFrame;
    std::vector<ReplayTick> inputHistory;
    OriginalWind recordingWeather;
    uint32_t recordingSeed = 0;
    float recordingDistance = 0;
    double recordClock = 0, tickClock = 0, inrunWork = 0, inrunStartY = 0, inrunInitialSpeed = 1;
    double scalarSpeed = 1, groundAngle = 0, impulseX = 0, impulseY = 0, impactAngle = 0, impactSpeed = 0;
    double desiredLandingDelay = .3;
    int firstLandingButton = 0;
    bool fixedWind = false, pendingLaunch = false, contactPending = false, virtualLanding = false;
    OriginalFlightStyle flightStyle;
    OriginalWind windModel;
    OriginalAi ai;
    OriginalAiInput lastAiInput;
    bool computerControlled = false;
    bool fastAdvance = false;
    bool outrunAirborne = false, outrunContact = false;
    int fallBounceCount = 0, startHoldTicks = 0;
    bool keyboardStartQueued = false;
    OriginalLandingAssessment impactAssessment;
    uint32_t randomState = 1;
    Input queuedInput;
    void tick(const Input &input);
    void controller(const Input &input);
    OriginalAirState airState(double q) const;
    void jump();
    void touchDown();
};
Input autopilot(const Simulation &);
} // namespace sjp
