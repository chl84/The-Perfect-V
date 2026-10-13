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
    sjp::gfx::vertex3d(p.x, p.y, p.z);
}
// Soft, untextured billboards: the centre carries the opacity, the shared
// outside ring is transparent. Fixed small meshes remain sharp when close.
void powder(Vec3 center, Vec3 right, Vec3 up, double radius, double opacity, Vec3 color) {
    if (opacity <= .001)
        return;
    sjp::gfx::begin(sjp::gfx::TRIANGLE_FAN);
    sjp::gfx::color4f(float(color.x), float(color.y), float(color.z), float(opacity));
    vertex(center);
    sjp::gfx::color4f(float(color.x), float(color.y), float(color.z), 0);
    for (int corner = 0; corner <= 6; ++corner) {
        const double angle = corner * PI / 3;
        vertex(center + right * (std::cos(angle) * radius) + up * (std::sin(angle) * radius));
    }
    sjp::gfx::end();
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

void Renderer::setJumpWeather(uint32_t seed, environment::Selection selection) {
    setEnvironment(environment::resolve(selection, seed), true);
}
void Renderer::setEnvironment(environment::Conditions conditions, bool restart) {
    if (!environment::valid(conditions))
        conditions = {};
    if (!restart && sceneEnvironment == conditions)
        return;
    sceneEnvironment = conditions;
    lighting = visual::environmentLight(conditions);
    snowfallSeed = visual::weatherHash(conditions.seed);
    snowfall = conditions.weather == environment::Weather::Snowfall;
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
    const Vec3 powderColor = lighting.moon ? Vec3{.56, .66, .82} : Vec3{.91, .97, 1};
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

    sjp::gfx::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::TEXTURE_BIT |
                 sjp::gfx::LINE_BIT | sjp::gfx::POLYGON_BIT);
    sjp::gfx::disable(sjp::gfx::LIGHTING);
    sjp::gfx::disable(sjp::gfx::TEXTURE_2D);
    sjp::gfx::disable(sjp::gfx::CULL_FACE);
    sjp::gfx::enable(sjp::gfx::BLEND);
    sjp::gfx::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::gfx::depthMask(sjp::gfx::FALSE);
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
        powder(position, right, up, particle.size * (1 + life * 2.4), .62 * std::pow(1 - life, 1.2), powderColor);
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
                powder({x, y, z}, right, up, .035 + life * .14, strength * .33 * (1 - life), powderColor);
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
                    powder({x, y, z}, right, up, radius, motion * contact * fade * (cloud ? .27 : .58), powderColor);
                }
            }
    }

    if (phase == Phase::Flight) {
        // Relative-air direction uses the existing wind vector and velocity.
        // These short, faint streaks stay outside the body silhouette.
        const Vec3 airflow = (wind * .35 - Vec3{f.vx, f.vy, 0}).normalized();
        const double strength = clamp(std::hypot(double(f.vx), double(f.vy)) / 45, 0.0, 1.0);
        sjp::gfx::lineWidth(1);
        sjp::gfx::begin(sjp::gfx::LINES);
        for (int i = 0; i < 13; ++i) {
            const double seed = 1710 + i * 7;
            const double cycle = std::fmod(clock * (1.4 + pattern(seed) * .4) + pattern(seed + 1), 1.0);
            const double fade = std::sin(cycle * PI);
            const Vec3 position = origin + Vec3{1.6 - cycle * 4.3, .12 + pattern(seed + 2) * 1.55,
                                                (i % 2 ? -1 : 1) * (.65 + pattern(seed + 3) * .65)};
            sjp::gfx::color4f(.85f, .95f, 1, float(.11 * strength * fade));
            vertex(position);
            sjp::gfx::color4f(.85f, .95f, 1, 0);
            vertex(position + airflow * (.25 + pattern(seed + 4) * .45));
        }
        sjp::gfx::end();
    }

    // Select weather once per jump, never per frame or camera. World-space
    // cells keep the light snowfall independent of the athlete and physics.
    // The replay clock freezes flakes on pause and makes seeking repeatable.
    if (snowfall && showWeather) {
        const uint32_t venue = venuePattern(h.id) ^ snowfallSeed;
        constexpr double cellSize = 8;
        // A compact 3D volume covers the view at the gate, in flight and in
        // every camera. Its world-space cells never move with the athlete.
        // At most 5 * 5 * 3 * 3 = 225 flakes are considered; only visible ones
        // reach the existing billboard batch. No textures or emitted state.
        const Vec3 center = (eye + target) * .5;
        const int cellX = int(std::floor(center.x / cellSize)),
                  cellY = int(std::floor(center.y / cellSize)),
                  cellZ = int(std::floor(center.z / cellSize));
        const double halfFov = std::tan(48 * PI / 360), aspect = double(width) / height;
        for (int xCell = cellX - 2; xCell <= cellX + 2; ++xCell)
            for (int zCell = cellZ - 2; zCell <= cellZ + 2; ++zCell)
                for (int yCell = cellY - 1; yCell <= cellY + 1; ++yCell)
                    for (int i = 0; i < 3; ++i) {
                        const double seed = double(venue % 997) + xCell * 197 + zCell * 71 + yCell * 311 + i * 23;
                        const double rate = .13 + pattern(seed) * .075;
                        const double cycle = std::fmod(std::max(0.0, clock) * rate + pattern(seed + 1), 1.0);
                        const Vec3 drift = visual::snowDrift(wind, cycle / rate);
                        const Vec3 position{xCell * cellSize + pattern(seed + 2) * cellSize + drift.x +
                                                .18 * std::sin(clock * .8 + seed),
                                            yCell * cellSize + (1 - cycle) * cellSize + drift.y,
                                            zCell * cellSize + pattern(seed + 3) * cellSize +
                                                .15 * std::sin(clock * .65 + seed * 1.7)};
                        if (position.y < snowSurface(position.x) + .04)
                            continue;
                        const Vec3 relative = position - eye;
                        const double depth = dot(relative, forward);
                        if (depth <= 1 || std::abs(dot(relative, right)) > depth * halfFov * aspect + .2 ||
                            std::abs(dot(relative, up)) > depth * halfFov + .2)
                            continue;
                        // Fade at continuous volume boundaries, so crossing a
                        // cell boundary or changing camera cannot pop flakes in.
                        const double edgeFade = clamp((16 - std::abs(position.x - center.x)) / 5, 0.0, 1.0) *
                                                clamp((16 - std::abs(position.z - center.z)) / 5, 0.0, 1.0) *
                                                clamp((8 - std::abs(position.y - center.y)) / 3, 0.0, 1.0);
                        const double lifeFade = clamp(cycle / .12, 0.0, 1.0) * clamp((1 - cycle) / .12, 0.0, 1.0);
                        const double nearFade = clamp((depth - 1) / 1.5, 0.0, 1.0);
                        // Keep distant flakes readable and cap nearby ones at
                        // a small screen footprint, avoiding giant white dots.
                        const double radius = std::min(depth * .0035, std::max(.055 + pattern(seed + 4) * .075,
                                                                            depth * .0013));
                        powder(position, right, up, radius,
                               (.8 + pattern(seed + 5) * .15) * edgeFade * lifeFade * nearFade, powderColor);
                    }
    }
    sjp::gfx::popAttrib();
}
} // namespace sjp
