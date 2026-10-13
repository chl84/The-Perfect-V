#pragma once
#include "environment.h"
#include "physics.h"
#include <algorithm>

namespace sjp::visual {
struct EnvironmentLight {
    Vec3 skyTop, skyHorizon, fog, ambient, direct, fill, direction;
    double fogStart = 170, fogEnd = 1050;
    double shadowStrength = .38, shadowSoftness = 1;
    double celestialVisibility = 1;
    bool moon = false;
};
inline Vec3 mix(Vec3 a, Vec3 b, double t) {
    return a + (b - a) * t;
}
inline EnvironmentLight environmentLight(environment::Conditions conditions) {
    using environment::TimeOfDay;
    using environment::Weather;
    EnvironmentLight light;
    switch (conditions.time) {
    case TimeOfDay::Morning:
        light = {{.31, .47, .64},
                 {.91, .77, .63},
                 {.78, .81, .83},
                 {.29, .33, .39},
                 {.85, .68, .48},
                 {.12, .18, .25},
                 {.65, .40, -.70},
                 155,
                 900,
                 .33,
                 1.2,
                 1,
                 false};
        break;
    case TimeOfDay::Evening:
        light = {{.19, .27, .43},
                 {.90, .58, .39},
                 {.61, .61, .70},
                 {.28, .28, .38},
                 {.85, .51, .29},
                 {.12, .17, .26},
                 {-.70, .34, -.50},
                 130,
                 850,
                 .30,
                 1.3,
                 1,
                 false};
        break;
    case TimeOfDay::Night:
        // Local mast lighting supplies the arena's warm light at night.
        light = {{.018, .033, .069},
                 {.085, .13, .21},
                 {.07, .11, .17},
                 {.13, .18, .27},
                 {.22, .28, .40},
                 {.09, .11, .15},
                 {.35, .70, -.60},
                 140,
                 800,
                 .16,
                 1.5,
                 1,
                 true};
        break;
    default:
        light = {{.41, .61, .77},
                 {.79, .86, .88},
                 {.79, .86, .88},
                 {.305, .365, .432},
                 {.78, .73, .64},
                 {.13, .19, .24},
                 {-.50, 1, .625},
                 170,
                 1050,
                 .38,
                 1,
                 1,
                 false};
        break;
    }
    const bool night = light.moon;
    double clouds = 0;
    if (conditions.weather == Weather::Overcast)
        clouds = .92;
    else if (conditions.weather == Weather::Snowfall)
        clouds = .98;
    else if (conditions.weather == Weather::Fog)
        clouds = .88;
    if (clouds > 0) {
        const Vec3 cloudyTop = night ? Vec3{.035, .055, .085} : Vec3{.51, .57, .62};
        const Vec3 cloudyHorizon = night ? Vec3{.15, .18, .22} : Vec3{.75, .79, .81};
        light.skyTop = mix(light.skyTop, cloudyTop, clouds);
        light.skyHorizon = mix(light.skyHorizon, cloudyHorizon, clouds);
        light.fog = mix(light.fog, cloudyHorizon, clouds);
        light.ambient = mix(light.ambient, night ? Vec3{.17, .21, .27} : Vec3{.53, .57, .61}, clouds);
        light.direct = light.direct * (1 - clouds * .93);
        light.shadowStrength *= 1 - clouds * .94;
        light.shadowSoftness += clouds * 2;
        light.celestialVisibility = 0;
    }
    if (conditions.weather == Weather::Snowfall) {
        light.fogStart = 75;
        light.fogEnd = 470;
    } else if (conditions.weather == Weather::Fog) {
        light.fogStart = 22;
        light.fogEnd = 210;
    } else if (conditions.weather == Weather::Overcast) {
        light.fogStart = 125;
        light.fogEnd = 800;
    }
    light.direction = light.direction.normalized();
    return light;
}
} // namespace sjp::visual
