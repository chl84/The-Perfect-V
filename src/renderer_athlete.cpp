#include "original_skeleton.h"
#include "renderer.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sjp {
namespace {
// Sample the existing rendered triangles, not a new collision surface. The
// small height cache covers the unchanged athlete mesh's local x extent.
struct FallContact {
    const Hill &hill;
    Vec3 origin;
    double start, step, firstX, lift = 0;
    std::vector<double> heights;
    sjp::gfx::Enum mode = sjp::gfx::QUADS;
    size_t count = 0;
    std::array<Vec3, 4> face{};
    FallContact(const Hill &h, const Frame &f, const OriginalSkeleton &skeleton)
        : hill(h), origin{f.x, f.y, 0}, start(std::min(-170 * h.scale(), h.inrunStart() - 60 * h.scale())),
          step(3 * h.scale()) {
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
    }
    double surface(double x, double z) const {
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
    void measure(Vec3 local) {
        const Vec3 p = local + origin;
        lift = std::max(lift, surface(p.x, p.z) + .008 - p.y);
    }
    void edge(Vec3 a, Vec3 b) {
        // A triangle edge can cross a terrain-grid bend between its endpoints.
        // Sample those crossings as well, retaining the original height grid.
        if (std::abs(b.x - a.x) < 1e-10)
            return;
        const double low = std::min(a.x, b.x) + origin.x;
        const double high = std::max(a.x, b.x) + origin.x;
        for (double x = firstX + std::ceil((low - firstX) / step) * step; x < high; x += step) {
            const double t = (x - origin.x - a.x) / (b.x - a.x);
            measure(a + (b - a) * t);
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
void ski(Vec3 rear, Vec3 front, Vec3 tip, Vec3 ankle, Color deck, Color edge, Color accent) {
    const Basis basis = skiBasis(rear, front);
    const std::array<Vec3, 3> centers{rear, front, tip};
    constexpr std::array<double, 3> widths{.071, .095, .066};
    constexpr double halfThickness = .018;
    auto corner = [&](size_t section, double across, double height) {
        return centers[section] + basis.side * (widths[section] * across) + basis.up * height;
    };
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
    orientedBox(ankle + basis.up * .025, basis, {.37, .045, .14}, edge);
    orientedBox(ankle - basis.forward * .13 + basis.up * .067, basis, {.08, .09, .13}, accent);
    orientedBox(ankle + basis.forward * .13 + basis.up * .046, basis, {.085, .055, .14}, accent);
}
void helmet(Vec3 head, const Basis &basis, Color shell, Color trim, Color visor, Color skin) {
    ellipsoid(head - basis.up * .054 + basis.forward * .025, basis, {.145, .16, .135}, skin);
    constexpr std::array<double, 7> latitudes{-.32, .05, .36, .71, 1.1, 1.4, PI * .5};
    constexpr int slices = 16;
    auto point = [&](double latitude, double angle) {
        return head + basis.forward * (.2 * std::cos(latitude) * std::cos(angle)) +
               basis.up * (.207 * std::sin(latitude)) +
               basis.side * (.192 * std::cos(latitude) * std::sin(angle));
    };
    emitBegin(sjp::gfx::QUADS);
    for (size_t ring = 0; ring + 1 < latitudes.size(); ++ring)
        for (int slice = 0; slice < slices; ++slice) {
            const double a = slice * 2 * PI / slices, b = (slice + 1) * 2 * PI / slices;
            const bool glass = ring < 2 && std::cos((a + b) * .5) > .7;
            const bool racingStripe = (slice == 7 || slice == 8) && ring > 1;
            const bool vent = ring == 3 && (slice == 4 || slice == 11);
            Color panel = glass          ? shade(visor, ring == 0 ? .72 : 1.07)
                          : racingStripe ? trim
                          : vent         ? shade(shell, .6)
                                         : shell;
            quad(point(latitudes[ring], a), point(latitudes[ring + 1], a), point(latitudes[ring + 1], b),
                 point(latitudes[ring], b), panel);
        }
    emitEnd();
    for (double side : {-1.0, 1.0}) {
        const Vec3 ear = head + basis.side * (.153 * side) - basis.up * .015;
        const Vec3 chin = head - basis.up * .175 + basis.forward * .045 + basis.side * (.08 * side);
        tailoredSegment(ear, chin, .011, .015, .011, .015, shade(shell, .66));
    }
}
void drawAthleteGeometry(const OriginalSkeleton &skeleton, bool ghost) {
    const auto point = [&](OriginalJoint joint) { return skeleton[joint]; };
    const float alpha = ghost ? .35f : 1;
    auto tint = [&](Color c) {
        if (ghost)
            return Color{c.r * .25f + .24f, c.g * .23f + .62f, c.b * .24f + .59f, alpha};
        c.a = alpha;
        return c;
    };
    const Color suit = tint({.94f, .31f, .13f}), dark = tint({.055f, .13f, .19f}),
                white = tint({.91f, .95f, .95f}), accent = tint({.23f, .82f, .75f}),
                visor = tint({.22f, .69f, .82f}), skin = tint({.77f, .56f, .43f});
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
        tailoredSegment(ankle, knee, .095, .09, .12, .11, dark);
        tailoredSegment(knee, hip, .12, .115, .15, .13, suit);
        ellipsoid(knee, segmentBasis(hip - ankle), {.115, .13, .112}, suit, 8, 4);
        tailoredSegment(shoulder, elbow, .105, .105, .083, .086, suit);
        tailoredSegment(elbow, hand, .086, .084, .065, .068, suit);
        ellipsoid(elbow, segmentBasis(shoulder - hand), {.083, .09, .085}, suit, 8, 4);
        const Basis handBasis = segmentBasis(elbow - hand);
        ellipsoid(hand, handBasis, {.077, .11, .075}, dark, 8, 5);
        ellipsoid(hand + handBasis.forward * .05 + handBasis.side * (leg == 0 ? -.045 : .045), handBasis,
                  {.044, .057, .038}, dark, 6, 4);
        tailoredSegment(hand + handBasis.up * .045, hand + handBasis.up * .1, .074, .074, .075, .075, white);
        const Basis boot = skiBasis(point(rear[leg]), point(front[leg]));
        ellipsoid(ankle + boot.forward * .055 + boot.up * .067, boot, {.19, .092, .098}, dark, 10, 5);
        tailoredSegment(ankle + boot.up * .05, ankle + boot.up * .19, .079, .078, .065, .074, dark);
        orientedBox(ankle + boot.forward * .09 + boot.up * .145, boot, {.035, .022, .167}, white);
        orientedBox(ankle + boot.forward * .15 + boot.up * .09, boot, {.032, .022, .16}, accent);
    }
    const Vec3 hip = point(OriginalJoint::HipCenter), shoulder = point(OriginalJoint::ShoulderCenter),
               head = point(OriginalJoint::Head);
    tailoredSegment(hip, shoulder, .135, .17, .15, .235, suit, true, white);
    const Basis torso = segmentBasis(shoulder - hip);
    tailoredSegment(hip + torso.up * .008, hip + torso.up * .052, .143, .182, .144, .184, dark);
    tailoredSegment(shoulder, head - (head - shoulder).normalized() * .09, .075, .078, .075, .078, dark);
    const Basis headBasis = segmentBasis(head - shoulder);
    helmet(head, headBasis, dark, white, visor, skin);
}
} // namespace

Frame displayedAthleteFrame(const Hill &h, const Frame &f) {
    Frame display = f;
    if (!f.crashed || f.x <= 0 || (Phase(f.phase) != Phase::Outrun && Phase(f.phase) != Phase::Finished))
        return display;
    auto pose = f.pose;
    if (pose[18] == 0 && pose[20] == 0) {
        OriginalPose fallback;
        fallback.reset(std::atan(h.slope(f.x)));
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
    // Measure the actual suit, helmet, boots, gloves and skis without GL calls.
    // One rigid translation preserves all joint angles, limb lengths and rolling.
    drawAthleteGeometry(skeleton, false);
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
    const float specular[]{.2f, .23f, .24f, 1};
    sjp::gfx::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SPECULAR, specular);
    sjp::gfx::materialf(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SHININESS, 34);
    sjp::gfx::pushMatrix();
    sjp::gfx::translated(f.x, f.y, 0);
    drawAthleteGeometry(skeleton, ghost);
    sjp::gfx::popMatrix();
    sjp::gfx::popAttrib();
}
} // namespace sjp
