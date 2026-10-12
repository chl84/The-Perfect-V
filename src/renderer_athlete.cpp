#include "original_skeleton.h"
#include "renderer.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sjp {
namespace {
void vertex(Vec3 p) {
    glVertex3d(p.x, p.y, p.z);
}
void color(Color c) {
    glColor4f(c.r, c.g, c.b, c.a);
}
Color shade(Color c, double amount) {
    return {float(c.r * amount), float(c.g * amount), float(c.b * amount), c.a};
}
void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color tint) {
    const Vec3 normal = cross(b - a, c - a).normalized();
    color(tint);
    glNormal3d(normal.x, normal.y, normal.z);
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
    glBegin(GL_QUADS);
    quad(center - x + y - z, center - x + y + z, center + x + y + z, center + x + y - z, tint);
    quad(center - x - y - z, center + x - y - z, center + x - y + z, center - x - y + z, shade(tint, .8));
    quad(center - x - y + z, center + x - y + z, center + x + y + z, center - x + y + z, tint);
    quad(center + x - y - z, center - x - y - z, center - x + y - z, center + x + y - z, tint);
    quad(center - x - y - z, center - x - y + z, center - x + y + z, center - x + y - z, tint);
    quad(center + x - y + z, center + x - y - z, center + x + y - z, center + x + y + z, tint);
    glEnd();
}
void ellipsoid(Vec3 center, const Basis &basis, Vec3 radii, Color tint, int slices = 10, int rings = 6) {
    color(tint);
    for (int ring = 0; ring < rings; ++ring) {
        const double a = -PI * .5 + ring * PI / rings, b = a + PI / rings;
        glBegin(GL_QUAD_STRIP);
        for (int slice = 0; slice <= slices; ++slice) {
            const double angle = slice * 2 * PI / slices;
            for (double latitude : {a, b}) {
                const double x = std::cos(latitude) * std::cos(angle), y = std::sin(latitude),
                             z = std::cos(latitude) * std::sin(angle);
                const Vec3 normal =
                    (basis.forward * (x / radii.x) + basis.up * (y / radii.y) + basis.side * (z / radii.z))
                        .normalized();
                glNormal3d(normal.x, normal.y, normal.z);
                vertex(center + basis.forward * (x * radii.x) + basis.up * (y * radii.y) +
                       basis.side * (z * radii.z));
            }
        }
        glEnd();
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
    glBegin(GL_QUADS);
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
    glEnd();
    for (bool end : {false, true}) {
        color(shade(tint, .92));
        glBegin(GL_TRIANGLE_FAN);
        const Vec3 normal = basis.up * (end ? 1 : -1);
        glNormal3d(normal.x, normal.y, normal.z);
        vertex(end ? b : a);
        for (int face = 0; face <= 8; ++face)
            vertex(ringPoint(end ? positions.size() - 1 : 0, (end ? -face : face) * PI / 4));
        glEnd();
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
    glBegin(GL_QUADS);
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
    glEnd();
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
    glBegin(GL_QUADS);
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
    glEnd();
    for (double side : {-1.0, 1.0}) {
        const Vec3 ear = head + basis.side * (.153 * side) - basis.up * .015;
        const Vec3 chin = head - basis.up * .175 + basis.forward * .045 + basis.side * (.08 * side);
        tailoredSegment(ear, chin, .011, .015, .011, .015, shade(shell, .66));
    }
}
} // namespace

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
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LIGHTING_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT |
                 GL_DEPTH_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    const GLfloat specular[]{.2f, .23f, .24f, 1};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 34);
    glPushMatrix();
    glTranslated(f.x, f.y, 0);
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
    glPopMatrix();
    glPopAttrib();
}
} // namespace sjp
