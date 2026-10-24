#pragma once
#include "visual_snow_mesh.h"

namespace sjp::visual {
inline double preparedSnowWeight(const Hill &h, double x, double z) {
    const double side = clamp((25*h.scale()-std::abs(z))/(8*h.scale()),0.,1.);
    const double end = h.outrunBottom()+45+2*std::sin(z*.13);
    const double t = clamp((end-x)/33,0.,1.);
    return side*t*t*(3-2*t);
}
// Every vertex of every receiver triangle touching this conservative interior
// has prepared alpha 1, including both the upstream and outrun fade boundaries.
inline std::array<float,4> opaquePreparedCoverage(const Hill &h) {
    const double s = h.scale();
    return {float(10*s-.001),float(h.outrunBottom()+10-6*s),float(snowGridStart(h)+8*s),1};
}
} // namespace sjp::visual
