#pragma once
#include "visual_environment.h"

namespace sjp::visual {
inline float snowGlitterSunlight(const EnvironmentLight &light) {
    // Moon/floodlights do not activate solar glints. Weak diffuse sunlight
    // through cloud/fog stays below the threshold for distinct ice reflections.
    return light.moon ? 0.f : float(clamp((light.direct.length() - .25) / .75, 0.0, 1.0));
}
} // namespace sjp::visual
