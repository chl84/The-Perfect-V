#include "physics.h"
#include "original_scoring.h"
#include "original_skeleton.h"
#include <algorithm>

namespace sjp {
namespace {
constexpr float ARC_STEP = .1f;
double unclampedInrun(const Hill &h, double x) {
    const auto &a = h.inrunParameters;
    return x <= a[2] ? a[5] * x + a[6] : a[7] * x * x + h.inrunB * x + h.inrunC;
}
double unclampedInrunSlope(const Hill &h, double x) {
    const auto &a = h.inrunParameters;
    return x <= a[2] ? a[5] : 2 * a[7] * x + h.inrunB;
}
double originalInrun(const Hill &h, double x) {
    const auto &a = h.inrunParameters;
    if (x < a[1])
        return float(a[5] * a[1] + a[6]);
    if (x <= a[2])
        return float(a[5] * x + a[6]);
    return float(a[7] * x * x + h.inrunB * x + h.inrunC);
}
double originalInrunSlope(const Hill &h, double x) {
    const auto &a = h.inrunParameters;
    if (x < a[1])
        return 0;
    return x > a[2] ? float(2 * a[7] * x + h.inrunB) : a[5];
}
double landingBase(const Hill &h, double x) {
    const auto &b = h.landingParameters;
    if (x <= b[4])
        return float(b[1] - b[2] * std::tanh((x - b[0]) * h.landingScale));
    return float(b[5] * x * x + h.landingB * x + h.landingC);
}
double landingBaseSlope(const Hill &h, double x) {
    const auto &b = h.landingParameters;
    double t = std::tanh((x - b[0]) * h.landingScale);
    return x > b[4] ? float(2 * b[5] * x + h.landingB) : float(-b[2] * h.landingScale * (1 - t * t));
}
} // namespace
const std::array<Hill, HILL_COUNT> &hills() {
    static const std::array<Hill, HILL_COUNT> catalog = [] {
        std::array<Hill, HILL_COUNT> h = {{
#include "original_hills.inc"
        }};
        // Cache the original 0.1 m midpoint arc integration. Rendering and scoring
        // use the same curve; no per-hill K scaling changes the recovered geometry.
        for (auto &hill : h) {
            const auto &a = hill.inrunParameters;
            const auto &b = hill.landingParameters;
            hill.inrunB = float(a[5] - 2 * a[7] * a[2]);
            hill.inrunC = float(a[5] * a[2] + a[6] - a[7] * a[2] * a[2] - hill.inrunB * a[2]);
            hill.landingScale = float(1 / b[3]);
            hill.landingB = float(landingBaseSlope(hill, b[4]) - 2 * b[5] * b[4]);
            hill.landingC = float(landingBase(hill, b[4]) - b[5] * b[4] * b[4] - hill.landingB * b[4]);
            float cursor = a[3], distance = 0;
            hill.arcLengths.push_back(0);
            hill.arcPositions.push_back(cursor);
            double cacheEnd = a[3] + hill.end() + 150 * hill.scale();
            while (cursor < cacheEnd) {
                double slope = hill.slope(float(cursor + double(ARC_STEP) / 2) - a[3]);
                distance = float(distance + double(ARC_STEP) * std::hypot(1.0, slope));
                cursor = float(cursor + ARC_STEP);
                hill.arcLengths.push_back(distance);
                hill.arcPositions.push_back(cursor);
            }
            // Original marker routine uses 1 m midpoint steps and K + 0.15 m.
            float x = a[3], total = 0, target = hill.k + .15;
            while (total < target) {
                float multiplier = std::hypot(1.0, hill.slope(float(x + .5) - a[3]));
                float dx = total + multiplier > target ? (target - total) / multiplier : 1;
                float nextX = x + dx, nextTotal = total + dx * multiplier;
                if (nextX <= x || nextTotal <= total) {
                    x = nextX;
                    break;
                }
                x = nextX;
                total = nextTotal;
            }
            hill.kMarkerX = x - a[3];
        }
        return h;
    }();
    return catalog;
}
double Hill::inrun(double x) const {
    double takeoff = inrunParameters[3];
    return originalInrun(*this, x + takeoff) - originalInrun(*this, takeoff);
}
double Hill::inrunSlope(double x) const {
    return originalInrunSlope(*this, x + inrunParameters[3]);
}
double Hill::ground(double x) const {
    double takeoff = inrunParameters[3], originalX = x + takeoff;
    double y = landingBase(*this, originalX);
    if (originalX <= takeoff) {
        y = float(y - landingParameters[7] * unclampedInrun(*this, takeoff));
        y = float(y + landingParameters[7] * unclampedInrun(*this, originalX));
    }
    return y - originalInrun(*this, takeoff);
}
double Hill::slope(double x) const {
    double originalX = x + inrunParameters[3];
    double slope = landingBaseSlope(*this, originalX);
    if (originalX <= inrunParameters[3]) {
        // Preserve the original slope evaluator, including its extra board-slope
        // subtraction. Before takeoff it differs from the height's derivative.
        slope = float(slope - landingParameters[7] * unclampedInrunSlope(*this, inrunParameters[3]));
        slope = float(slope + landingParameters[7] * unclampedInrunSlope(*this, originalX));
    }
    return slope;
}
double Hill::outrunBottom() const {
    const auto &b = landingParameters;
    return -landingB / (2 * b[5]) - inrunParameters[3];
}
double Hill::takeoffAltitude() const {
    return originalInrun(*this, inrunParameters[3]);
}
double Hill::end() const {
    // Simulation boundary only: retain the original quadratic through the outrun.
    return outrunBottom() + 80 * scale();
}
double Hill::distance(double x) const {
    if (x <= 0)
        return 0;
    float endpoint = x + inrunParameters[3];
    if (endpoint <= inrunParameters[3])
        return 0;
    // Use the strictly preceding node so the original integrator's final
    // partial step also matches when the endpoint lies exactly on a cache node.
    auto it = std::lower_bound(arcPositions.begin(), arcPositions.end(), endpoint);
    size_t index = size_t(it - arcPositions.begin()) - 1;
    float from = arcPositions[index], sum = arcLengths[index];
    while (from < endpoint) {
        float dx = std::min(ARC_STEP, float(endpoint - from));
        double s = slope(float(from + double(dx) / 2) - inrunParameters[3]);
        sum = float(sum + double(dx) * std::hypot(1.0, s));
        float next = from + dx;
        if (next <= from)
            break;
        from = next;
    }
    return sum;
}
double Hill::atDistance(double d) const {
    if (d <= 0)
        return 0;
    auto it = std::lower_bound(arcLengths.begin(), arcLengths.end(), d);
    if (it == arcLengths.end())
        return arcPositions.back() - inrunParameters[3];
    size_t upper = size_t(it - arcLengths.begin());
    if (!upper)
        return 0;
    double fraction = (d - arcLengths[upper - 1]) / (*it - arcLengths[upper - 1]);
    return arcPositions[upper - 1] + fraction * (arcPositions[upper] - arcPositions[upper - 1]) -
           inrunParameters[3];
}
double scoreStyle(std::array<double, 5> j) {
    std::sort(j.begin(), j.end());
    return j[1] + j[2] + j[3];
}
double distancePoints(const Hill &h, double d) {
    return (h.flying() ? 120 : 60) + (d - h.k) * h.pointsPerMeter();
}
Simulation::Simulation(int h, double w) {
    reset(h, w);
}
void Simulation::reset(int h, double w) {
    hillIndex = clamp(h, 0, int(hills().size()) - 1);
    baseWind = clamp(w, -5.0, 5.0);
    wind = baseWind;
    windSpeed = std::abs(wind);
    windAngle = wind >= 0 ? PI : 0;
    fixedWind = false;
    phase = Phase::Ready;
    originalStage = 1;
    originalCounter = 1;
    computerControlled = false;
    fastAdvance = false;
    ai = {};
    lastAiInput = {};
    outrunAirborne = outrunContact = false;
    fallBounceCount = startHoldTicks = 0;
    keyboardStartQueued = false;
    impactAssessment = {};
    landing = Landing::None;
    x = hill().start();
    y = hill().inrun(x);
    vx = vy = pitch = 0;
    groundAngle = float(std::atan(hill().inrunSlope(x)));
    pose.reset(groundAngle);
    elapsed = phaseTime = recordClock = tickClock = 0;
    countdown = 16;
    takeoffQuality = landingAge = instability = telemarkWidth = 0;
    takeoffDuration = flightDuration = 0;
    impulseX = impulseY = inrunWork = impactAngle = impactSpeed = 0;
    inrunStartY = y + hill().takeoffAltitude();
    inrunInitialSpeed = scalarSpeed = 0;
    desiredLandingDelay = .3;
    firstLandingButton = 0;
    jumped = crashed = false;
    pendingLaunch = false;
    contactPending = false;
    virtualLanding = false;
    flightStyle = {};
    result = {};
    result.judges.fill(20);
    queuedInput = {};
    randomState = 0x13f57u + uint32_t(hillIndex) * 7919u;
    OriginalRandom r(randomState);
    windModel.reset(windSpeed, windAngle, r);
    windModel.frameCounter = originalCounter;
    randomState = r.state;
    recordingSeed = randomState;
    recordingWeather = windModel;
    inputHistory.clear();
    recordingDistance = 0;
    frames.clear();
    frames.push_back(frame());
    previousRenderFrame = frame();
}
void Simulation::resetOriginal(int h, uint32_t weatherSeed, uint32_t jumpSeed) {
    OriginalRandom weatherRandom(weatherSeed);
    OriginalWind weather;
    weather.initialize(weatherRandom);
    resetWithWeather(h, weather, jumpSeed);
}
void Simulation::resetWithWeather(int h, const OriginalWind &weather, uint32_t jumpSeed) {
    OriginalWind continuedWeather = weather;
    const auto continuedAuxiliary = pose.auxiliary;
    reset(h, 0);
    // Athlete reset clears foot trims but retains the two global arm controls.
    pose.auxiliary = continuedAuxiliary;
    windModel = continuedWeather;
    // Athlete reset 0x2d255 starts DS4c50 at1 after weather initialization.
    windModel.frameCounter = originalCounter;
    windSpeed = windModel.speed;
    windAngle = windModel.angle;
    wind = baseWind = -windSpeed * std::cos(windAngle);
    // 0x2939d reseeds before per-jump AI initialization/athlete reset.
    randomState = jumpSeed;
    recordingSeed = jumpSeed;
    recordingWeather = windModel;
    frames.front() = frame();
    previousRenderFrame = frame();
}
void Simulation::configureAi(const OriginalAiSkills &skills, bool computer, bool fast) {
    OriginalRandom random(randomState);
    ai.initialize(skills, hill().k, random);
    randomState = random.state;
    recordingSeed = randomState;
    computerControlled = computer;
    fastAdvance = fast;
}
void Simulation::setWind(double speed, double angle) {
    fixedWind = true;
    windSpeed = float(speed);
    windAngle = float(angle);
    wind = -windSpeed * std::cos(windAngle);
    if (phase == Phase::Ready)
        baseWind = wind;
}
OriginalAirState Simulation::airState(double q) const {
    return {vx,           -vy, q, pose.footAngle1(), pose.footAngle2(), windSpeed, windAngle, pose.bodyFactor,
            ORIGINAL_MASS};
}
double Simulation::optimalPitch() const {
    // Modern guidance uses the minimum of original style cost (q+.35)^2,
    // close to the original AI's average target of -.38. Instantaneous lift
    // alone recommended excessive pitch and sacrificed speed over a flight.
    return -.35;
}
double Simulation::efficiency() const {
    return clamp(1 - (pitch + .35) * (pitch + .35), 0.0, 1.0);
}
Frame Simulation::frame() const {
    Frame f{float(x),       float(y),       float(vx),        float(vy),       float(pitch),
            float(elapsed), int32_t(phase), int32_t(landing), int32_t(crashed)};
    std::copy(pose.angles.begin(), pose.angles.end(), f.pose.begin());
    std::copy(pose.offsets.begin(), pose.offsets.end(), f.pose.begin() + 18);
    f.telemarkWidth = float(telemarkWidth);
    f.windSpeed = float(windSpeed);
    f.windAngle = float(displayWindAngle());
    f.leadingLeg = pose.leadingLeg;
    return f;
}
Frame interpolateFrame(const Frame &previous, const Frame &current, double alpha) {
    alpha = clamp(alpha, 0.0, 1.0);
    if (alpha >= 1)
        return current;
    if (alpha <= 0)
        return previous;
    Frame result = previous;
    auto blend = [alpha](float a, float b) { return float(double(a) + (double(b) - a) * alpha); };
    result.x = blend(previous.x, current.x);
    result.y = blend(previous.y, current.y);
    result.vx = blend(previous.vx, current.vx);
    result.vy = blend(previous.vy, current.vy);
    result.pitch = blend(previous.pitch, current.pitch);
    result.elapsed = blend(previous.elapsed, current.elapsed);
    for (size_t i = 0; i < result.pose.size(); ++i)
        result.pose[i] = blend(previous.pose[i], current.pose[i]);
    // Interpolate original joint endpoints as well as poses: re-evaluating
    // the original discrete acos table at each display frame would still pop.
    if (previous.pose[18] > 0 && previous.pose[20] > 0 && current.pose[18] > 0 && current.pose[20] > 0) {
        const auto a = originalSkeleton(previous.pose, previous.leadingLeg);
        const auto b = originalSkeleton(current.pose, current.leadingLeg);
        for (size_t i = 0; i < result.renderSkeleton.size(); ++i)
            result.renderSkeleton[i] = a.points[i] + (b.points[i] - a.points[i]) * alpha;
        result.hasRenderSkeleton = true;
    }
    result.telemarkWidth = blend(previous.telemarkWidth, current.telemarkWidth);
    result.windSpeed = blend(previous.windSpeed, current.windSpeed);
    result.windAngle = float(double(previous.windAngle) +
                             std::remainder(double(current.windAngle) - previous.windAngle, 2 * PI) * alpha);
    return result;
}
Frame Simulation::renderFrame(double remainingTime) const {
    if (originalCounter == 1)
        return frame();
    return interpolateFrame(previousRenderFrame, frame(), (tickClock + remainingTime) / ORIGINAL_TICK);
}
void Simulation::jump() {
    if (jumped)
        return;
    jumped = true;
    originalStage = 4;
}
void Simulation::touchDown() {
    auto state = originalLandingStateFromPose(pose);
    state.impactSpeed = impactSpeed;
    state.impactAngle = impactAngle;
    state.telemarkWidth = telemarkWidth;
    state.beforeTakeoff = x < 0;
    state.takeoffDuration = takeoffDuration;
    state.flightDuration = flightStyle.duration;
    state.flightErrorIntegral = flightStyle.integral;
    OriginalRandom r(randomState);
    // Source273bd..274b0 scores a contact at/before the board as zero
    // distance, with no style calculation or judge reveal draws.
    const bool beforeBoard = x <= 0;
    auto assessment = assessOriginalLanding(state, r, !beforeBoard);
    // Main279f6..27a77 schedules the five judge reveals after scoring.
    // Those draws also affect the following wind and fall evolution.
    if (!beforeBoard)
        for (int i = 0; i < 5; ++i)
            r.next(120);
    randomState = r.state;
    impactAssessment = assessment;
    if (!beforeBoard) {
        flightStyle.integral = assessment.normalizedFlightError;
        takeoffDuration = std::max(0.0, takeoffDuration);
    } else
        countdown = -1;
    crashed = assessment.crashed();
    const float measuredDistance = float(std::trunc(hill().distance(x - telemarkWidth) * 100) / 100);
    recordingDistance = beforeBoard ? 0 : measuredDistance;
    result.distance = beforeBoard ? 0 : std::trunc(double(measuredDistance) * 2) / 2;
    result.beforeTakeoff = beforeBoard;
    result.takeoff = takeoffQuality;
    result.landing = landing;
    result.telemarkWidth = telemarkWidth;
    result.crashKind = assessment.crashKind;
    result.crashed = crashed;
    result.judges = assessment.judges;
    result.stylePoints = assessment.stylePoints;
    result.distancePoints = float(distancePoints(hill(), result.distance));
    auto orderedJudges = result.judges;
    std::sort(orderedJudges.begin(), orderedJudges.end());
    float total = result.distancePoints;
    if (!beforeBoard)
        for (size_t i = 1; i < 4; ++i)
            total = float(double(total) + orderedJudges[i]);
    result.total = beforeBoard ? total : float(std::trunc(double(total) * 10) / 10);
    result.feedback =
        beforeBoard ? "Take off closer to the board. Contact before it receives no style marks."
        : crashed   ? (landing == Landing::None    ? "Prepare your landing before touching the snow."
                       : assessment.crashKind == 1 ? "Hard landing. Keep a narrower stance on a long flight."
                       : assessment.crashKind == 2 ? "Your telemark was too wide for this landing."
                                                   : "Prepare earlier so your skis and body can settle.")
        : landing == Landing::TwoFoot ? "Standing landing. A wider telemark earns better style marks."
                                      : "Beautiful telemark. Hold that flying position.";
    phase = Phase::Outrun;
    originalStage = 11;
    phaseTime = 0;
    originalInitializeImpact(pose, assessment, telemarkWidth, groundAngle);
    pose.bodyFactor = 1;
    pitch = 1;
    y = x <= 0 ? hill().inrun(x) : hill().ground(x);
}
void Simulation::controller(const Input &in) {
    bool landingStarted = false;
    bool freezeLanding = false;
    int mask = in.buttonMask;
    int buttons = mask < 0 ? 2 : int(bool(mask & 1)) + int(bool(mask & 2));
    flightStyle.accumulate(originalStage, (phase == Phase::Flight && !contactPending) || outrunAirborne,
                           pitch, pose, ORIGINAL_TICK);
    if (phase == Phase::Ready) {
        if (originalCounter % 70 == 35 && countdown > 0)
            --countdown;
        // The keyboard action supplies a virtual left press; physical input
        // uses the held left-button level, including through the start gate.
        if (in.start && mask <= 0)
            keyboardStartQueued = true;
        bool startPressed = (mask >= 0 && (mask & 1)) || keyboardStartQueued;
        if (countdown == 0) {
            phase = Phase::Disqualified;
            result.disqualified = true;
            result.distancePoints = float(distancePoints(hill(), 0));
            result.total = result.distancePoints;
            result.feedback = "The 15 second start window expired.";
        } else if (startPressed && originalCounter >= 65) {
            phase = Phase::Inrun;
            keyboardStartQueued = false;
            originalStage = 2;
            phaseTime = 0;
            scalarSpeed = inrunInitialSpeed = 1;
            vx = vy = 0;
            frames.clear();
            recordClock = 0;
        }
    }
    if (originalStage == 2) {
        ++startHoldTicks;
        if (startHoldTicks > 20 && mask <= 0)
            originalStage = 3;
    }
    if (originalStage == 3) {
        if (in.takeoff || mask > 0)
            jump();
    }
    if (originalStage == 4) {
        if (x < 0) {
            auto impulse = originalTakeoffDose({impulseX, impulseY}, pose.height(), groundAngle, buttons);
            impulseX = impulse.x;
            impulseY = impulse.y;
            takeoffDuration = float(takeoffDuration + buttons * ORIGINAL_TICK / 2);
        }
        if (pose.height() > 1.07) {
            if (x <= 0) {
                pendingLaunch = true;
                double slope = hill().inrunSlope(x / 2);
                takeoffDuration =
                    float(takeoffDuration + x * std::hypot(1.0, slope) / std::max(.0001, scalarSpeed));
            }
            originalStage = 5;
        }
    }
    if (originalStage == 5) {
        if (phase == Phase::Flight)
            originalStage = 6;
    }
    if (originalStage == 6) {
        if (mask <= 0)
            originalStage = 7;
    }
    if (originalStage == 6 || originalStage == 7) {
        OriginalRandom r(randomState);
        updateOriginalFlightInput(originalStage, pitch, in.pitchDelta, ORIGINAL_TICK, windSpeed, windAngle, r,
                                  pose);
        randomState = r.state;
        pose.flightTargets(pitch);
    }
    if (originalStage == 7 && (mask > 0 || in.land != Landing::None)) {
        landingStarted = true;
        originalStage = 8;
        pitch = .75;
        telemarkWidth = 0;
        landingAge = 0;
        desiredLandingDelay = in.land == Landing::TwoFoot ? ORIGINAL_TICK : clamp(in.landingDelay, 0.0, .6);
        virtualLanding = in.land != Landing::None;
        firstLandingButton = mask > 0 ? mask : 0;
        pose.leadingLeg = mask & 1 ? 1 : 0;
        landing = mask == 3 || in.land == Landing::TwoFoot ? Landing::TwoFoot : Landing::Telemark;
    }
    // Source 0x26d0a: preparation OR contact advances width once, including
    // frozen stage9 at contact. Source 0x26d36 clamps the stored value to .6.
    if (originalStage == 8 || (contactPending && originalStage != 12)) {
        telemarkWidth = float(telemarkWidth + ORIGINAL_TICK);
        if (telemarkWidth > .6) {
            telemarkWidth = .6f;
            originalStage = 9;
        }
        bool second = mask >= 0 && firstLandingButton && (mask & (3 ^ firstLandingButton));
        if (second || ((mask < 0 || virtualLanding) && telemarkWidth >= desiredLandingDelay) ||
            contactPending)
            freezeLanding = true;
        if (firstLandingButton == 3)
            freezeLanding = true;
    }
    if (phase == Phase::Flight) {
        flightDuration = flightStyle.duration;
        instability = flightStyle.integral;
        if (landing != Landing::None)
            landingAge += ORIGINAL_TICK;
    }
    // Original foot-pitch blend near the surface, without a per-hill range.
    auto surface = [&](double xx) { return xx < 0 ? hill().inrun(xx) : hill().ground(xx); };
    double ahead = surface(x - 1), here = surface(x), blend = 0;
    if (ahead > y && ahead != here)
        blend = (ahead - y) / (ahead - here);
    pose.prepare(originalStage, pitch, telemarkWidth, groundAngle,
                 (phase == Phase::Flight && !contactPending) || outrunAirborne, blend, !contactPending,
                 landingStarted);
    // 26d66 refreshes stage8 targets at the new width, then 26d8e freezes
    // them on the second button. The width-cap branch precedes that callback.
    if (freezeLanding) {
        originalStage = 9;
        pose.stage = 9;
    }
    if (contactPending) {
        touchDown();
        contactPending = false;
    }
    const double nearX = float(x + hill().inrunParameters[3] - 1) - hill().inrunParameters[3];
    originalNearSurfaceHeights(pose, originalStage, y, surface(nearX),
                               x <= 0 ? hill().inrunSlope(x) : hill().slope(x));
    if (phase == Phase::Outrun) {
        OriginalOutrunInput state;
        state.originalStage = originalStage;
        state.crashKind = impactAssessment.crashKind;
        state.roughness = impactAssessment.roughness;
        state.width = telemarkWidth;
        state.terrainAngle = groundAngle;
        state.terrainSlope = x <= 0 ? hill().inrunSlope(x) : hill().slope(x);
        double ahead = float(x + hill().inrunParameters[3] + 5) - hill().inrunParameters[3];
        state.aheadSlope = ahead <= 0 ? hill().inrunSlope(ahead) : hill().slope(ahead);
        state.vx = vx;
        state.vyDown = -vy;
        state.speed = scalarSpeed;
        state.impactAngle = impactAngle;
        state.airborne = outrunAirborne;
        state.contactEvent = outrunContact;
        state.fastAdvance = fastAdvance;
        state.fallBounceCount = fallBounceCount;
        state.existingImpulseX = impulseX;
        state.existingImpulseYUp = impulseY;
        OriginalRandom random(randomState);
        auto action = originalOutrunUpdate(pose, state, random);
        randomState = random.state;
        originalStage = action.originalStage;
        fallBounceCount = action.fallBounceCount;
        if (action.pitchChanged)
            pitch = action.pitch;
        if (action.pendingLaunch) {
            impulseX = action.impulseX;
            impulseY = action.impulseYUp;
            pendingLaunch = true;
        }
        outrunContact = false;
    }
    pose.advance(ORIGINAL_TICK);
    if (phase == Phase::Outrun && originalOutrunStopped(outrunAirborne, scalarSpeed, true)) {
        phase = Phase::Finished;
        phaseTime = 0;
    }
}
void Simulation::tick(const Input &in) {
    ++originalCounter;
    elapsed += ORIGINAL_TICK;
    phaseTime += ORIGINAL_TICK;
    if (!fixedWind) {
        OriginalRandom r(randomState);
        windModel.advance(originalCounter, r, ORIGINAL_TICK);
        randomState = r.state;
        windSpeed = windModel.speed;
        windAngle = windModel.angle;
        wind = -windSpeed * std::cos(windAngle);
    }
    double altitude = hill().takeoffAltitude();
    bool shouldLaunch = pendingLaunch;
    if (phase == Phase::Inrun || (phase == Phase::Outrun && !outrunAirborne)) {
        double beforeX = x, beforeY = y;
        OriginalInrunState state;
        state.x = x + hill().inrunParameters[3];
        state.y = y + altitude;
        state.speed = scalarSpeed;
        state.startY = inrunStartY;
        state.initialSpeed = inrunInitialSpeed;
        state.work = inrunWork;
        state.angle = groundAngle;
        state.windSpeed = windSpeed;
        state.windAngle = windAngle;
        state.bodyFactor = pose.bodyFactor;
        state.poseState = pose.mode;
        state.friction = x < 0 ? .02 : .05;
        auto next = originalInrunStep(state);
        x = next.x - hill().inrunParameters[3];
        scalarSpeed = next.speed;
        inrunWork = next.work;
        vx = next.vx;
        vy = -next.vyDown;
        // Source2d7e9 preserves height at the board crossing even after a
        // failed jump. A fallen skier can leave the board during the outrun.
        // A float32 integration step can land exactly at x=0. Include that
        // position when the following step leaves the board; otherwise a CPU
        // can remain in Inrun all the way through the landing slope.
        bool crossedBoard = beforeX <= 0 && x > 0;
        y = crossedBoard ? beforeY : x <= 0 ? hill().inrun(x) : hill().ground(x);
        double surface = x <= 0 ? hill().inrun(x) : hill().ground(x);
        bool aboveSurface = y > surface;
        if (aboveSurface && phase == Phase::Outrun && !outrunAirborne && crashed) {
            OriginalRandom random(randomState);
            originalRandomizeFallPose(pose, 3.4f, 2.5f, .5f, 2.2f, random);
            randomState = random.state;
        }
        shouldLaunch = crossedBoard || pendingLaunch || aboveSurface;
    } else if (phase == Phase::Flight || (phase == Phase::Outrun && outrunAirborne)) {
        auto next = originalFlightStep(x + hill().inrunParameters[3], y + altitude, airState(pitch));
        x = next.x - hill().inrunParameters[3];
        y = next.y - altitude;
        vx = next.vx;
        vy = -next.vyDown;
        double ground = x <= 0 ? hill().inrun(x) : hill().ground(x);
        if (y <= ground) {
            impactAngle = float(vx == 0 ? PI - groundAngle : std::abs(std::atan(vy / vx) - groundAngle));
            impactSpeed = float(std::hypot(vx, vy));
            scalarSpeed = inrunInitialSpeed = float(std::cos(impactAngle) * impactSpeed);
            inrunStartY = ground + altitude;
            inrunWork = 0;
            y = ground;
            if (phase == Phase::Flight)
                contactPending = true;
            else {
                outrunContact = true;
                outrunAirborne = false;
            }
        }
    }
    // Source2dc6e..2dced: pending impulses apply after both ground and air
    // integration, including a first fall trigger while already airborne.
    if (shouldLaunch) {
        vx = float(std::cos(-groundAngle) * scalarSpeed + impulseX / ORIGINAL_MASS);
        vy = -float(std::sin(-groundAngle) * scalarSpeed - impulseY / ORIGINAL_MASS);
        takeoffQuality = clamp(std::hypot(impulseX, impulseY) / 310.6, 0.0, 1.0);
        impulseX = impulseY = 0;
        pendingLaunch = false;
        if (phase == Phase::Inrun) {
            phase = Phase::Flight;
            phaseTime = 0;
        } else if (phase == Phase::Outrun)
            outrunAirborne = true;
    }
    if (phase == Phase::Inrun || phase == Phase::Flight || phase == Phase::Outrun)
        groundAngle = float(std::atan(x <= 0 ? hill().inrunSlope(x) : hill().slope(x)));
    Input controls = in;
    if (computerControlled) {
        OriginalAiObservation observation;
        observation.tick = originalCounter;
        observation.stage = originalStage;
        observation.q = pitch;
        observation.telemarkWidth = telemarkWidth;
        observation.distanceToTakeoff = -x;
        observation.speed = scalarSpeed;
        observation.motionAngle = groundAngle;
        observation.vx = vx;
        observation.vyDown = -vy;
        observation.clearance = y - hill().ground(x);
        observation.landingSlope = hill().slope(x);
        double ahead = float(x + hill().inrunParameters[3] + 5) - hill().inrunParameters[3];
        observation.terrainAngleAhead =
            float(std::atan(ahead <= 0 ? hill().inrunSlope(ahead) : hill().slope(ahead)));
        auto mouse = ai.poll(observation, OriginalRandom(randomState), ORIGINAL_TICK);
        lastAiInput = mouse;
        controls = {};
        controls.buttonMask = mouse.buttonMask;
        controls.start = phase == Phase::Ready && (mouse.buttonMask & 1);
        controls.pitchDelta = originalMousePitchDelta(mouse.mouseDy, ORIGINAL_TICK);
    }
    ReplayTick recorded;
    recorded.counter = originalCounter;
    recorded.stageBefore = originalStage;
    const double count = -controls.pitchDelta / (double(ORIGINAL_TICK) * 1.5);
    recorded.mouseDy = int(std::clamp(std::round(count), -128.0, 127.0));
    recorded.representable = std::abs(count - recorded.mouseDy) < 1e-7;
    recorded.buttonMask = std::max(0, controls.buttonMask);
    controller(controls);
    recorded.stageAfter = originalStage;
    if (controls.buttonMask < 0) {
        if (recorded.stageBefore == 1 && recorded.stageAfter == 2)
            recorded.buttonMask = 1;
        else if (recorded.stageAfter == 4)
            recorded.buttonMask = 3;
        else if (recorded.stageBefore == 7 && recorded.stageAfter >= 8)
            recorded.buttonMask = controls.land == Landing::TwoFoot ? 3 : (pose.leadingLeg ? 1 : 2);
        else if (recorded.stageBefore == 8)
            recorded.buttonMask = recorded.stageAfter == 9 ? 3 : (pose.leadingLeg ? 1 : 2);
    }
    if (inputHistory.size() < 50000)
        inputHistory.push_back(recorded);
    if (phase != Phase::Ready && phase != Phase::Disqualified) {
        if (frames.empty())
            frames.push_back(frame());
        recordClock += ORIGINAL_TICK;
        if (recordClock >= 1.0 / 60 || phase == Phase::Finished) {
            recordClock -= 1.0 / 60;
            frames.push_back(frame());
        }
    }
}
void Simulation::step(double dt, const Input &in) {
    if (phase == Phase::Finished || phase == Phase::Disqualified)
        return;
    queuedInput.start |= in.start;
    queuedInput.takeoff |= in.takeoff;
    queuedInput.pitchDelta += in.pitchDelta;
    queuedInput.mouseDy =
        int(clamp(int64_t(queuedInput.mouseDy) + in.mouseDy, int64_t(-1000000), int64_t(1000000)));
    queuedInput.mouseSensitivity = in.mouseSensitivity;
    queuedInput.buttonMask = in.buttonMask;
    if (in.land != Landing::None) {
        queuedInput.land = in.land;
        queuedInput.landingDelay = in.landingDelay;
    }
    tickClock += clamp(dt, 0.0, .025);
    while (tickClock + 1e-12 >= ORIGINAL_TICK) {
        tickClock -= ORIGINAL_TICK;
        Input input = queuedInput;
        input.pitchDelta += originalMousePitchDelta(input.mouseDy, ORIGINAL_TICK) * input.mouseSensitivity;
        queuedInput.start = queuedInput.takeoff = false;
        queuedInput.pitchDelta = 0;
        queuedInput.mouseDy = 0;
        queuedInput.land = Landing::None;
        previousRenderFrame = frame();
        tick(input);
        if (phase == Phase::Finished || phase == Phase::Disqualified)
            break;
    }
}
Input autopilot(const Simulation &s) {
    Input in;
    in.start = s.phase == Phase::Ready;
    in.takeoff = s.originalStage == 3 && s.x > -std::max(1.0, s.vx) * .21;
    if (s.phase == Phase::Flight && (s.originalStage == 6 || s.originalStage == 7)) {
        in.pitchDelta = clamp(s.optimalPitch() - s.pitch, -.012, .012);
        double approach = -(s.vy - s.hill().slope(s.x) * s.vx);
        if (s.originalStage == 7 && approach > 0 && s.clearance() < approach * .65 && s.phaseTime > .4) {
            in.land = Landing::Telemark;
            in.landingDelay = .3;
        }
    }
    return in;
}
} // namespace sjp
