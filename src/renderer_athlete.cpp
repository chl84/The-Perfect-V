#include "original_skeleton.h"
#include "renderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace sjp {
namespace {
// Sample the existing rendered triangles, not a new collision surface. The
// small height cache covers the unchanged athlete mesh's local x extent.
struct FallContact {
    const Hill &hill;
    Vec3 origin;
    double start, step, firstX, lift = 0;
    double rampStep, rampFirstX = 0;
    bool skiSurface = false;
    Vec3 skiAnkle, skiDirection;
    std::vector<double> heights;
    std::vector<double> rampHeights;
    sjp::gfx::Enum mode = sjp::gfx::QUADS;
    size_t count = 0;
    std::array<Vec3, 4> face{};
    FallContact(const Hill &h, const Frame &f, const OriginalSkeleton &skeleton)
        : hill(h), origin{f.x, f.y, 0}, start(std::min(-170 * h.scale(), h.inrunStart() - 60 * h.scale())),
          step(3 * h.scale()), rampStep(2 * h.scale()) {
        double low = skeleton.points[0].x, high = low;
        for (auto p : skeleton.points) {
            low = std::min(low, p.x);
            high = std::max(high, p.x);
        }
        firstX = start + std::floor((f.x + low - .6 - start) / step) * step;
        const size_t count = size_t(std::ceil((f.x + high + .6 - firstX) / step)) + 2;
        heights.reserve(count);
        for (size_t i = 0; i < count; ++i)
            heights.push_back(h.ground(firstX + i * step));
        if (f.x + low - .6 <= 0 && f.x + high + .6 >= h.inrunStart()) {
            rampFirstX = h.inrunStart() + std::floor((f.x + low - .6 - h.inrunStart()) / rampStep) * rampStep;
            const size_t rampCount = size_t(std::ceil((f.x + high + .6 - rampFirstX) / rampStep)) + 2;
            rampHeights.reserve(rampCount);
            for (size_t i = 0; i < rampCount; ++i)
                rampHeights.push_back(h.inrun(std::min(0.0, rampFirstX + i * rampStep)));
        }
    }
    double terrainSurface(double x, double z) const {
        const long i = long(std::floor((x - firstX) / step));
        const double x0 = firstX + i * step, u = (x - x0) / step;
        auto height = [&](long index) {
            return index >= 0 && size_t(index) < heights.size() ? heights[size_t(index)]
                                                                : hill.ground(firstX + index * step);
        };
        const double a = height(i), b = height(i + 1), s = hill.scale();
        const double zStep = 10 * s, z0 = -200 * s + std::floor((z + 200 * s) / zStep) * zStep;
        const double z1 = z0 + zStep, v = (z - z0) / zStep;
        if (std::max(std::abs(z0), std::abs(z1)) <= 19 * s)
            return a * (1 - u) + b * u;
        auto node = [&](double xx, double zz, double y) {
            const double edge = std::max(0.0, std::abs(zz) - 19 * s);
            return y - edge * .08 + std::sin(xx * .017 + zz * .013) * edge * .018;
        };
        const double aa = node(x0, z0, a), bb = node(x0, z1, a), cc = node(x0 + step, z1, b),
                     dd = node(x0 + step, z0, b);
        return v >= u ? aa * (1 - v) + bb * (v - u) + cc * u : aa * (1 - u) + cc * v + dd * (u - v);
    }
    double surface(double x, double z) const {
        double y = terrainSurface(x, z);
        if (rampHeights.empty() || x < hill.inrunStart() || x > 0 || std::abs(z) > 2.3)
            return y;
        // Read the track's existing 2*scale snow quads, including the shorter
        // final quad at the board. Never extend or move the track itself.
        const long i = long(std::floor((x - rampFirstX) / rampStep));
        const double a = rampFirstX + i * rampStep, b = std::min(0.0, a + rampStep);
        auto height = [&](long index) {
            return index >= 0 && size_t(index) < rampHeights.size()
                       ? rampHeights[size_t(index)]
                       : hill.inrun(std::min(0.0, rampFirstX + index * rampStep));
        };
        const double t = b > a ? (x - a) / (b - a) : 0;
        return std::max(y, height(i) * (1 - t) + height(i + 1) * t);
    }
    void measure(Vec3 local) {
        const Vec3 p = local + origin;
        // Only the distant ski ends may press into snow during a fall. Keep
        // bindings, boots and the complete body clear, with no pose changes.
        double snowPress = 0;
        if (skiSurface) {
            const double distance = std::abs(dot(local - skiAnkle, skiDirection));
            const double t = clamp((distance - .30) / .65, 0.0, 1.0);
            snowPress = .15 * t * t * (3 - 2 * t);
        }
        lift = std::max(lift, surface(p.x, p.z) + .008 - snowPress - p.y);
    }
    void edge(Vec3 a, Vec3 b) {
        // A triangle edge can cross a terrain-grid bend between its endpoints.
        // Sample those crossings as well, retaining the original height grid.
        const Vec3 delta = b - a;
        if (std::abs(delta.x) >= 1e-10) {
            const double low = std::min(a.x, b.x) + origin.x;
            const double high = std::max(a.x, b.x) + origin.x;
            for (double x = firstX + std::ceil((low - firstX) / step) * step; x < high; x += step) {
                const double t = (x - origin.x - a.x) / delta.x;
                measure(a + delta * t);
            }
            if (!rampHeights.empty()) {
                auto trackCrossing = [&](double x) {
                    if (x <= low || x >= high || x < hill.inrunStart() || x > 0)
                        return;
                    const double t = (x - origin.x - a.x) / delta.x;
                    auto point = a + delta * t;
                    point.x = x - origin.x;
                    measure(point);
                };
                for (double x = rampFirstX + std::ceil((low - rampFirstX) / rampStep) * rampStep; x < high;
                     x += rampStep)
                    trackCrossing(x);
                trackCrossing(hill.inrunStart());
                trackCrossing(0);
            }
        }
        // A limb/ski can cross a track side even with no travel-axis extent.
        if (!rampHeights.empty() && std::abs(delta.z) >= 1e-10)
            for (double z : {-2.3, 2.3}) {
                const double t = (z - a.z) / delta.z;
                if (t > 0 && t < 1) {
                    auto point = a + delta * t;
                    point.z = z;
                    if (point.x + origin.x >= hill.inrunStart() && point.x + origin.x <= 0)
                        measure(point);
                }
            }
    }
    void begin(sjp::gfx::Enum primitive) {
        mode = primitive;
        count = 0;
    }
    void include(Vec3 local) {
        measure(local);
        if (mode == sjp::gfx::TRIANGLE_FAN) {
            if (count == 0)
                face[0] = local;
            else {
                edge(face[0], local);
                if (count > 1)
                    edge(face[1], local);
                face[1] = local;
            }
        } else {
            face[count % 4] = local;
            if ((mode == sjp::gfx::QUADS && count % 4 == 3) ||
                (mode == sjp::gfx::QUAD_STRIP && count >= 3 && count % 2 == 1))
                for (size_t a = 0; a < 4; ++a)
                    for (size_t b = a + 1; b < 4; ++b)
                        edge(face[a], face[b]);
        }
        ++count;
    }
};
thread_local FallContact *contactPass = nullptr;
enum class Finish { Fabric, Rubber, Ski, Shell, Glass, Skin, Metal };
void material(Finish finish) {
    if (contactPass)
        return; // Fall contact measurement must remain entirely free of drawing calls.
    float gloss = .035f, shininess = 8;
    switch (finish) {
    case Finish::Rubber:
        gloss = .025f;
        shininess = 10;
        break;
    case Finish::Ski:
        gloss = .19f;
        shininess = 46;
        break;
    case Finish::Shell:
        gloss = .25f;
        shininess = 48;
        break;
    case Finish::Glass:
        gloss = .52f;
        shininess = 72;
        break;
    case Finish::Skin:
        gloss = .045f;
        shininess = 14;
        break;
    case Finish::Metal:
        gloss = .20f;
        shininess = 50;
        break;
    default:
        break;
    }
    const float specular[]{gloss * .94f, gloss, gloss * 1.04f, 1};
    gfx::materialfv(gfx::FRONT_AND_BACK, gfx::SPECULAR, specular);
    gfx::materialf(gfx::FRONT_AND_BACK, gfx::SHININESS, shininess);
}
void emitBegin(sjp::gfx::Enum mode) {
    if (contactPass)
        contactPass->begin(mode);
    else
        sjp::gfx::begin(mode);
}
void emitEnd() {
    if (!contactPass)
        sjp::gfx::end();
}
void emitNormal(double x, double y, double z) {
    if (!contactPass)
        sjp::gfx::normal3d(x, y, z);
}
void vertex(Vec3 p) {
    if (contactPass)
        contactPass->include(p);
    else
        sjp::gfx::vertex3d(p.x, p.y, p.z);
}
void color(Color c) {
    if (!contactPass)
        sjp::gfx::color4f(c.r, c.g, c.b, c.a);
}
Color shade(Color c, double amount) {
    return {float(c.r * amount), float(c.g * amount), float(c.b * amount), c.a};
}
void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color tint) {
    const Vec3 normal = cross(b - a, c - a).normalized();
    color(tint);
    emitNormal(normal.x, normal.y, normal.z);
    vertex(a);
    vertex(b);
    vertex(c);
    vertex(d);
}
struct Basis {
    Vec3 forward, up, side;
};
Basis segmentBasis(Vec3 axis) {
    axis = axis.normalized();
    const Vec3 reference = std::abs(axis.z) < .9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const Vec3 forward = cross(axis, reference).normalized();
    return {forward, axis, cross(forward, axis).normalized()};
}
Basis skiBasis(Vec3 a, Vec3 b) {
    const Vec3 forward = (b - a).normalized();
    const Vec3 reference = std::abs(forward.y) < .95 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    const Vec3 side = cross(forward, reference).normalized();
    return {forward, cross(side, forward).normalized(), side};
}
void orientedBox(Vec3 center, const Basis &basis, Vec3 size, Color tint) {
    const Vec3 x = basis.forward * (size.x * .5), y = basis.up * (size.y * .5),
               z = basis.side * (size.z * .5);
    emitBegin(sjp::gfx::QUADS);
    quad(center - x + y - z, center - x + y + z, center + x + y + z, center + x + y - z, tint);
    quad(center - x - y - z, center + x - y - z, center + x - y + z, center - x - y + z, shade(tint, .8));
    quad(center - x - y + z, center + x - y + z, center + x + y + z, center - x + y + z, tint);
    quad(center + x - y - z, center - x - y - z, center - x + y - z, center + x + y - z, tint);
    quad(center - x - y - z, center - x - y + z, center - x + y + z, center - x + y - z, tint);
    quad(center + x - y + z, center + x - y - z, center + x + y - z, center + x + y + z, tint);
    emitEnd();
}
void ellipsoid(Vec3 center, const Basis &basis, Vec3 radii, Color tint, int slices = 10, int rings = 6) {
    color(tint);
    for (int ring = 0; ring < rings; ++ring) {
        const double a = -PI * .5 + ring * PI / rings, b = a + PI / rings;
        emitBegin(sjp::gfx::QUAD_STRIP);
        for (int slice = 0; slice <= slices; ++slice) {
            const double angle = slice * 2 * PI / slices;
            for (double latitude : {a, b}) {
                const double x = std::cos(latitude) * std::cos(angle), y = std::sin(latitude),
                             z = std::cos(latitude) * std::sin(angle);
                const Vec3 normal =
                    (basis.forward * (x / radii.x) + basis.up * (y / radii.y) + basis.side * (z / radii.z))
                        .normalized();
                emitNormal(normal.x, normal.y, normal.z);
                vertex(center + basis.forward * (x * radii.x) + basis.up * (y * radii.y) +
                       basis.side * (z * radii.z));
            }
        }
        emitEnd();
    }
}
// Only the skin around a segment changes. All segment centres and endpoints
// are the original, already interpolated pose nodes, including fall poses.
void tailoredSegment(Vec3 a, Vec3 b, double frontA, double sideA, double frontB, double sideB, Color tint,
                     bool bib = false, Color bibColor = {}) {
    if ((b - a).length() < 1e-5)
        return;
    const Basis basis = segmentBasis(b - a);
    constexpr std::array<double, 6> positions{0, .1, .27, .71, .89, 1};
    constexpr std::array<double, 6> fullness{.76, 1, 1, 1, 1, .76};
    auto ringPoint = [&](size_t ring, double angle) {
        const double t = positions[ring], depth = (frontA + (frontB - frontA) * t) * fullness[ring],
                     width = (sideA + (sideB - sideA) * t) * fullness[ring];
        return a + (b - a) * t + basis.forward * (std::cos(angle) * depth) +
               basis.side * (std::sin(angle) * width);
    };
    emitBegin(sjp::gfx::QUADS);
    for (size_t ring = 0; ring + 1 < positions.size(); ++ring)
        for (int face = 0; face < 8; ++face) {
            const double left = face * PI / 4, right = (face + 1) * PI / 4;
            const bool frontPanel = face == 0 || face == 7;
            Color panel = bib && frontPanel && ring >= 1 && ring <= 3
                              ? bibColor
                              : shade(tint, face == 2 || face == 5 ? .9 : 1);
            quad(ringPoint(ring, left), ringPoint(ring + 1, left), ringPoint(ring + 1, right),
                 ringPoint(ring, right), panel);
        }
    emitEnd();
    for (bool end : {false, true}) {
        color(shade(tint, .92));
        emitBegin(sjp::gfx::TRIANGLE_FAN);
        const Vec3 normal = basis.up * (end ? 1 : -1);
        emitNormal(normal.x, normal.y, normal.z);
        vertex(end ? b : a);
        for (int face = 0; face <= 8; ++face)
            vertex(ringPoint(end ? positions.size() - 1 : 0, (end ? -face : face) * PI / 4));
        emitEnd();
    }
}
void skiBoot(Vec3 ankle, Vec3 knee, const Basis &basis, Color shell, Color sole, Color strap, Color buckle) {
    // A light shell and red closures separate the boot from both trouser skins
    // and the turquoise binding. The sole sits on the existing binding plate.
    material(Finish::Rubber);
    orientedBox(ankle + basis.forward * .045 + basis.up * .049, basis, {.36, .030, .155}, sole);
    material(Finish::Shell);
    ellipsoid(ankle + basis.forward * .055 + basis.up * .108, basis, {.185, .068, .094}, shell, 10, 5);
    const Vec3 calf = (knee - ankle).normalized();
    const Vec3 cuffBottom = ankle + calf * .060 + basis.up * .040,
               cuffTop = ankle + calf * .250 + basis.up * .015;
    const Basis cuff = segmentBasis(cuffTop - cuffBottom);
    tailoredSegment(cuffBottom, cuffTop, .105, .100, .110, .105, shell);
    const Vec3 upperStrap = cuffTop - cuff.up * .045;
    material(Finish::Rubber);
    orientedBox(upperStrap + cuff.forward * .109, cuff, {.025, .037, .208}, strap);
    orientedBox(ankle + basis.forward * .145 + basis.up * .155, basis, {.040, .026, .172}, strap);
    material(Finish::Metal);
    orientedBox(upperStrap + cuff.forward * .127, cuff, {.014, .023, .045}, buckle);
}
void ski(Vec3 rear, Vec3 front, Vec3 tip, Vec3 ankle, Color deck, Color edge, Color accent) {
    const Basis basis = skiBasis(rear, front);
    if (contactPass) {
        contactPass->skiSurface = true;
        contactPass->skiAnkle = ankle;
        contactPass->skiDirection = basis.forward;
    }
    const std::array<Vec3, 3> centers{rear, front, tip};
    constexpr std::array<double, 3> widths{.071, .095, .066};
    constexpr double halfThickness = .018;
    auto corner = [&](size_t section, double across, double height) {
        return centers[section] + basis.side * (widths[section] * across) + basis.up * height;
    };
    material(Finish::Ski);
    emitBegin(sjp::gfx::QUADS);
    for (size_t section = 0; section < 2; ++section) {
        // Three adjacent top panels share their edges; stripes do not sit on
        // top of a second nearly coplanar ski surface.
        constexpr std::array<double, 4> stripes{-1, -.23, .23, 1};
        for (size_t stripe = 0; stripe < 3; ++stripe)
            quad(corner(section, stripes[stripe], halfThickness),
                 corner(section, stripes[stripe + 1], halfThickness),
                 corner(section + 1, stripes[stripe + 1], halfThickness),
                 corner(section + 1, stripes[stripe], halfThickness),
                 stripe == 1    ? accent
                 : section == 1 ? accent
                                : deck);
        quad(corner(section, -1, -halfThickness), corner(section + 1, -1, -halfThickness),
             corner(section + 1, 1, -halfThickness), corner(section, 1, -halfThickness), edge);
        quad(corner(section, -1, -halfThickness), corner(section, -1, halfThickness),
             corner(section + 1, -1, halfThickness), corner(section + 1, -1, -halfThickness), edge);
        quad(corner(section + 1, 1, -halfThickness), corner(section + 1, 1, halfThickness),
             corner(section, 1, halfThickness), corner(section, 1, -halfThickness), edge);
    }
    quad(corner(0, -1, -halfThickness), corner(0, 1, -halfThickness), corner(0, 1, halfThickness),
         corner(0, -1, halfThickness), edge);
    quad(corner(2, 1, -halfThickness), corner(2, -1, -halfThickness), corner(2, -1, halfThickness),
         corner(2, 1, halfThickness), edge);
    emitEnd();
    if (contactPass)
        contactPass->skiSurface = false;
    material(Finish::Metal);
    orientedBox(ankle + basis.up * .025, basis, {.37, .045, .14}, edge);
    orientedBox(ankle - basis.forward * .13 + basis.up * .067, basis, {.08, .09, .13}, accent);
    orientedBox(ankle + basis.forward * .13 + basis.up * .046, basis, {.085, .055, .14}, accent);
}
void helmet(Vec3 head, const Basis &basis, Color shell, Color trim, Color visor, Color skin) {
    material(Finish::Skin);
    ellipsoid(head - basis.up * .054 + basis.forward * .025, basis, {.145, .16, .135}, skin);
    constexpr std::array<double, 7> latitudes{-.32, .05, .36, .71, 1.1, 1.4, PI * .5};
    constexpr int slices = 16;
    auto point = [&](double latitude, double angle) {
        return head + basis.forward * (.2 * std::cos(latitude) * std::cos(angle)) +
               basis.up * (.207 * std::sin(latitude)) +
               basis.side * (.192 * std::cos(latitude) * std::sin(angle));
    };
    const auto helmetVertex = [&](double latitude, double angle) {
        const Vec3 n = (basis.forward * (std::cos(latitude) * std::cos(angle) / .2) +
                        basis.up * (std::sin(latitude) / .207) +
                        basis.side * (std::cos(latitude) * std::sin(angle) / .192))
                           .normalized();
        emitNormal(n.x, n.y, n.z);
        vertex(point(latitude, angle));
    };
    // Shell and glass reuse the original faces in two compact material groups.
    for (bool glassPass : {false, true}) {
        material(glassPass ? Finish::Glass : Finish::Shell);
        emitBegin(sjp::gfx::QUADS);
        for (size_t ring = 0; ring + 1 < latitudes.size(); ++ring)
            for (int slice = 0; slice < slices; ++slice) {
                const double a = slice * 2 * PI / slices, b = (slice + 1) * 2 * PI / slices;
                const bool glass = ring < 2 && std::cos((a + b) * .5) > .7;
                if (glass != glassPass)
                    continue;
                const bool racingStripe = (slice == 7 || slice == 8) && ring > 1;
                const bool vent = ring == 3 && (slice == 4 || slice == 11);
                Color panel = glass          ? shade(visor, ring == 0 ? .72 : 1.07)
                              : racingStripe ? trim
                              : vent         ? shade(shell, .6)
                                             : shell;
                color(panel);
                helmetVertex(latitudes[ring], a);
                helmetVertex(latitudes[ring + 1], a);
                helmetVertex(latitudes[ring + 1], b);
                helmetVertex(latitudes[ring], b);
            }
        emitEnd();
    }
    material(Finish::Rubber);
    for (double side : {-1.0, 1.0}) {
        const Vec3 ear = head + basis.side * (.153 * side) - basis.up * .015;
        const Vec3 chin = head - basis.up * .175 + basis.forward * .045 + basis.side * (.08 * side);
        tailoredSegment(ear, chin, .011, .015, .011, .015, shade(shell, .66));
    }
}
double fringeLift(const Frame &frame) {
    // Distance past the board gives a short, smooth rise without maintaining
    // animation state. Recorded/interpolated frames therefore also work when
    // seeking or pausing. Wind eases away as the jumper slows in the outrun.
    const auto smooth = [](double t) {
        t = clamp(t, 0.0, 1.0);
        return t * t * (3 - 2 * t);
    };
    const double speed = std::hypot(frame.vx, frame.vy);
    return smooth(frame.x / (std::max(1.0, double(frame.vx)) * .25)) * smooth((speed - 3) / 9);
}
void presidentHead(Vec3 head, const Basis &basis, Color skin, Color hair, Color hairLight, double lift) {
    const auto at = [&](double forward, double up, double side = 0) {
        return head + basis.forward * forward + basis.up * up + basis.side * side;
    };
    material(Finish::Skin);
    // A continuous cheek/jaw surface keeps the reference's broad face and
    // tapering chin, without separate spherical cheeks or protruding eyeballs.
    struct HeadSection { double up, front, back, width; };
    constexpr std::array<HeadSection, 15> sections{{
        {-.224, 0, 0, 0}, {-.207, .066, .070, .078}, {-.180, .103, .110, .119},
        {-.148, .129, .133, .145}, {-.110, .148, .145, .154}, {-.070, .155, .151, .160},
        {-.030, .157, .154, .159}, {.010, .151, .151, .155}, {.024, .149, .149, .153},
        {.045, .145, .145, .150}, {.060, .140, .140, .147},
        {.080, .132, .132, .141}, {.114, .109, .109, .119}, {.143, .073, .073, .083},
        {.163, 0, 0, 0}}};
    const auto sectionAt = [&](double up) {
        size_t r = 1;
        while (r + 1 < sections.size() && sections[r].up < up)
            ++r;
        const auto &a = sections[r - 1], &b = sections[r];
        const double t = clamp((up - a.up) / (b.up - a.up), 0.0, 1.0);
        return HeadSection{up, a.front + (b.front - a.front) * t,
                            a.back + (b.back - a.back) * t, a.width + (b.width - a.width) * t};
    };
    const auto centerAt = [](double up) { return .012 + .045 * clamp((-.150 - up) / .074, 0.0, 1.0); };
    const auto relief = [](double up, double side) {
        const auto bump = [](double a, double b) { return std::exp(-a * a - b * b); };
        return .014 * bump((up + .050) / .055, (std::abs(side) - .095) / .040) -
               .010 * bump((up - .024) / .020, (std::abs(side) - .067) / .035) +
               .010 * bump((up - .052) / .018, (std::abs(side) - .067) / .035);
    };
    const auto face = [&](double up, double side, double offset = .0025) {
        const auto section = sectionAt(up);
        const double c = std::sqrt(std::max(0.0, 1 - std::pow(side / std::max(.001, section.width), 2)));
        return at(centerAt(up) + section.front * c + relief(up, side) * c * c + offset, up, side);
    };
    constexpr int headSlices = 32;
    std::array<std::array<Vec3, headSlices>, sections.size()> points{};
    for (size_t r = 0; r < sections.size(); ++r)
        for (int i = 0; i < headSlices; ++i) {
            const auto &s = sections[r];
            const double angle = i * 2 * PI / headSlices, c = std::cos(angle), side = s.width * std::sin(angle);
            points[r][i] = at(centerAt(s.up) + c * (c >= 0 ? s.front : s.back) +
                                 (c > 0 ? relief(s.up, side) * c * c : 0), s.up, side);
        }
    const auto drawSkin = [&](const auto &grid, bool wrap) {
        auto normals = grid;
        for (auto &row : normals)
            for (auto &n : row)
                n = {};
        const size_t rows = grid.size(), columns = grid[0].size(), faces = columns - (wrap ? 0 : 1);
        for (size_t r = 0; r + 1 < rows; ++r)
            for (size_t i = 0; i < faces; ++i) {
                const size_t j = (i + 1) % columns;
                const auto a = grid[r][i], b = grid[r + 1][i], c = grid[r + 1][j], d = grid[r][j];
                const Vec3 normal = cross(b - a, c - a) + cross(c - a, d - a);
                for (size_t row : {r, r + 1})
                    for (size_t column : {i, j})
                        normals[row][column] = normals[row][column] + normal;
            }
        color(skin);
        emitBegin(gfx::QUADS);
        for (size_t r = 0; r + 1 < rows; ++r)
            for (size_t i = 0; i < faces; ++i) {
                const size_t j = (i + 1) % columns;
                for (const auto index : {std::pair{r, i}, {r + 1, i}, {r + 1, j}, {r, j}}) {
                    const auto normal = normals[index.first][index.second].normalized();
                    emitNormal(normal.x, normal.y, normal.z);
                    vertex(grid[index.first][index.second]);
                }
            }
        emitEnd();
    };
    drawSkin(points, true);
    const auto line = [&](double upA, double sideA, double upB, double sideB, double radius, Color tint,
                          double offset = .0018) {
        // Narrow surface ribbons keep folds/lips attached to the skin rather
        // than shading them as little cylindrical cords above the face.
        const double length = std::hypot(upB - upA, sideB - sideA);
        if (length < 1e-8)
            return;
        const double du = -(sideB - sideA) * radius / length, ds = (upB - upA) * radius / length;
        emitBegin(gfx::QUADS);
        for (int i = 0; i < 4; ++i) {
            const double a = i / 4.0, b = (i + 1) / 4.0;
            const auto edge = [&](double t, double side) {
                return face(upA + (upB - upA) * t + du * side,
                            sideA + (sideB - sideA) * t + ds * side, offset);
            };
            quad(edge(a, -1), edge(b, -1), edge(b, 1), edge(a, 1), tint);
        }
        emitEnd();
    };
    const Color crease = shade(skin, .88), eyelid = shade(skin, .96);
    const Color eyebrow{hair.r * .60f + .30f, hair.g * .60f + .29f, hair.b * .60f + .34f, hair.a};
    for (double side : {-1.0, 1.0}) {
        ellipsoid(at(-.002, -.038, side * .157), basis, {.026, .052, .023}, skin, 10, 6);
        ellipsoid(at(.019, -.035, side * .165), basis, {.008, .029, .010}, shade(skin, .84), 8, 5);
        // Small blue-grey eyes, largely covered by the upper and lower lids.
        const double eyeSide = side * .067;
        ellipsoid(face(.023, eyeSide, .001), basis, {.003, .0045, .024},
                  {.65f, .68f, .65f, skin.a}, 12, 5);
        ellipsoid(face(.023, eyeSide, .004), basis, {.002, .0038, .007},
                  {.20f, .31f, .35f, skin.a}, 10, 5);
        ellipsoid(face(.023, eyeSide, .006), basis, {.001, .0025, .003},
                  {.11f, .14f, .15f, skin.a}, 8, 4);
        for (int i = 0; i < 6; ++i) {
            const double a = i / 6.0, b = (i + 1) / 6.0;
            const double sa = eyeSide + side * (-.028 + .056 * a), sb = eyeSide + side * (-.028 + .056 * b);
            line(.023 + .007 * std::sin(a * PI), sa, .023 + .007 * std::sin(b * PI), sb, .0035, eyelid, .006);
            line(.021 - .005 * std::sin(a * PI), sa, .021 - .005 * std::sin(b * PI), sb, .0025, eyelid, .006);
            line(.007 - .007 * std::sin(a * PI), sa, .007 - .007 * std::sin(b * PI), sb, .0013, crease);
            line(.045 + .008 * std::sin(a * PI) - .004 * a, sa,
                 .045 + .008 * std::sin(b * PI) - .004 * b, sb, .0045, eyebrow, .0025);
        }
        for (int i = 0; i < 3; ++i)
            line(.023, side * .099, .033 - i * .011, side * .121, .0012, crease);
        line(-.060, side * .043, -.080, side * .070, .0010, crease);
        line(-.080, side * .070, -.108, side * .088, .0010, crease);
        line(.056, side * .018, .085, side * .020, .0010, crease);
    }
    // Blend a single broad nose into the face, with no spherical seams between
    // bridge, tip and wings. Rows run upwards to match the head's winding.
    struct NoseSection { double up, width, projection; };
    constexpr std::array<NoseSection, 9> noseSections{{
        {-.070, .012, 0}, {-.064, .033, .008}, {-.050, .044, .032}, {-.035, .035, .044},
        {-.014, .024, .037}, {.012, .020, .022}, {.034, .019, .008}, {.046, .018, .002}, {.054, .016, 0}}};
    std::array<std::array<Vec3, 13>, noseSections.size()> nose{};
    for (size_t r = 0; r < nose.size(); ++r)
        for (size_t i = 0; i < nose[r].size(); ++i) {
            const double t = -1 + i / 6.0;
            const auto &s = noseSections[r];
            nose[r][i] = face(s.up, t * s.width,
                             .001 + s.projection * std::pow(std::max(0.0, std::cos(t * PI / 2)), .75));
        }
    drawSkin(nose, false);
    for (double side : {-1.0, 1.0}) {
        ellipsoid(face(-.062, side * .025, .021), basis, {.003, .0025, .008}, shade(skin, .64), 8, 4);
        line(-.076, side * .012, -.092, side * .010, .0011, crease);
    }
    // Thin closed lips with slightly raised, asymmetric corners and a broad chin.
    const auto mouthUp = [](double t) { return -.108 + .006 * t * t + .004 * t; };
    for (int i = 0; i < 12; ++i) {
        const double a = -1 + i / 6.0, b = -1 + (i + 1) / 6.0;
        line(mouthUp(a) - .004, a * .080, mouthUp(b) - .004, b * .080,
             .0025, shade(skin, .99), .0045);
        line(mouthUp(a), a * .080, mouthUp(b), b * .080,
             .0015, {.52f, .30f, .27f, skin.a}, .006);
    }
    line(-.150, -.028, -.153, .028, .0012, shade(skin, .90));
    material(Finish::Fabric);
    // Keep the crown close to the scalp and below the resting fringe. A subtle
    // continuous tint avoids a separate pale dome above the swept-over lock.
    constexpr int slices = 16, rings = 5;
    for (int ring = 0; ring < rings; ++ring) {
        emitBegin(gfx::QUAD_STRIP);
        for (int slice = 0; slice <= slices; ++slice) {
            const double angle = slice * 2 * PI / slices;
            const double bottom = .08 + .57 * std::max(0.0, std::cos(angle)) -
                                  .28 * std::max(0.0, -std::cos(angle));
            for (int edge : {ring, ring + 1}) {
                const double latitude = bottom + (PI * .5 - bottom) * edge / rings;
                const double s = std::sin(latitude), c = std::cos(latitude);
                const Vec3 latitudeTangent{-.170 * s * std::cos(angle) + .028 * c,
                                           (.192 - .160 * s * s * s) * c,
                                           -.151 * s * std::sin(angle) - .018 * c};
                const Vec3 angleTangent{-.170 * std::sin(angle), 0, .151 * std::cos(angle)};
                const Vec3 localNormal = cross(latitudeTangent, angleTangent).normalized();
                const Vec3 normal = basis.forward * localNormal.x + basis.up * localNormal.y +
                                    basis.side * localNormal.z;
                emitNormal(normal.x, normal.y, normal.z);
                const float highlight = float(.20 * s * s);
                color({hair.r + (hairLight.r - hair.r) * highlight,
                       hair.g + (hairLight.g - hair.g) * highlight,
                       hair.b + (hairLight.b - hair.b) * highlight, hair.a});
                vertex(at(.170 * c * std::cos(angle) + .028 * s,
                          .018 + .192 * s - .040 * s * s * s * s,
                          .151 * c * std::sin(angle) - .018 * s));
            }
        }
        emitEnd();
    }
    const Vec3 sweptForward = (basis.forward * .84 + basis.side * .54).normalized();
    const Vec3 sweptSide = (basis.side * .84 - basis.forward * .54).normalized();
    const Vec3 vertical{0, 1, 0};
    Vec3 hinge = cross(sweptForward, vertical);
    hinge = hinge.length() > 1e-6 ? hinge.normalized() : sweptSide;
    const double angle = lift * std::acos(clamp(dot(sweptForward, vertical), -1.0, 1.0));
    const auto rotate = [&](Vec3 v) {
        return v * std::cos(angle) + cross(hinge, v) * std::sin(angle) +
               hinge * (dot(hinge, v) * (1 - std::cos(angle)));
    };
    // World up keeps the fringe vertical even with the head pitched forward
    // in flight. Rotate the whole basis so the lock keeps its volume.
    const Basis sweep{rotate(sweptForward), rotate(basis.up), rotate(sweptSide)};
    // Keep the root in the scalp while the longer fringe pivots upright.
    ellipsoid(at(.025, .145, -.020) + sweep.forward * .085, sweep, {.160, .045, .145}, hair, 14, 5);
}
constexpr std::array<double, 7> jacketHeights{0, .10, .27, .45, .64, .82, 1};
struct JacketSection {
    double front, back, width, shift;
};
JacketSection jacketSection(size_t ring) {
    const double t = jacketHeights[ring], belly = std::sin(PI * clamp(t / .90, 0.0, 1.0));
    const double hem = ring == 0 ? .90 : ring == jacketHeights.size() - 1 ? .76 : 1;
    return {(.19 * (1 - t) + .15 * t + .12 * belly) * hem,
            (.15 + .025 * belly) * hem,
            (.22 * (1 - t) + .235 * t + .075 * belly) * hem, .055 * belly};
}
void presidentTorso(Vec3 hip, Vec3 shoulder, const Basis &basis, Color suit, Color white, Color lapel, Color tie) {
    // Lower the full belly and hem without shrinking any cross-section. The
    // shoulder stays anchored; the longer jacket retains all its original bulk.
    const auto center = [&](double t) {
        return hip + (shoulder - hip) * t - basis.up * (.13 * (1 - t));
    };
    const auto point = [&](size_t ring, double angle) {
        const auto section = jacketSection(ring);
        const double c = std::cos(angle);
        return center(jacketHeights[ring]) +
               basis.forward * (section.shift + c * (c >= 0 ? section.front : section.back)) +
               basis.side * (std::sin(angle) * section.width);
    };
    emitBegin(gfx::QUADS);
    for (size_t ring = 0; ring + 1 < jacketHeights.size(); ++ring)
        for (int side = 0; side < 16; ++side) {
            const double a = side * PI / 8, b = (side + 1) * PI / 8;
            quad(point(ring, a), point(ring + 1, a), point(ring + 1, b), point(ring, b),
                 shade(suit, side >= 7 && side <= 9 ? .90 : 1));
        }
    emitEnd();
    for (size_t ring : {size_t(0), jacketHeights.size() - 1}) {
        color(suit);
        emitBegin(gfx::TRIANGLE_FAN);
        const Vec3 normal = basis.up * (ring == 0 ? -1 : 1);
        emitNormal(normal.x, normal.y, normal.z);
        vertex(center(jacketHeights[ring]));
        for (int i = 0; i <= 16; ++i)
            vertex(point(ring, (ring == 0 ? i : -i) * PI / 8));
        emitEnd();
    }
    // Subdivide the shirt, lapels and tie at every belly ring so they follow
    // the jacket surface in all poses instead of cutting through its middle.
    const auto frontAt = [&](size_t ring, double side) {
        const auto section = jacketSection(ring);
        const double angle = std::asin(clamp(std::abs(side) / section.width, 0.0, 1.0));
        const double a = std::floor(angle / (PI / 8)) * PI / 8, b = std::min(PI / 2, a + PI / 8);
        const double blend = b > a ? (std::abs(side) / section.width - std::sin(a)) /
                                        (std::sin(b) - std::sin(a)) : 0;
        return section.shift + section.front * (std::cos(a) * (1 - blend) + std::cos(b) * blend);
    };
    const auto surface = [&](double t, double side, double offset) {
        const auto upper = std::upper_bound(jacketHeights.begin(), jacketHeights.end(), t);
        const size_t r = std::min(size_t(upper - jacketHeights.begin()), jacketHeights.size() - 1);
        const size_t l = r ? r - 1 : 0;
        const double blend = r != l ? (t - jacketHeights[l]) / (jacketHeights[r] - jacketHeights[l]) : 0;
        const double depth = frontAt(l, side) * (1 - blend) + frontAt(r, side) * blend;
        return center(t) + basis.forward * (depth + offset) + basis.side * side;
    };
    const auto patch = [&](double low, double leftLow, double rightLow, double high,
                           double leftHigh, double rightHigh, Color tint, double offset) {
        if (leftLow > rightLow)
            std::swap(leftLow, rightLow);
        if (leftHigh > rightHigh)
            std::swap(leftHigh, rightHigh);
        double previous = low;
        for (double boundary : jacketHeights) {
            const double next = std::min(high, boundary);
            if (next <= previous)
                continue;
            const auto edge = [&](double t, double a, double b) {
                return surface(t, a + (b - a) * ((t - low) / (high - low)), offset);
            };
            emitBegin(gfx::QUADS);
            quad(edge(previous, leftLow, leftHigh), edge(next, leftLow, leftHigh),
                 edge(next, rightLow, rightHigh), edge(previous, rightLow, rightHigh), tint);
            emitEnd();
            previous = next;
        }
    };
    patch(.48, -.028, .028, .99, -.090, .090, white, .006);
    for (double side : {-1.0, 1.0})
        patch(.35, side * .014, side * .014, .99, side * .045, side * .13, lapel, .010);
    patch(.78, -.012, .012, .88, -.024, .024, tie, .017);
    patch(.36, -.031, .031, .78, -.012, .012, tie, .018);
    patch(.29, 0, 0, .36, -.031, .031, tie, .018);
}
void actorHead(Vec3 head, const Basis &basis, Color skin, Color hair, Color gold, Color lens, Color white) {
    const auto at = [&](double forward, double up, double side = 0) {
        return head + basis.forward * forward + basis.up * up + basis.side * side;
    };
    material(Finish::Skin);
    ellipsoid(at(.006, -.018), basis, {.139, .194, .143}, skin, 16, 12);
    const auto face = [&](double up, double side, double offset = .002) {
        const double surface = 1 - std::pow((up + .018) / .194, 2) - std::pow(side / .143, 2);
        return at(.006 + .139 * std::sqrt(std::max(0.0, surface)) + offset, up, side);
    };
    for (double side : {-1.0, 1.0})
        ellipsoid(at(-.006, -.026, side * .141), basis, {.024, .045, .020}, skin, 8, 5);
    ellipsoid(at(.142, -.004), basis, {.023, .052, .024}, skin, 10, 6);
    ellipsoid(at(.166, -.042), basis, {.029, .023, .031}, skin, 10, 6);
    // Broad smile and thick, downward-tipped moustache are visible below the glasses.
    emitBegin(gfx::QUADS);
    for (int i = 0; i < 12; ++i) {
        const double a = -1 + i / 6.0, b = a + 1.0 / 6;
        quad(face(-.124 + .020 * a * a, a * .069), face(-.092 + .004 * a * a, a * .069),
             face(-.092 + .004 * b * b, b * .069), face(-.124 + .020 * b * b, b * .069), shade(skin, .53));
        quad(face(-.110 + .011 * a * a, a * .058, .0035), face(-.099 + .008 * a * a, a * .058, .0035),
             face(-.099 + .008 * b * b, b * .058, .0035), face(-.110 + .011 * b * b, b * .058, .0035), white);
    }
    emitEnd();
    material(Finish::Fabric);
    for (double side : {-1.0, 1.0}) {
        ellipsoid(face(-.073, side * .030, .009), basis, {.010, .017, .039}, hair, 10, 5);
        ellipsoid(face(-.084, side * .060, .006), basis, {.007, .019, .013}, hair, 8, 4);
        ellipsoid(face(.071, side * .063, .005), basis, {.009, .010, .039}, hair, 8, 4);
    }
    // Wavy cap plus a small number of coarse curls keeps the silhouette curly
    // without creating hundreds of individual hair spheres.
    const auto hairBottom = [](double angle) {
        return -.08 + .65 * std::max(0.0, std::cos(angle)) - .20 * std::max(0.0, -std::cos(angle));
    };
    const auto hairPoint = [&](double latitude, double angle) {
        const double bottom = hairBottom(angle);
        const double growth = clamp((latitude - bottom) / (PI / 2 - bottom), 0.0, 1.0);
        const double c = std::cos(latitude), s = std::sin(latitude);
        const double volume = 1 + .45 * std::sqrt(growth);
        const double wave = 1 + .045 * std::sin(angle * 7 + latitude * 17);
        // Grow away from the existing hairline, keeping the roots on the scalp.
        return at(.151 * c * std::cos(angle) * wave * volume,
                  .010 + .207 * s + .075 * growth + .005 * c * std::sin(angle * 9),
                  .153 * c * std::sin(angle) * wave * volume);
    };
    constexpr int slices = 20, rings = 6;
    for (int ring = 0; ring < rings; ++ring) {
        color(hair);
        emitBegin(gfx::QUAD_STRIP);
        for (int slice = 0; slice <= slices; ++slice) {
            const double angle = slice * 2 * PI / slices;
            const double bottom = hairBottom(angle);
            for (int edge : {ring, ring + 1}) {
                const double latitude = bottom + (PI / 2 - bottom) * edge / rings;
                const double c = std::cos(latitude), s = std::sin(latitude);
                const Vec3 normal = basis.forward * (c * std::cos(angle)) + basis.up * s +
                                    basis.side * (c * std::sin(angle));
                emitNormal(normal.x, normal.y, normal.z);
                vertex(hairPoint(latitude, angle));
            }
        }
        emitEnd();
    }
    for (int i = 0; i < 12; ++i) {
        const double angle = i * 2 * PI / 12, latitude = .58 + .09 * std::sin(i * 2.3);
        ellipsoid(hairPoint(latitude, angle), basis, {.027, .030, .027},
                  shade(hair, i % 3 == 0 ? 1.13 : .96), 6, 4);
    }
    for (int i = 0; i < 8; ++i) {
        const double angle = i * PI / 4, latitude = 1.12 + .04 * std::sin(i * 1.9);
        ellipsoid(hairPoint(latitude, angle), basis, {.029, .029, .029},
                  shade(hair, i % 2 ? .97 : 1.10), 6, 4);
    }
    ellipsoid(hairPoint(PI / 2, 0), basis, {.031, .026, .030}, hair, 6, 4);
    // Golden aviator frames, brown lenses, nose bridge and arms to the ears.
    constexpr std::array<std::array<double, 2>, 12> contour{{
        {-1, .28}, {-.82, .82}, {-.32, 1}, {.28, .96}, {.80, .65}, {1, .15},
        {.90, -.45}, {.50, -.92}, {-.10, -1}, {-.62, -.75}, {-.94, -.26}, {-1, .04}}};
    for (double side : {-1.0, 1.0}) {
        const auto framePoint = [&](size_t index, double scale) {
            const auto &p = contour[index % contour.size()];
            return at(.155, .024 + p[1] * .036 * scale, side * (.064 + p[0] * .050 * scale));
        };
        material(Finish::Metal);
        emitBegin(gfx::QUADS);
        for (size_t i = 0; i < contour.size(); ++i) {
            if (side > 0)
                quad(framePoint(i, 1), framePoint(i + 1, 1), framePoint(i + 1, .94), framePoint(i, .94), gold);
            else
                quad(framePoint(i, .94), framePoint(i + 1, .94), framePoint(i + 1, 1), framePoint(i, 1), gold);
        }
        emitEnd();
        tailoredSegment(at(.148, .038, side * .108), at(-.010, .025, side * .143),
                        .003, .003, .003, .003, gold);
        material(Finish::Glass);
        color(lens);
        emitBegin(gfx::TRIANGLE_FAN);
        emitNormal(basis.forward.x, basis.forward.y, basis.forward.z);
        vertex(at(.155, .024, side * .064));
        for (size_t i = 0; i <= contour.size(); ++i)
            vertex(framePoint(side > 0 ? i : contour.size() - i, .94));
        emitEnd();
    }
    material(Finish::Metal);
    tailoredSegment(at(.169, .039, -.017), at(.169, .039, .017), .003, .003, .003, .003, gold);
}

void actorLimbHair(Vec3 a, Vec3 b, double frontA, double sideA, double frontB, double sideB, Color hair) {
    const Basis basis = segmentBasis(b - a);
    const auto surface = [&](double t, double angle) {
        return a + (b - a) * t + basis.forward * ((frontA + (frontB - frontA) * t + .0015) * std::cos(angle)) +
               basis.side * ((sideA + (sideB - sideA) * t + .0015) * std::sin(angle));
    };
    emitBegin(gfx::QUADS);
    for (int i = 0; i < 18; ++i) {
        const double t = .20 + (i % 6) * .115, angle = -.85 + (i / 6) * .78 + .09 * std::sin(i * 2.1);
        quad(surface(t, angle - .008), surface(t + .021, angle + .025),
             surface(t + .021, angle + .041), surface(t, angle + .008), hair);
    }
    emitEnd();
}

void actorTorso(Vec3 hip, Vec3 shoulder, const Basis &basis, Color skin, Color swimwear, Color hair) {
    constexpr std::array<double, 6> heights{0, .18, .38, .60, .82, 1};
    constexpr std::array<double, 6> widths{.158, .155, .173, .205, .226, .208};
    constexpr std::array<double, 6> fronts{.108, .105, .119, .137, .144, .124};
    const auto section = [&](double t, const auto &values) {
        size_t r = 1;
        while (r + 1 < heights.size() && heights[r] < t)
            ++r;
        const double u = clamp((t - heights[r - 1]) / (heights[r] - heights[r - 1]), 0.0, 1.0);
        return values[r - 1] + (values[r] - values[r - 1]) * u;
    };
    const auto surface = [&](double t, double side, bool back, double offset) {
        const double w = section(t, widths), depth = back ? .115 : section(t, fronts);
        const double x = depth * std::sqrt(std::max(0.0, 1 - side * side / (w * w))) + offset;
        return hip + (shoulder - hip) * t + basis.side * side + basis.forward * (back ? -x : x);
    };
    material(Finish::Skin);
    ellipsoid(hip - basis.up * .005, basis, {.125, .125, .168}, skin, 14, 7);
    emitBegin(gfx::QUADS);
    for (size_t r = 0; r + 1 < heights.size(); ++r)
        for (int i = 0; i < 20; ++i) {
            const auto point = [&](size_t row, int slice) {
                const double angle = slice * 2 * PI / 20, c = std::cos(angle);
                return hip + (shoulder - hip) * heights[row] + basis.side * (widths[row] * std::sin(angle)) +
                       basis.forward * ((c >= 0 ? fronts[row] : .115) * c);
            };
            quad(point(r, i), point(r + 1, i), point(r + 1, i + 1), point(r, i + 1), skin);
        }
    emitEnd();
    color(skin);
    emitBegin(gfx::TRIANGLE_FAN);
    emitNormal(basis.up.x, basis.up.y, basis.up.z);
    vertex(shoulder);
    for (int i = 0; i <= 20; ++i) {
        const double angle = -i * PI / 10, c = std::cos(angle);
        vertex(shoulder + basis.side * (widths.back() * std::sin(angle)) +
               basis.forward * ((c >= 0 ? fronts.back() : .115) * c));
    }
    emitEnd();
    // Deterministic short ribbons suggest the reference's chest/abdominal hair.
    // They follow the same surface in live play, ghosts, falls and replay seeks.
    emitBegin(gfx::QUADS);
    for (int i = 0; i < 450; ++i) {
        const auto fraction = [](double v) { return v - std::floor(v); };
        const double scatter = fraction(i * .618033989);
        const double t = i < 300 ? .08 + scatter * .84 : .38 + scatter * .54;
        const double extent = .047 + .115 * std::sin(t * PI);
        const double side = extent * (-.94 + 1.88 * fraction(i * .754877666));
        const double bend = .0035 * std::sin(i * 1.7), length = .022 + .014 * fraction(i * .414213562);
        quad(surface(t, side - .0011, false, .0025), surface(t + length, side + bend - .0011, false, .0025),
             surface(t + length, side + bend + .0011, false, .0025), surface(t, side + .0011, false, .0025), hair);
    }
    emitEnd();
    material(Finish::Fabric);
    // Opaque front/back coverage at the pelvis; there is no genital geometry.
    constexpr std::array<double, 5> bottomHeights{-.172, -.125, -.060, .015, .085};
    constexpr std::array<double, 5> bottomWidths{.016, .058, .086, .082, .065};
    constexpr std::array<double, 5> frontDepths{.165, .220, .215, .157, .119};
    constexpr std::array<double, 5> backDepths{.100, .153, .160, .145, .119};
    const auto bottom = [&](size_t row, double side, bool back) {
        const double up = bottomHeights[row];
        const double depth = (back ? backDepths[row] : frontDepths[row]) -
                             .015 * std::pow(side / bottomWidths[row], 2);
        return hip + basis.up * up + basis.side * side + basis.forward * (back ? -depth : depth);
    };
    emitBegin(gfx::QUADS);
    for (bool back : {false, true}) {
        for (size_t r = 0; r + 1 < bottomHeights.size(); ++r)
            for (int i = 0; i < 8; ++i) {
                const double a = -1 + i / 4.0, b = a + .25;
                if (back)
                    quad(bottom(r, a * bottomWidths[r], true), bottom(r, b * bottomWidths[r], true),
                         bottom(r + 1, b * bottomWidths[r + 1], true), bottom(r + 1, a * bottomWidths[r + 1], true), swimwear);
                else
                    quad(bottom(r, a * bottomWidths[r], false), bottom(r + 1, a * bottomWidths[r + 1], false),
                         bottom(r + 1, b * bottomWidths[r + 1], false), bottom(r, b * bottomWidths[r], false), swimwear);
            }
        for (double sign : {-1.0, 1.0}) {
            const auto edge = [&](double t, double across) {
                return surface(t, sign * (.050 + .120 * t) + across * .013, back, .005);
            };
            for (size_t r = 0; r + 1 < heights.size(); ++r) {
                const double a = heights[r], b = heights[r + 1];
                if (back)
                    quad(edge(a, -1), edge(a, 1), edge(b, 1), edge(b, -1), swimwear);
                else
                    quad(edge(a, -1), edge(b, -1), edge(b, 1), edge(a, 1), swimwear);
            }
        }
    }
    for (double sign : {-1.0, 1.0})
        quad(surface(1, sign * .170 - .013, false, .005), surface(1, sign * .170 + .013, false, .005),
             surface(1, sign * .170 + .013, true, .005), surface(1, sign * .170 - .013, true, .005), swimwear);
    quad(bottom(0, -.016, false), bottom(0, .016, false), bottom(0, .016, true), bottom(0, -.016, true), swimwear);
    emitEnd();
}

void drawAthleteGeometry(const OriginalSkeleton &skeleton, bool ghost, JumperAppearance appearance,
                         double hairLift = 0) {
    const auto point = [&](OriginalJoint joint) { return skeleton[joint]; };
    const float alpha = ghost ? .35f : 1;
    auto tint = [&](Color c) {
        if (ghost)
            return Color{c.r * .25f + .24f, c.g * .23f + .62f, c.b * .24f + .59f, alpha};
        c.a = alpha;
        return c;
    };
    const bool president = appearance == JumperAppearance::President, actor = appearance == JumperAppearance::Actor;
    const Color suit = tint(president ? Color{.075f, .105f, .17f} : Color{.94f, .31f, .13f}),
                dark = tint({.055f, .13f, .19f}), white = tint({.91f, .95f, .95f}),
                accent = tint({.23f, .82f, .75f}),
                visor = tint({.22f, .69f, .82f}),
                bootShell = tint({.86f, .88f, .85f}), bootSole = tint({.065f, .075f, .085f}),
                bootStrap = tint({.78f, .13f, .09f}), bootBuckle = tint({.62f, .66f, .68f}),
                skin = tint(actor ? Color{.79f, .57f, .43f} : president ? Color{.94f, .61f, .50f} : Color{.77f, .56f, .43f}),
                actorHair = tint({.13f, .105f, .080f}), bodyHair = tint({.29f, .19f, .13f}),
                swimwear = tint({.53f, .96f, .10f});
    const OriginalJoint rear[]{OriginalJoint::SkiRear1, OriginalJoint::SkiRear2};
    const OriginalJoint front[]{OriginalJoint::SkiFront1, OriginalJoint::SkiFront2};
    const OriginalJoint tips[]{OriginalJoint::SkiTip1, OriginalJoint::SkiTip2};
    const OriginalJoint ankles[]{OriginalJoint::Ankle1, OriginalJoint::Ankle2};
    const OriginalJoint knees[]{OriginalJoint::Knee1, OriginalJoint::Knee2};
    const OriginalJoint hips[]{OriginalJoint::Hip1, OriginalJoint::Hip2};
    const OriginalJoint shoulders[]{OriginalJoint::Shoulder1, OriginalJoint::Shoulder2};
    const OriginalJoint elbows[]{OriginalJoint::Elbow1, OriginalJoint::Elbow2};
    const OriginalJoint hands[]{OriginalJoint::Hand1, OriginalJoint::Hand2};
    for (size_t leg = 0; leg < 2; ++leg) {
        const Vec3 ankle = point(ankles[leg]), knee = point(knees[leg]), hip = point(hips[leg]),
                   shoulder = point(shoulders[leg]), elbow = point(elbows[leg]), hand = point(hands[leg]);
        ski(point(rear[leg]), point(front[leg]), point(tips[leg]), ankle, dark, shade(dark, .75), accent);
        material(actor ? Finish::Skin : Finish::Fabric);
        // Tuck the trouser hem into the cuff instead of drawing a dark tube
        // through the entire light boot. All skeleton joints stay untouched.
        const Vec3 trouserHem = ankle + (knee - ankle).normalized() * .215;
        if (actor) {
            tailoredSegment(trouserHem, knee, .075, .075, .100, .095, skin);
            tailoredSegment(knee, hip, .100, .095, .135, .125, skin);
            ellipsoid(knee, segmentBasis(hip - ankle), {.097, .115, .096}, skin, 8, 4);
            ellipsoid(shoulder, segmentBasis(elbow - shoulder), {.105, .110, .105}, skin, 8, 4);
            tailoredSegment(shoulder, elbow, .095, .095, .073, .076, skin);
            tailoredSegment(elbow, hand, .076, .074, .055, .058, skin);
            ellipsoid(elbow, segmentBasis(shoulder - hand), {.073, .080, .075}, skin, 8, 4);
            actorLimbHair(trouserHem, knee, .075, .075, .100, .095, bodyHair);
            actorLimbHair(knee, hip, .100, .095, .135, .125, bodyHair);
            actorLimbHair(shoulder, elbow, .095, .095, .073, .076, bodyHair);
            actorLimbHair(elbow, hand, .076, .074, .055, .058, bodyHair);
        } else {
            tailoredSegment(trouserHem, knee, .095, .09, .12, .11, president ? suit : dark);
            tailoredSegment(knee, hip, .12, .115, .15, .13, suit);
            ellipsoid(knee, segmentBasis(hip - ankle), {.115, .13, .112}, suit, 8, 4);
            tailoredSegment(shoulder, elbow, .105, .105, .083, .086, suit);
            tailoredSegment(elbow, hand, .086, .084, .065, .068, suit);
            ellipsoid(elbow, segmentBasis(shoulder - hand), {.083, .09, .085}, suit, 8, 4);
        }
        const Basis handBasis = segmentBasis(elbow - hand);
        material(president || actor ? Finish::Skin : Finish::Rubber);
        ellipsoid(hand, handBasis, {.077, .11, .075}, president || actor ? skin : dark, 8, 5);
        ellipsoid(hand + handBasis.forward * .05 + handBasis.side * (leg == 0 ? -.045 : .045), handBasis,
                  {.044, .057, .038}, president || actor ? skin : dark, 6, 4);
        if (!actor)
            tailoredSegment(hand + handBasis.up * .045, hand + handBasis.up * .1, .074, .074, .075, .075, white);
        else if (leg == 0) {
            material(Finish::Rubber);
            tailoredSegment(hand + handBasis.up * .055, hand + handBasis.up * .085,
                            .070, .070, .070, .070, actorHair);
            orientedBox(hand + handBasis.up * .070 + handBasis.forward * .071,
                        handBasis, {.016, .032, .040}, bootBuckle);
        }
        skiBoot(ankle, knee, skiBasis(point(rear[leg]), point(front[leg])), bootShell, bootSole, bootStrap, bootBuckle);
    }
    const Vec3 hip = point(OriginalJoint::HipCenter), shoulder = point(OriginalJoint::ShoulderCenter),
               head = point(OriginalJoint::Head);
    material(Finish::Fabric);
    const Basis torso = segmentBasis(shoulder - hip);
    if (actor)
        actorTorso(hip, shoulder, torso, skin, swimwear, bodyHair);
    else if (president)
        presidentTorso(hip, shoulder, torso, suit, white, tint({.12f, .155f, .23f}), tint({.80f, .045f, .065f}));
    else
        tailoredSegment(hip, shoulder, .135, .17, .15, .235, suit, true, white);
    if (!actor)
        tailoredSegment(hip + torso.up * .008, hip + torso.up * .052, .143, .182, .144, .184, dark);
    material(actor ? Finish::Skin : Finish::Fabric);
    tailoredSegment(shoulder, head - (head - shoulder).normalized() * .09, .075, .078, .075, .078,
                    actor ? skin : president ? white : dark);
    const Basis headBasis = segmentBasis(head - shoulder);
    if (actor)
        actorHead(head, headBasis, skin, actorHair, tint({.76f, .68f, .49f}), tint({.20f, .12f, .065f}), white);
    else if (president)
        presidentHead(head, headBasis, skin, tint({.95f, .81f, .53f}), tint({1.f, .90f, .67f}), hairLift);
    else
        helmet(head, headBasis, dark, white, visor, skin);
}
} // namespace

Frame displayedAthleteFrame(const Hill &h, const Frame &f, JumperAppearance appearance) {
    Frame display = f;
    if (!f.crashed || (Phase(f.phase) != Phase::Outrun && Phase(f.phase) != Phase::Finished))
        return display;
    auto pose = f.pose;
    if (pose[18] == 0 && pose[20] == 0) {
        OriginalPose fallback;
        fallback.reset(std::atan(f.x <= 0 ? h.inrunSlope(f.x) : h.slope(f.x)));
        std::copy(fallback.angles.begin(), fallback.angles.end(), pose.begin());
        std::copy(fallback.offsets.begin(), fallback.offsets.end(), pose.begin() + 18);
    }
    const auto skeleton =
        f.hasRenderSkeleton ? OriginalSkeleton{f.renderSkeleton} : originalSkeleton(pose, f.leadingLeg);
    FallContact contact(h, f, skeleton);
    struct MeasurementScope {
        FallContact *previous;
        explicit MeasurementScope(FallContact &contact) : previous(contactPass) { contactPass = &contact; }
        ~MeasurementScope() { contactPass = previous; }
    } scope(contact);
    // Measure the actual skin without GL calls; only ski ends get snow allowance.
    // One rigid translation preserves all joint angles, limb lengths and rolling.
    drawAthleteGeometry(skeleton, false, appearance, fringeLift(f));
    display.y = float(double(f.y) + contact.lift);
    return display;
}

void Renderer::athlete(const Hill &h, const Frame &f, bool ghost) {
    auto pose = f.pose;
    if (pose[18] == 0 && pose[20] == 0) {
        OriginalPose fallback;
        fallback.reset(std::atan(f.x <= 0 ? h.inrunSlope(f.x) : h.slope(f.x)));
        std::copy(fallback.angles.begin(), fallback.angles.end(), pose.begin());
        std::copy(fallback.offsets.begin(), fallback.offsets.end(), pose.begin() + 18);
    }
    const auto skeleton =
        f.hasRenderSkeleton ? OriginalSkeleton{f.renderSkeleton} : originalSkeleton(pose, f.leadingLeg);
    sjp::gfx::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::LIGHTING_BIT | sjp::gfx::TEXTURE_BIT | sjp::gfx::COLOR_BUFFER_BIT |
                 sjp::gfx::DEPTH_BUFFER_BIT);
    sjp::gfx::disable(sjp::gfx::TEXTURE_2D);
    // Enable subtle sun/moon highlights only within this restored athlete
    // scope. Cloudy conditions attenuate them with the existing direct light.
    const float specularLight[]{float(lighting.direct.x * .60), float(lighting.direct.y * .60),
                                float(lighting.direct.z * .60), 1};
    const float noHighlights[]{0, 0, 0, 1};
    sjp::gfx::lightfv(gfx::LIGHT0, gfx::SPECULAR, ghost ? noHighlights : specularLight);
    sjp::gfx::pushMatrix();
    sjp::gfx::translated(f.x, f.y, 0);
    drawAthleteGeometry(skeleton, ghost, jumperAppearance, fringeLift(f));
    sjp::gfx::popMatrix();
    sjp::gfx::popAttrib();
}

void Renderer::jumperPreviewGeometry(JumperAppearance appearance) {
    // The original standing pose is shared by both previews; only its skin
    // changes. Preview lighting and transforms never touch the live simulation.
    static const OriginalSkeleton standing = [] {
        OriginalPose pose;
        pose.reset(0);
        pose.selectMode(7, 0);
        for (int i = 0; i < 280; ++i)
            pose.advance(ORIGINAL_TICK);
        std::array<float, 22> values{};
        std::copy(pose.angles.begin(), pose.angles.end(), values.begin());
        std::copy(pose.offsets.begin(), pose.offsets.end(), values.begin() + 18);
        return originalSkeleton(values, 0);
    }();
    drawAthleteGeometry(standing, false, appearance);
}

} // namespace sjp
