#include "renderer.h"
#include "render_batch.h"
#include "visual_start_signal.h"
#include "visual_snow_accumulation.h"
#include <algorithm>

namespace sjp {
namespace {
constexpr Color steel{.34f, .41f, .44f, 1}, brace{.45f, .51f, .53f, 1}, timber{.43f, .35f, .28f, 1};
void vertex(Vec3 p) { visual::vertex(p.x, p.y, p.z); }
void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    visual::color(color.r, color.g, color.b, color.a);
    const Vec3 n = cross(b - a, c - a).normalized();
    visual::begin(gfx::QUADS);
    visual::normal(n.x, n.y, n.z);
    for (Vec3 p : {a, b, c, d})
        vertex(p);
    visual::end();
}
void box(Vec3 p, Vec3 size, Color color) {
    const Vec3 a = p - size * .5, b = p + size * .5;
    quad({a.x, b.y, a.z}, {a.x, b.y, b.z}, {b.x, b.y, b.z}, {b.x, b.y, a.z}, color);
    quad({a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z}, color);
    quad({a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}, color);
    quad({b.x, a.y, a.z}, {a.x, a.y, a.z}, {a.x, b.y, a.z}, {b.x, b.y, a.z}, color);
    quad({a.x, a.y, a.z}, {a.x, a.y, b.z}, {a.x, b.y, b.z}, {a.x, b.y, a.z}, color);
    quad({b.x, a.y, b.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, color);
}
void beam(Vec3 a, Vec3 b, double radius, Color color) {
    const Vec3 dir = (b - a).normalized();
    const Vec3 axis = std::abs(dir.z) > .95 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    const Vec3 right = cross(dir, axis).normalized() * radius, up = cross(right, dir).normalized() * radius;
    quad(a + right + up, b + right + up, b - right + up, a - right + up, color);
    quad(a - right - up, b - right - up, b + right - up, a + right - up, color);
    quad(a - right + up, b - right + up, b - right - up, a - right - up, color);
    quad(a + right - up, b + right - up, b + right + up, a + right + up, color);
}
double level(const visual::InrunSupport &s, double t) {
    return s.footingTop + .07 + (s.height() - .07) * t;
}
void accessStairs(const Hill &h, int side, visual::SnowAccumulation &snowCaps) {
    const visual::CameraSurface snow(h);
    const double x = -1.45, top = h.inrun(x) - .04;
    const double ground = snow.height(x, side * (visual::inrunWalkOuter + 1.5));
    const double height = top - ground;
    if (height < .25)
        return;
    const int steps = int(std::ceil(height / .19));
    const double run = .27, rise = height / steps, end = visual::inrunWalkOuter + steps * run;
    for (int step = 0; step < steps; ++step) {
        const double z = side * (visual::inrunWalkOuter + (step + .5) * run), y = top - step * rise;
        box({x, y - .05, z}, {1.05, .10, run}, timber);
        if (step % 4 == 0)
            for (double xx : {x - .57, x + .57})
                beam({xx, y - .035, z}, {xx, y + .98, z}, .028, brace);
    }
    box({x, ground - .06, side * (end + .12)}, {1.4, .26, .60}, {.49f, .52f, .53f, 1});
    snowCaps.footing(h, x, side * (end + .12), .7, .3, ground + .07, x + end * 7);
    for (double xx : {x - .47, x + .47})
        beam({xx, top - .14, side * visual::inrunWalkOuter}, {xx, ground - .16, side * end}, .065, steel);
    for (double xx : {x - .57, x + .57}) {
        beam({xx, top + 1.01, side * visual::inrunWalkOuter}, {xx, ground + 1.01, side * end}, .028, brace);
        beam({xx, ground, side * end}, {xx, ground + 1.01, side * end}, .028, brace);
    }
}
void lightHousing(const gfx::SpotLight &light, visual::SnowAccumulation &snow) {
    const auto &p = light.positionRange, &d = light.directionCone;
    const Vec3 center{p[0], p[1], p[2]}, aim{d[0], d[1], d[2]};
    const Vec3 right = cross(aim, {0, 1, 0}).normalized() * .25;
    const Vec3 up = cross(right, aim).normalized() * .15, back = aim * -.15;
    const Vec3 a = center - right - up, b = center + right - up,
               c = center + right + up, e = center - right + up;
    const Color casing{.16f, .21f, .24f, 1};
    quad(a, b, b + back, a + back, casing);
    quad(b, c, c + back, b + back, casing);
    quad(c, e, e + back, c + back, casing);
    quad(e, a, a + back, e + back, casing);
    quad(a + back, b + back, c + back, e + back, casing);
    // Rest on the upper casing, behind the lens. Use the same irregular
    // distribution scaled to this smaller fixture; light/glass stay clear.
    snow.caps(e+back*.65,c+back*.65,.055,.065,
              center.x*2.3+center.z*5.7,.30);
}
void lightGlass(const gfx::SpotLight &light) {
    const auto &p = light.positionRange, &d = light.directionCone;
    const Vec3 center{p[0], p[1], p[2]}, aim{d[0], d[1], d[2]};
    const Vec3 right = cross(aim, {0, 1, 0}).normalized() * .21;
    const Vec3 up = cross(right, aim).normalized() * .115;
    // The visible lens normal must face along the real spotlight's aim.
    quad(center - right - up, center - right + up, center + right + up, center + right - up,
         {.83f, .85f, .79f, 1});
}
} // namespace

void Renderer::inrunStructure(const Hill &h, const visual::InrunLayout &layout, visual::SnowAccumulation &snowCaps) {
    // This assembly surrounds the original track. Its snow, grooves, start
    // seat and collision profile are constructed separately and remain fixed.
    const double first = h.inrunStart(), last = -.75;
    constexpr int serviceSide = visual::inrunServiceSide;
    for (double x = first; x < 0; x += visual::inrunSnowStep(h)) {
        const double xx = std::min(0.0, x + visual::inrunSnowStep(h)), y = h.inrun(x), yy = h.inrun(xx);
        quad({x, y - .62, -2.3}, {xx, yy - .62, -2.3}, {xx, yy - .62, 2.3}, {x, y - .62, 2.3}, timber);
        for (int side : {-1, 1})
            beam({x, y - .48, side * 2.12}, {xx, yy - .48, side * 2.12}, .13, steel);
        beam({x, y - .27, serviceSide * 3.55}, {xx, yy - .27, serviceSide * 3.55}, .09, steel);
    }
    // Close the board's existing x=0 edge downwards, without extending or
    // lifting its snow surface. Side fascia coordinates are unchanged.
    quad({0, -.62, -2.3}, {0, -.62, 2.3}, {0, 0, 2.3}, {0, 0, -2.3}, {.33f, .29f, .25f, 1});
    // Connected maintenance stairs on steep sections, flat decking near the
    // board, on the far side only to leave camera 1's view clear. All walking
    // surfaces lie outside the unchanged +/-2.3 m track.
    for (double x = first; x < last;) {
        const double grade = std::abs(h.inrunSlope(x));
        const double step = grade > .28 ? .19 / grade : .8;
        const double xx = std::min(last, x + step), y = h.inrun(x) - .04, yy = h.inrun(xx) - .04;
        const double a = serviceSide * visual::inrunWalkInner, b = serviceSide * visual::inrunWalkOuter;
        const double endY = grade > .28 ? y : yy;
        quad({x, y, a}, {x, y, b}, {xx, endY, b}, {xx, endY, a}, timber);
        if (grade > .28)
            quad({xx, y, a}, {xx, y, b}, {xx, yy, b}, {xx, yy, a}, {.32f, .30f, .27f, 1});
        quad({x, y - .22, b}, {xx, yy - .22, b}, {xx, endY, b}, {x, y, b}, steel);
        x = xx;
    }
    const int railBays = int(std::ceil((last - first) / 1.35));
    const double cabinEntry = h.start() - 4.15;
    std::vector<double> railNodes;
    for (int i = 0; i <= railBays; ++i)
        railNodes.push_back(i == railBays ? last : first + (last - first) * i / railBays);
    for (double x : {cabinEntry - .35, cabinEntry + .35, -2.05})
        if (x > first && x < last)
            railNodes.push_back(x);
    std::sort(railNodes.begin(), railNodes.end());
    railNodes.erase(std::unique(railNodes.begin(), railNodes.end(),
                               [](double a, double b) { return std::abs(a - b) < 1e-8; }), railNodes.end());
    auto opening = [&](double x) {
        return (x > -2.05 && x < last) ||
               (x > cabinEntry - .35 && x < cabinEntry + .35);
    };
    for (size_t i = 0; i < railNodes.size(); ++i) {
        const double x = railNodes[i], y = h.inrun(x);
        beam({x, y - .34, serviceSide * 2.2}, {x, y + .76, serviceSide * 2.2}, .033, brace);
        if (!opening(x))
            beam({x, y - .27, serviceSide * 3.68}, {x, y + 1.12, serviceSide * 3.68}, .035, brace);
        if (i + 1 < railNodes.size())
            for (double z : {serviceSide * 2.2, serviceSide * 3.68}) {
                const bool outer = std::abs(z) > 3;
                const double end = railNodes[i + 1];
                if (outer && opening((x + end) * .5))
                    continue; // Clear openings at the stairs and cabin landing.
                const double endY = h.inrun(end), height = outer ? 1.08 : .72;
                beam({x, y + height, z}, {end, endY + height, z}, .040, brace);
                beam({x, y + height * .52, z}, {end, endY + height * .52, z}, .023, steel);
            }
    }
    // Place snow over complete rail runs, so the 1.35 m posts cannot impose
    // their repeating rhythm on the clumps. Preserve the existing openings.
    for (double z : {serviceSide * 2.2, serviceSide * 3.68}) {
        const bool outer = std::abs(z) > 3;
        const double height = outer ? 1.08 : .72;
        std::vector<Vec3> run;
        const auto emit = [&] {
            if (run.size() >= 2)
                snowCaps.caps(run, .14, .16, run.front().x * 3.1 + z * 7.7);
        };
        for (size_t i = 0; i+1 < railNodes.size(); ++i) {
            const double a = railNodes[i], b = railNodes[i+1];
            if (outer && opening((a+b)*.5)) {
                emit(); run.clear(); continue;
            }
            if (run.empty()) run.push_back({a,h.inrun(a)+height+.035,z});
            run.push_back({b,h.inrun(b)+height+.035,z});
        }
        emit();
    }
    {
        const double y = h.inrun(last);
        beam({last, y - .27, serviceSide * 2.45}, {last, y + 1.12, serviceSide * 2.45}, .035, brace);
        beam({last, y + 1.08, serviceSide * 2.45}, {last, y + 1.08, serviceSide * 3.68}, .040, brace);
        snowCaps.caps({last, y + 1.115, serviceSide * 2.45}, {last, y + 1.115, serviceSide * 3.68},
                      .14, .16, last * 3.1);
        beam({last, y + .56, serviceSide * 2.45}, {last, y + .56, serviceSide * 3.68}, .023, steel);
    }
    for (const auto &support : layout.supports) {
        const double x = support.x;
        beam({x, support.deck, -3.65}, {x, support.deck, visual::inrunColumnZ + .2}, .16, steel);
        if (!support.exposed())
            continue; // Earth-supported sections need no reversed/buried piers.
        const double radius = clamp(.12 + support.height() * .003, .12, .32);
        for (int side : {-1, 1}) {
            const double z = side * visual::inrunColumnZ;
            box({x, (support.footingBottom + support.footingTop) * .5, z},
                {1.10, support.footingTop - support.footingBottom, 1.10}, {.49f, .52f, .53f, 1});
            snowCaps.footing(h, x, z, .55, .55, support.footingTop, x + z * 7);
            box({x, support.footingTop + .04, z}, {radius * 2 + .15, .08, radius * 2 + .15}, steel);
            beam({x, level(support, 0), z}, {x, support.deck, z}, radius, steel);
            box({x, support.deck, z}, {.26, .27, .30}, brace);
        }
        if (support.height() > 1.8) {
            const int tiers = clamp(int(std::ceil(support.height() / 6.5)), 1, 12);
            for (int tier = 0; tier < tiers; ++tier) {
                const double a = level(support, double(tier) / tiers), b = level(support, double(tier + 1) / tiers);
                beam({x, a, -visual::inrunColumnZ}, {x, a, visual::inrunColumnZ}, .075, steel);
                const int side = tier % 2 ? 1 : -1;
                beam({x, a, side * visual::inrunColumnZ}, {x, b, -side * visual::inrunColumnZ}, .06, brace);
            }
        }
    }
    for (size_t i = 1; i < layout.supports.size(); ++i) {
        const auto &a = layout.supports[i - 1], &b = layout.supports[i];
        if (std::min(a.height(), b.height()) <= 1.8)
            continue;
        const int tiers = clamp(int(std::ceil(std::max(a.height(), b.height()) / 6.5)), 1, 12);
        for (int tier = 0; tier < tiers; ++tier) {
            const double low = double(tier) / tiers, high = double(tier + 1) / tiers;
            for (int side : {-1, 1}) {
                const double z = side * visual::inrunColumnZ;
                if ((tier + int(i) + (side > 0 ? 1 : 0)) % 2)
                    beam({a.x, level(a, low), z}, {b.x, level(b, high), z}, .060, brace);
                else
                    beam({a.x, level(a, high), z}, {b.x, level(b, low), z}, .060, brace);
                if (tier == 0)
                    beam({a.x, level(a, low), z}, {b.x, level(b, low), z}, .070, steel);
                if (tier + 1 < tiers)
                    beam({a.x, level(a, high), z}, {b.x, level(b, high), z}, .070, steel);
            }
        }
    }
    accessStairs(h, serviceSide, snowCaps);
    // Small shielded fixtures are attached to the outer service girder.
    // Their faces and real vertex lights share precisely the same positions.
    for (int column = 0; column < layout.lightColumns; ++column) {
        const double x = layout.firstLightX + column * layout.lightSpacing, y = h.inrun(x);
        beam({x, y - .27, serviceSide * 3.65}, {x, y + 2.55, serviceSide * 3.65}, .037, steel);
        beam({x, y + 2.55, serviceSide * 3.65}, {x, y + 2.49, serviceSide * 3.28}, .032, steel);
        lightHousing(layout.lights[size_t(column)], snowCaps);
    }
    visual::pushAttrib(gfx::LIGHTING_BIT);
    const float emission[]{.20f, .19f, .16f, 1};
    visual::materialfv(gfx::FRONT_AND_BACK, gfx::EMISSION, emission);
    for (int light = 0; light < layout.lightColumns; ++light)
        lightGlass(layout.lights[size_t(light)]);
    visual::popAttrib();
}
void Renderer::startSignalHousing(const Hill &h) {
    const visual::StartSignal signal(h, Frame{});
    const Vec3 up{0, 1, 0};
    constexpr Color shell{.055f, .062f, .069f, 1}, rim{.035f, .041f, .047f, 1},
        inside{.018f, .022f, .027f, 1};
    visual::pushAttrib(gfx::LIGHTING_BIT | gfx::CURRENT_BIT | gfx::TEXTURE_BIT | gfx::ENABLE_BIT);
    visual::disable(gfx::TEXTURE_2D);
    const float noEmission[]{0, 0, 0, 1}, specular[]{.035f, .035f, .035f, 1};
    visual::materialfv(gfx::FRONT_AND_BACK, gfx::EMISSION, noEmission);
    visual::materialfv(gfx::FRONT_AND_BACK, gfx::SPECULAR, specular);
    visual::materialf(gfx::FRONT_AND_BACK, gfx::SHININESS, 8);
    beam(signal.base, signal.base + up * 3.08, .045, {.17f, .21f, .23f, 1});
    auto local = [&](double x, double y, double depth) {
        return signal.center + signal.right * x + up * y + signal.front * depth;
    };
    const std::array<std::array<double, 2>, 8> corners{{{-.17, -.50},
                                                        {.17, -.50},
                                                        {.21, -.46},
                                                        {.21, .46},
                                                        {.17, .50},
                                                        {-.17, .50},
                                                        {-.21, .46},
                                                        {-.21, -.46}}};
    for (int face : {-1, 1}) {
        visual::color(shell.r, shell.g, shell.b, 1);
        visual::begin(gfx::TRIANGLE_FAN);
        visual::normal(signal.front.x * face, 0, signal.front.z * face);
        vertex(local(0, 0, face * .085));
        for (int i = 0; i <= 8; ++i) {
            const auto &p = corners[size_t(face > 0 ? i % 8 : (8 - i) % 8)];
            vertex(local(p[0], p[1], face * .085));
        }
        visual::end();
    }
    for (size_t i = 0; i < corners.size(); ++i) {
        const auto &a = corners[i], &b = corners[(i + 1) % corners.size()];
        quad(local(a[0], a[1], .085), local(a[0], a[1], -.085), local(b[0], b[1], -.085),
             local(b[0], b[1], .085), shell);
    }
    for (double y : {-.34, .34})
        box(local(0, y, -.105), {.25, .065, .12}, {.12f, .15f, .17f, 1});
    for (int lens = 0; lens < 2; ++lens) {
        const Vec3 center = signal.lens(lens);
        auto circle = [&](double angle, double radius, double depth) {
            return center + (signal.right * std::cos(angle) + up * std::sin(angle)) * radius +
                   signal.front * depth;
        };
        for (int i = 0; i < 20; ++i) {
            const double a = i * 2 * PI / 20, b = (i + 1) * 2 * PI / 20;
            quad(circle(a, .162, .012), circle(a, .184, .012), circle(b, .184, .012), circle(b, .162, .012),
                 rim);
            quad(circle(a, .184, -.03), circle(b, .184, -.03), circle(b, .184, .012), circle(a, .184, .012),
                 rim);
        }
        // Open-bottom visor: dark inner wall and a thin front lip. The
        // glass stays recessed so the signal is brightest when viewed ahead.
        for (int i = 0; i < 14; ++i) {
            const double a = (-.13 + 1.26 * i / 14) * PI, b = (-.13 + 1.26 * (i + 1) / 14) * PI;
            quad(circle(a, .186, 0), circle(b, .186, 0), circle(b, .186, .18), circle(a, .186, .18), rim);
            quad(circle(b, .176, 0), circle(a, .176, 0), circle(a, .176, .18), circle(b, .176, .18), inside);
            quad(circle(a, .176, .18), circle(a, .186, .18), circle(b, .186, .18), circle(b, .176, .18), rim);
        }
        for (int side : {-1, 1})
            for (int row : {-1, 1})
                box(center + signal.right * (side * .179) + up * (row * .179), {.015, .015, .015},
                    {.25f, .28f, .30f, 1});
    }
    visual::popAttrib();
}
void Renderer::startSignalLenses(const Hill &h, const Frame &frame) {
    const visual::StartSignal signal(h, frame);
    visual::pushAttrib(gfx::LIGHTING_BIT | gfx::CURRENT_BIT);
    const float specular[]{.08f, .08f, .08f, 1};
    visual::materialfv(gfx::FRONT_AND_BACK, gfx::SPECULAR, specular);
    visual::materialf(gfx::FRONT_AND_BACK, gfx::SHININESS, 24);
    const Vec3 up{0, 1, 0};
    for (int lens = 0; lens < 2; ++lens) {
        const auto emission = signal.emission(lens);
        visual::materialfv(gfx::FRONT_AND_BACK, gfx::EMISSION, emission.data());
        const Color glass =
            lens == 0 ? (signal.lit(lens) ? Color{.30f, .025f, .015f, 1} : Color{.085f, .012f, .008f, 1})
                      : (signal.lit(lens) ? Color{.045f, .26f, .075f, 1} : Color{.012f, .065f, .025f, 1});
        visual::color(glass.r, glass.g, glass.b, 1);
        auto point = [&](double fraction, double angle) {
            const Vec3 radial = signal.right * std::cos(angle) + up * std::sin(angle);
            const Vec3 normal = (signal.front + radial * (2 * .028 * fraction / signal.radius)).normalized();
            visual::normal(normal.x, normal.y, normal.z);
            vertex(signal.lens(lens) + radial * (fraction * signal.radius) +
                   signal.front * (.028 * (1 - fraction * fraction)));
        };
        visual::begin(gfx::TRIANGLE_FAN);
        point(0, 0);
        for (int i = 0; i <= 20; ++i)
            point(.45, i * 2 * PI / 20);
        visual::end();
        for (const auto ring : {std::array<double, 2>{.45, .80}, std::array<double, 2>{.80, 1}}) {
            visual::begin(gfx::QUAD_STRIP);
            for (int i = 0; i <= 20; ++i)
                for (double radius : ring)
                    point(radius, i * 2 * PI / 20);
            visual::end();
        }
    }
    visual::popAttrib();
}
} // namespace sjp
