#include "original_skeleton.h"
#include "renderer.h"
#include "visual_weather.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sjp {
namespace {
// A fixed spatial pattern, independent of every gameplay random state.
// There is deliberately no RNG, physics advance, or input polling here.
double pattern(double index) {
    const double value = std::sin(index * 12.9898 + 4.1414) * 43758.5453;
    return value - std::floor(value);
}
uint32_t venuePattern(const std::string &id) {
    uint32_t value = 2166136261U;
    for (unsigned char character : id)
        value = (value ^ character) * 16777619U;
    return value;
}
void vertex(Vec3 p) {
    glVertex3d(p.x, p.y, p.z);
}
// Soft, untextured billboards: the centre carries the opacity, the shared
// outside ring is transparent. Fixed small meshes remain sharp when close.
void powder(Vec3 center, Vec3 right, Vec3 up, double radius, double opacity) {
    if (opacity <= .001)
        return;
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(.91f, .97f, 1, float(opacity));
    vertex(center);
    glColor4f(.91f, .97f, 1, 0);
    for (int corner = 0; corner <= 6; ++corner) {
        const double angle = corner * PI / 3;
        vertex(center + right * (std::cos(angle) * radius) + up * (std::sin(angle) * radius));
    }
    glEnd();
}
OriginalSkeleton effectSkeleton(const Hill &h, const Frame &f) {
    if (f.hasRenderSkeleton)
        return OriginalSkeleton{f.renderSkeleton};
    auto pose = f.pose;
    if (pose[18] == 0 && pose[20] == 0) {
        OriginalPose fallback;
        fallback.reset(std::atan(f.x <= 0 ? h.inrunSlope(f.x) : h.slope(f.x)));
        std::copy(fallback.angles.begin(), fallback.angles.end(), pose.begin());
        std::copy(fallback.offsets.begin(), fallback.offsets.end(), pose.begin() + 18);
    }
    return originalSkeleton(pose, f.leadingLeg);
}
} // namespace

void Renderer::setJumpWeather(uint32_t seed) {
    snowfallSeed = visual::weatherHash(seed);
    snowfall = visual::snowForJump(seed);
    snowParticles.clear();
    effectsInitialized = false;
}

void Renderer::effects(const Hill &h, const Frame &f, double, bool showWeather) {
    const Phase phase = Phase(f.phase), previousPhase = Phase(lastEffectFrame.phase);
    // Event particles use the frame/replay clock: pause freezes powder, and
    // seeking/restarting cannot carry a previous jump's burst into this one.
    const double clock = f.elapsed;
    const double delta = clock - lastEffectFrame.elapsed;
    const bool jumpRestart =
        (phase == Phase::Ready && previousPhase != Phase::Ready) || (f.x + 12 < lastEffectFrame.x);
    const bool discontinuity =
        !effectsInitialized || effectHill != h.id || delta < -.0001 || delta > .5 || jumpRestart;
    if (discontinuity) {
        snowParticles.clear();
        effectHill = h.id;
        effectsInitialized = true;
    }
    const double windSpeed = clamp(double(f.windSpeed), 0.0, 18.0);
    const Vec3 wind = visual::windDirection(f) * windSpeed;
    const OriginalSkeleton skeleton = effectSkeleton(h, f);
    const Vec3 origin{f.x, f.y, 0};
    const auto snowSurface = [&](double x) { return x <= 0 ? h.inrun(x) : h.ground(x); };
    auto skiContact = [&](size_t leg) {
        const OriginalJoint joint = leg == 0 ? OriginalJoint::Ankle1 : OriginalJoint::Ankle2;
        const Vec3 ankle = skeleton[joint] + origin;
        return Vec3{ankle.x, snowSurface(ankle.x) + .07, ankle.z};
    };
    snowParticles.erase(std::remove_if(snowParticles.begin(), snowParticles.end(),
                                       [&](const VisualParticle &p) {
                                           return clock < p.born || clock - p.born > p.lifetime;
                                       }),
                        snowParticles.end());
    auto burst = [&](Vec3 location, int count, double power, double seed, double born) {
        constexpr size_t particleBudget = 192;
        for (int i = 0; i < count && snowParticles.size() < particleBudget; ++i) {
            const double a = pattern(seed + i * 3), b = pattern(seed + i * 3 + 1),
                         c = pattern(seed + i * 3 + 2);
            const Vec3 spread{(a - .5) * .75, .025 + b * .1, (c - .5) * .55};
            const Vec3 velocity{(a - .65) * power * 1.45 + wind.x * .15, (.3 + b * .8) * power + wind.y * .07,
                                (c - .5) * power * 1.9};
            snowParticles.push_back({location + spread, velocity, born, .46 + b * .55, .028 + c * .09});
        }
    };
    if (!discontinuity && delta > .00001) {
        const bool passedTakeoff =
            previousPhase == Phase::Inrun &&
            (phase == Phase::Flight || phase == Phase::Outrun || phase == Phase::Finished);
        if (passedTakeoff) {
            // A slow display may skip the first airborne frame. The puff still
            // originates at the existing takeoff lip, never at the new skier.
            const double travel = std::max(.0001, double(f.x) - lastEffectFrame.x);
            const double fraction = clamp(-double(lastEffectFrame.x) / travel, 0.0, 1.0);
            burst({0, h.inrun(0) + .08, 0}, 38, 1.1, 71, lastEffectFrame.elapsed + delta * fraction);
        }
        const bool passedLanding = (previousPhase == Phase::Flight || previousPhase == Phase::Inrun) &&
                                   (phase == Phase::Outrun || phase == Phase::Finished);
        const bool groundFall = f.crashed && !lastEffectFrame.crashed && phase == Phase::Outrun &&
                                std::abs(f.y - snowSurface(f.x)) < 2;
        if (passedLanding || groundFall) {
            const double power = clamp(std::hypot(double(f.vx), double(f.vy)) / 19, .85, 2.1);
            for (size_t leg = 0; leg < 2; ++leg)
                burst(skiContact(leg), f.crashed ? 45 : 32, power, 133 + leg * 157, clock);
        }
    }
    lastEffectFrame = f;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_TEXTURE_BIT |
                 GL_LINE_BIT | GL_POLYGON_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    const Vec3 forward = (target - eye).normalized(), right = cross(forward, {0, 1, 0}).normalized(),
               up = cross(right, forward).normalized();
    for (const VisualParticle &particle : snowParticles) {
        const double age = clock - particle.born, life = age / particle.lifetime;
        if (life < 0 || life > 1)
            continue;
        const Vec3 position = particle.origin + particle.velocity * age + Vec3{0, -2.4 * age * age, 0};
        const double surface = snowSurface(position.x);
        if (position.y < surface + .015)
            continue;
        powder(position, right, up, particle.size * (1 + life * 2.4), .62 * std::pow(1 - life, 1.2));
    }

    // Ongoing ski powder is analytic rather than emitted per render call.
    // Identical replay timestamps produce identical flakes at any display rate.
    if (phase == Phase::Outrun && std::hypot(f.vx, f.vy) > 2 && std::abs(f.y - snowSurface(f.x)) < 1.4) {
        const double speed = std::hypot(f.vx, f.vy), strength = clamp(speed / 29, .0, 1.0);
        for (size_t leg = 0; leg < 2; ++leg)
            for (int i = 0; i < 24; ++i) {
                const double seed = 900 + leg * 79 + i * 5;
                const double period = .32 + pattern(seed) * .32;
                const double age = std::fmod(clock + pattern(seed + 1) * period, period);
                const double life = age / period;
                const Vec3 contact = skiContact(leg);
                const double x = contact.x - double(f.vx) * age + wind.x * age * .15 - age * 1.4;
                const double y = snowSurface(x) + .04 + age * (1.15 + pattern(seed + 2)) - age * age * 2;
                const double z = contact.z + (pattern(seed + 3) - .5) * age * 3.4;
                powder({x, y, z}, right, up, .035 + life * .14, strength * .33 * (1 - life));
            }
    }

    // A fallen skier ploughs snow with the body as well as the skis. These
    // short trails are evaluated from the displayed skeleton and timestamp,
    // with a fixed 7 * 18 particle ceiling and no per-frame emissions/cache.
    // Contact fades out as a body part lifts during a bounce; an airborne fall
    // cannot create a continuous shower of snow from below the skier.
    if (f.crashed && (phase == Phase::Outrun || phase == Phase::Finished)) {
        const double speed = std::hypot(double(f.vx), double(f.vy));
        const double motion = clamp((speed - 1) / 17, 0.0, 1.0);
        struct ContactPart {
            OriginalJoint joint;
            double radius;
        };
        constexpr std::array<ContactPart, 7> parts{{
            {OriginalJoint::HipCenter, .18},
            {OriginalJoint::ShoulderCenter, .21},
            {OriginalJoint::Head, .20},
            {OriginalJoint::Knee1, .13},
            {OriginalJoint::Knee2, .13},
            {OriginalJoint::Elbow1, .10},
            {OriginalJoint::Elbow2, .10},
        }};
        if (motion > .001)
            for (size_t part = 0; part < parts.size(); ++part) {
                const Vec3 body = skeleton[parts[part].joint] + origin;
                const double slope = body.x <= 0 ? h.inrunSlope(body.x) : h.slope(body.x);
                const double gap = (body.y - snowSurface(body.x)) / std::sqrt(1 + slope * slope);
                const double contact = clamp((parts[part].radius + .25 - gap) / .25, 0.0, 1.0) *
                                       clamp((gap + parts[part].radius + .3) / .25, 0.0, 1.0);
                if (contact <= .001)
                    continue;
                for (int i = 0; i < 18; ++i) {
                    const double seed = 2480 + part * 109 + i * 7;
                    const bool cloud = i < 6;
                    const double lifetime = cloud ? .40 + pattern(seed) * .28 : .24 + pattern(seed) * .38;
                    const double age = std::fmod(clock + pattern(seed + 1) * lifetime, lifetime);
                    const double life = age / lifetime;
                    const double x = body.x - double(f.vx) * age * .8 + wind.x * age * .12 +
                                     (pattern(seed + 2) - .5) * (.35 + age * 1.8);
                    const double lift = (cloud ? .72 : 1.6) + pattern(seed + 3) * (cloud ? 1.1 : 1.8);
                    const double y =
                        snowSurface(x) + .065 + age * lift - age * age * 2.1 + wind.y * age * .04;
                    const double z = body.z + (pattern(seed + 4) - .5) * (.6 + age * 3.8);
                    if (y < snowSurface(x) + .015)
                        continue;
                    const double radius = cloud ? .16 + life * .38 + motion * .09
                                                : .026 + pattern(seed + 5) * .066 + life * .025;
                    const double fade =
                        cloud ? std::sin(life * PI) * (1 - life * .35) : std::pow(1 - life, 1.2);
                    powder({x, y, z}, right, up, radius, motion * contact * fade * (cloud ? .27 : .58));
                }
            }
    }

    if (phase == Phase::Flight) {
        // Relative-air direction uses the existing wind vector and velocity.
        // These short, faint streaks stay outside the body silhouette.
        const Vec3 airflow = (wind * .35 - Vec3{f.vx, f.vy, 0}).normalized();
        const double strength = clamp(std::hypot(double(f.vx), double(f.vy)) / 45, 0.0, 1.0);
        glLineWidth(1);
        glBegin(GL_LINES);
        for (int i = 0; i < 13; ++i) {
            const double seed = 1710 + i * 7;
            const double cycle = std::fmod(clock * (1.4 + pattern(seed) * .4) + pattern(seed + 1), 1.0);
            const double fade = std::sin(cycle * PI);
            const Vec3 position = origin + Vec3{1.6 - cycle * 4.3, .12 + pattern(seed + 2) * 1.55,
                                                (i % 2 ? -1 : 1) * (.65 + pattern(seed + 3) * .65)};
            glColor4f(.85f, .95f, 1, float(.11 * strength * fade));
            vertex(position);
            glColor4f(.85f, .95f, 1, 0);
            vertex(position + airflow * (.25 + pattern(seed + 4) * .45));
        }
        glEnd();
    }

    // Select weather once per jump, never per frame or camera. World-space
    // cells keep the light snowfall independent of the athlete and physics.
    // The replay clock freezes flakes on pause and makes seeking repeatable.
    if (snowfall && showWeather) {
        const uint32_t venue = venuePattern(h.id) ^ snowfallSeed;
        const int cellX = int(std::floor(target.x / 18)), cellZ = int(std::floor(target.z / 18));
        for (int xCell = cellX - 2; xCell <= cellX + 2; ++xCell)
            for (int zCell = cellZ - 2; zCell <= cellZ + 2; ++zCell)
                for (int i = 0; i < 4; ++i) {
                    const double seed = double(venue % 997) + xCell * 197 + zCell * 71 + i * 23;
                    const double rate = .043 + pattern(seed) * .02;
                    const double cycle = std::fmod(std::max(0.0, clock) * rate + pattern(seed + 1), 1.0);
                    const double driftClock = cycle / rate;
                    const double baseX = xCell * 18 + pattern(seed + 2) * 18;
                    const Vec3 drift = visual::snowDrift(wind, driftClock);
                    const double x = baseX + drift.x, z = zCell * 18 + pattern(seed + 3) * 18;
                    const double y = snowSurface(baseX) + 4 + (1 - cycle) * 27 + drift.y;
                    const Vec3 position{x, y, z};
                    const double distance = (position - eye).length();
                    const double nearFade = clamp((distance - 3) / 5, 0.0, 1.0);
                    powder(position, right, up, .05 + pattern(seed + 4) * .035,
                           .56 * nearFade * std::sin(cycle * PI));
                }
    }
    glPopAttrib();
}
} // namespace sjp
