#include "renderer.h"
#include "original_skeleton.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "render_batch.h"
// Only this translation unit batches static terrain compilation. The wrapper
// forwards directly to OpenGL for the animated scenery pass.
#define glBegin sjp::visual::begin
#define glEnd sjp::visual::end
#define glColor4f sjp::visual::color
#define glNormal3d sjp::visual::normal
#define glTexCoord2d sjp::visual::texCoord
#define glVertex3d sjp::visual::vertex
#define glEnable sjp::visual::enable
#define glDisable sjp::visual::disable
#define glPushAttrib sjp::visual::pushAttrib
#define glPopAttrib sjp::visual::popAttrib
#define glMaterialfv sjp::visual::materialfv
#define glMaterialf sjp::visual::materialf
#define glBindTexture sjp::visual::bindTexture
#define glPolygonOffset sjp::visual::polygonOffset
#define glDepthMask sjp::visual::depthMask
#define glBlendFunc sjp::visual::blendFunc
#define glTexEnvi sjp::visual::texEnvi

namespace sjp {
namespace {
void col(Color c) {
    glColor4f(c.r, c.g, c.b, c.a);
}
void vertex(Vec3 p) {
    glVertex3d(p.x, p.y, p.z);
}
Color shade(Color c, double value) {
    return {float(c.r * value), float(c.g * value), float(c.b * value), c.a};
}
double hash(double n) {
    return std::fmod(std::abs(std::sin(n * 12.9898) * 43758.5453), 1.0);
}
void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    col(color);
    const Vec3 n = cross(b - a, c - a).normalized();
    glBegin(GL_QUADS);
    glNormal3d(n.x, n.y, n.z);
    vertex(a);
    vertex(b);
    vertex(c);
    vertex(d);
    glEnd();
}
void triangle(Vec3 a, Vec3 b, Vec3 c, Color color) {
    col(color);
    const Vec3 n = cross(b - a, c - a).normalized();
    glBegin(GL_TRIANGLES);
    glNormal3d(n.x, n.y, n.z);
    vertex(a);
    vertex(b);
    vertex(c);
    glEnd();
}
void box(Vec3 p, Vec3 size, Color c) {
    const Vec3 a = p - size * .5, b = p + size * .5;
    quad({a.x, b.y, a.z}, {a.x, b.y, b.z}, {b.x, b.y, b.z}, {b.x, b.y, a.z}, c);
    quad({a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z}, c);
    quad({a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}, c);
    quad({b.x, a.y, a.z}, {a.x, a.y, a.z}, {a.x, b.y, a.z}, {b.x, b.y, a.z}, c);
    quad({a.x, a.y, a.z}, {a.x, a.y, b.z}, {a.x, b.y, b.z}, {a.x, b.y, a.z}, c);
    quad({b.x, a.y, b.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, c);
}
void beam(Vec3 a, Vec3 b, double radius, Color color) {
    const Vec3 dir = (b - a).normalized();
    const Vec3 axis = std::abs(dir.z) > .95 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    const Vec3 right = cross(dir, axis).normalized() * radius;
    const Vec3 up = cross(right, dir).normalized() * radius;
    quad(a + right + up, b + right + up, b - right + up, a - right + up, color);
    quad(a - right - up, b - right - up, b + right - up, a + right - up, color);
    quad(a - right + up, b - right + up, b - right - up, a - right - up, color);
    quad(a + right - up, b + right - up, b + right + up, a + right + up, color);
}
void sphere(Vec3 p, double r, Color color) {
    col(color);
    for (int i = 0; i < 4; ++i) {
        const double a = -PI / 2 + i * PI / 4, b = a + PI / 4;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= 8; ++j)
            for (double k : {a, b}) {
                const double angle = j * 2 * PI / 8;
                const Vec3 n{std::cos(k) * std::cos(angle), std::sin(k), std::cos(k) * std::sin(angle)};
                glNormal3d(n.x, n.y, n.z);
                vertex(p + n * r);
            }
        glEnd();
    }
}
// This is the existing decorative valley profile. The central corridor and
// all inrun vertices retain exactly the original hill surface coordinates.
double terrainY(const Hill &h, double x, double z) {
    const double edge = std::max(0.0, std::abs(z) - 19 * h.scale());
    return h.ground(x) - edge * .08 + std::sin(x * .017 + z * .013) * edge * .018;
}
Color snowColor(double x, double z, double scale) {
    const double variation = .014 * std::sin(x * .057 + z * .037) + .009 * std::sin(x * .19 - z * .083);
    const double corridor = clamp((25 * scale - std::abs(z)) / (8 * scale), 0.0, 1.0);
    return {float(.83 + .065 * corridor + variation), float(.89 + .035 * corridor + variation),
            float(.94 + .012 * corridor + variation * .6), 1};
}
void snowQuad(const Hill &h, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    const Vec3 n = cross(b - a, c - a).normalized();
    glBegin(GL_QUADS);
    glNormal3d(n.x, n.y, n.z);
    for (Vec3 p : {a, b, c, d}) {
        col(snowColor(p.x, p.z, h.scale()));
        glTexCoord2d(p.x / 13.0, p.z / 13.0);
        vertex(p);
    }
    glEnd();
}
void texturedQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    const Vec3 n = cross(b - a, c - a).normalized();
    col(color);
    glBegin(GL_QUADS);
    glNormal3d(n.x, n.y, n.z);
    for (Vec3 p : {a, b, c, d}) {
        glTexCoord2d(p.x / 13.0, p.z / 13.0);
        vertex(p);
    }
    glEnd();
}
GLuint makeSnowTexture() {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glPushAttrib(GL_TEXTURE_BIT);
    GLint oldAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, texture);
    // Small brightness variations, groomed ridges, and fine grain. Explicit
    // averaged mip levels prevent the distant snow texture from sparkling.
    int width = 128;
    std::vector<uint8_t> pixels(width * width);
    for (int y = 0; y < width; ++y)
        for (int x = 0; x < width; ++x) {
            const double ridges =
                .006 * std::cos(y * 2 * PI * 8 / 128) + .004 * std::sin((x + y) * 2 * PI / 128);
            const double grain = (hash(x * 13.71 + y * 8.17) - .5) * .018;
            pixels[y * width + x] = uint8_t(clamp(int((.978 + ridges + grain) * 255), 0, 255));
        }
    for (int level = 0;; ++level) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_LUMINANCE, width, width, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     pixels.data());
        if (width == 1)
            break;
        const int nextWidth = width / 2;
        std::vector<uint8_t> next(nextWidth * nextWidth);
        for (int y = 0; y < nextWidth; ++y)
            for (int x = 0; x < nextWidth; ++x)
                next[y * nextWidth + x] = uint8_t(
                    (int(pixels[(y * 2) * width + x * 2]) + pixels[(y * 2) * width + x * 2 + 1] +
                     pixels[(y * 2 + 1) * width + x * 2] + pixels[(y * 2 + 1) * width + x * 2 + 1] + 2) /
                    4);
        pixels.swap(next);
        width = nextWidth;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, oldAlignment);
    glPopAttrib();
    return texture;
}
void contactShadow(const Hill &h, Vec3 p, double rx, double rz, float opacity) {
    constexpr int sides = 16, rings = 4;
    for (int ring = 0; ring < rings; ++ring) {
        const double inner = ring / double(rings), outer = (ring + 1) / double(rings);
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= sides; ++i) {
            const double angle = i * 2 * PI / sides;
            for (double radius : {outer, inner}) {
                const double x = p.x + std::cos(angle) * rx * radius, z = p.z + std::sin(angle) * rz * radius;
                col({.12f, .22f, .31f, float(opacity * (1 - radius) * (1 - radius))});
                vertex({x, terrainY(h, x, z) + .018, z});
            }
        }
        glEnd();
    }
}
void mountainRidge(double scale, int layer) {
    constexpr int columns = 32, rows = 9;
    std::array<std::array<Vec3, rows>, columns> points{};
    const double depth = (650 + layer * 270) * scale, width = (230 + layer * 35) * scale;
    const double floor = (-165 - layer * 8) * scale, nominal = (215 + layer * 67) * scale;
    for (int i = 0; i < columns; ++i) {
        const double seed = layer * 193.0 + i * 17.13;
        const double x = (-1040 + i * 71) * scale;
        const double summitX = x + (hash(seed + 1) - .5) * 32 * scale;
        const double summitZ = -depth + (hash(seed + 2) - .5) * 42 * scale;
        const double height = nominal * (.61 + hash(seed + 3) * .53 + .06 * std::sin(i * .72 + layer));
        const double frontWidth = width * (.87 + hash(seed + 4) * .24);
        const double backWidth = width * (.73 + hash(seed + 5) * .20);
        const double frontLine = .46 + hash(seed + 6) * .18;
        const double backLine = .50 + hash(seed + 7) * .15;
        const std::array<double, rows> fractions{{0, .20, frontLine, .82, 1, .82, backLine, .20, 0}};
        for (int row = 0; row < rows; ++row) {
            const double fraction = fractions[row];
            const double side = row < 4 ? 1 : (row > 4 ? -1 : 0);
            const double spread = side > 0 ? frontWidth : backWidth;
            // Offset intermediate foothill knots to form oblique ridge spurs
            // instead of a row of radial cone-shaped mountains.
            const double spur = (row == 1 || row == 7) ? (hash(seed + row * 11) - .5) * 37 * scale : 0;
            points[i][row] = {x + (summitX - x) * fraction + spur, floor + height * fraction,
                              summitZ + side * spread * (1 - fraction)};
        }
    }
    const Color rock{float(.38 + layer * .055), float(.46 + layer * .05), float(.50 + layer * .05), 1};
    const Color cap{float(.83 + layer * .022), float(.89 + layer * .017), float(.94 + layer * .010), 1};
    for (int i = 0; i < columns - 1; ++i)
        for (int row = 0; row < rows - 1; ++row) {
            const bool snowy = row >= 2 && row <= 5;
            const double seed = i * 31.91 + row * 9.37 + layer * 137;
            const Color color = shade(snowy ? cap : rock, .89 + hash(seed) * .17);
            const Vec3 a = points[i][row], b = points[i][row + 1], c = points[i + 1][row + 1],
                       d = points[i + 1][row];
            // Shared vertices partition snow/rock without overlapping shells.
            // Alternating diagonals and facet tones add alpine rock bands.
            if ((i + row) % 2) {
                triangle(a, d, b, color);
                triangle(b, d, c, shade(color, snowy ? 1.015 : .96));
            } else {
                triangle(a, c, b, color);
                triangle(a, d, c, shade(color, snowy ? .99 : 1.06));
            }
        }
    // The existing valley mesh ends at z=-200*scale. Continue only the distant
    // mountain's lower foothills well below the visible valley, so its base
    // never exposes sky from the existing cameras. Ridge and snowline vertices
    // above remain untouched, and the near skirt stays outside that valley.
    for (int edge : {0, rows - 1}) {
        const double direction = edge == 0 ? 1.0 : -1.0;
        for (int i = 0; i < columns - 1; ++i) {
            const Vec3 a = points[i][edge], b = points[i + 1][edge];
            const auto lower = [&](Vec3 p, double advance, double y) {
                p.y = y * scale;
                p.z += direction * advance * scale;
                if (edge == 0)
                    p.z = std::min(p.z, -205 * scale);
                return p;
            };
            const Vec3 middleA = lower(a, 85, -320), middleB = lower(b, 85, -320),
                       bottomA = lower(a, 170, -720), bottomB = lower(b, 170, -720);
            const Color upper = shade(rock, .96 + hash(i * 9.37 + layer * 131) * .08);
            const Color lowerRock{float(.45 + layer * .05), float(.53 + layer * .05),
                                  float(.57 + layer * .05), 1};
            if (edge == 0) {
                triangle(a, middleA, b, upper);
                triangle(b, middleA, middleB, shade(upper, 1.025));
                triangle(middleA, bottomA, middleB, lowerRock);
                triangle(middleB, bottomA, bottomB, shade(lowerRock, .98));
            } else {
                triangle(a, b, middleA, upper);
                triangle(b, middleB, middleA, shade(upper, 1.025));
                triangle(middleA, middleB, bottomA, lowerRock);
                triangle(middleB, bottomB, bottomA, shade(lowerRock, .98));
            }
        }
    }
}
struct Tree {
    Vec3 base;
    double height = 1, seed = 0;
    int tiers = 4;
    bool animated = false;
};
Tree treeAt(const Hill &h, int i) {
    const double scale = h.scale();
    Tree tree;
    const double x = (-150 + hash(i * 3.1) * 530) * scale;
    const double z = (28 + hash(i * 9.2) * 150) * scale * (i % 2 ? 1 : -1);
    tree.base = {x, terrainY(h, x, z), z};
    tree.height = (3.5 + hash(i * 7.6) * 7.6) * std::sqrt(scale);
    tree.seed = i * 2.7 + 17;
    tree.tiers = 4 + int(hash(i * 11.71) * 3);
    tree.animated = i % 26 == 0;
    return tree;
}
bool visibleTree(const Hill &h, const Tree &tree) {
    const double s = h.scale();
    return !(tree.base.x > 74 * s && tree.base.x < 242 * s && std::abs(tree.base.z) < std::max(47 * s, 18.0));
}
void branch(Vec3 p, double radius, double height, double seed, Vec3 sway = {}) {
    constexpr int count = 7;
    const Vec3 tip = p + Vec3{0, height, 0} + sway;
    std::array<Vec3, count + 1> bases{}, snow{};
    const double rotation = hash(seed) * PI;
    for (int i = 0; i < count; ++i) {
        const double angle = i * 2 * PI / count + rotation;
        const double r = radius * (.90 + hash(seed + i * 11.21) * .18);
        bases[i] = p + Vec3{std::cos(angle) * r, -height * hash(seed + i * 1.41) * .065, std::sin(angle) * r};
        const double fraction = .28 + hash(seed + i * 8.12) * .28;
        snow[i] = bases[i] + (tip - bases[i]) * fraction;
    }
    bases[count] = bases[0];
    snow[count] = snow[0];
    const Color needles{float(.085 + hash(seed + 1) * .032), float(.23 + hash(seed + 2) * .06),
                        float(.235 + hash(seed + 3) * .045), 1};
    const Color cap{.82f, .9f, .94f, 1};
    for (int i = 0; i < count; ++i) {
        quad(bases[i], snow[i], snow[i + 1], bases[i + 1], shade(needles, .94 + hash(seed + i * 4.39) * .12));
        triangle(snow[i], tip, snow[i + 1], shade(cap, .96 + hash(seed + i * 9.13) * .05));
    }
}
void treeBranch(const Tree &tree, int tier, Vec3 sway = {}) {
    const double t = tier / double(tree.tiers - 1);
    const Vec3 p = tree.base + Vec3{0, tree.height * (.12 + .53 * t), 0};
    branch(p, tree.height * (.31 - .238 * t), tree.height * (.43 - .12 * t), tree.seed + tier * 29.7, sway);
}
void slopedSlab(const Hill &h, double x0, double x1, double z0, double z1, double low, double high, Color c) {
    const Vec3 a{x0, terrainY(h, x0, z0), z0}, b{x0, terrainY(h, x0, z1), z1}, d{x1, terrainY(h, x1, z0), z0},
        e{x1, terrainY(h, x1, z1), z1};
    const Vec3 up{0, high, 0}, down{0, low, 0};
    quad(a + up, b + up, e + up, d + up, c);
    quad(a + down, d + down, e + down, b + down, c);
    quad(a + down, a + up, d + up, d + down, shade(c, .88));
    quad(b + down, e + down, e + up, b + up, shade(c, .88));
    quad(a + down, b + down, b + up, a + up, shade(c, .94));
    quad(d + down, d + up, e + up, e + down, shade(c, .94));
}
void inrunSideDeck(const Hill &h, double x0, double x1, double z0, double z1) {
    // Only the side platform changes; the centre track is drawn separately
    // with its unchanged original inrun vertices.
    for (double x = x0; x < x1; x += .5) {
        const double xx = std::min(x + .5, x1);
        const Vec3 a{x, h.inrun(x) + .025, z0}, b{x, h.inrun(x) + .025, z1}, c{xx, h.inrun(xx) + .025, z1},
            d{xx, h.inrun(xx) + .025, z0};
        const Vec3 down{0, -.23, 0};
        quad(a, b, c, d, {.39f, .32f, .27f, 1});
        quad(a + down, d + down, c + down, b + down, {.26f, .30f, .32f, 1});
        quad(a, d, d + down, a + down, {.27f, .34f, .37f, 1});
        quad(b, b + down, c + down, c, {.27f, .34f, .37f, 1});
        beam(a + Vec3{0, .016, .12}, b + Vec3{0, .016, -.12}, .017, {.53f, .44f, .35f, 1});
    }
}
struct VisualStartSeat {
    Vec3 origin, contact;
    double rear, front, top, bottom;
};
VisualStartSeat startSeat(const Hill &h) {
    // Reconstruct the existing Ready pose locally. This reads no live
    // simulation, advances no clock/RNG, and never moves the athlete.
    OriginalPose ready;
    ready.reset(float(std::atan(h.inrunSlope(h.start()))));
    std::array<float, 22> pose{};
    std::copy(ready.angles.begin(), ready.angles.end(), pose.begin());
    std::copy(ready.offsets.begin(), ready.offsets.end(), pose.begin() + 18);
    const auto skeleton = originalSkeleton(pose, ready.leadingLeg);
    const Vec3 hip = skeleton[OriginalJoint::HipCenter], knee = skeleton[OriginalJoint::Knee1];
    const Vec3 thighAxis = (hip - knee).normalized();
    const Vec3 thighFront = cross(thighAxis, {0, 0, 1}).normalized();
    // The suit's proximal thigh ring is at .89 of the knee/hip segment, with
    // its .12->.15 metre radius and full width. Its posterior lower vertex is
    // the visible seated pelvis underside, rather than the bare hip joint.
    constexpr double ring = .89;
    const Vec3 underside = knee + (hip - knee) * ring - thighFront * (.12 + (.15 - .12) * ring);
    const Vec3 origin{float(h.start()), float(h.inrun(h.start())), 0};
    const Vec3 contact = origin + underside;
    const double top = contact.y - .010;
    // The seat ends at the contact point and extends rearward. Extending it
    // forward through the descending thigh would cut through the suit.
    return {origin, contact, contact.x - .22, contact.x, top, top - .075};
}
// Compact geometry lettering on physical arena signs, independent of HUD and
// font textures. It also gives the original K point an unobtrusive label.
std::array<uint8_t, 7> glyph(char c) {
    switch (c) {
    case 'A':
        return {14, 17, 17, 31, 17, 17, 17};
    case 'C':
        return {14, 17, 16, 16, 16, 17, 14};
    case 'D':
        return {30, 17, 17, 17, 17, 17, 30};
    case 'E':
        return {31, 16, 16, 30, 16, 16, 31};
    case 'I':
        return {31, 4, 4, 4, 4, 4, 31};
    case 'J':
        return {7, 2, 2, 2, 18, 18, 12};
    case 'K':
        return {17, 18, 20, 24, 20, 18, 17};
    case 'M':
        return {17, 27, 21, 21, 17, 17, 17};
    case 'N':
        return {17, 25, 25, 21, 19, 19, 17};
    case 'O':
        return {14, 17, 17, 17, 17, 17, 14};
    case 'P':
        return {30, 17, 17, 30, 16, 16, 16};
    case 'R':
        return {30, 17, 17, 30, 20, 18, 17};
    case 'S':
        return {15, 16, 16, 14, 1, 1, 30};
    case 'T':
        return {31, 4, 4, 4, 4, 4, 4};
    case 'U':
        return {17, 17, 17, 17, 17, 17, 14};
    case '0':
        return {14, 17, 19, 21, 25, 17, 14};
    case '1':
        return {4, 12, 4, 4, 4, 4, 14};
    case '2':
        return {14, 17, 1, 2, 4, 8, 31};
    case '3':
        return {30, 1, 1, 14, 1, 1, 30};
    case '4':
        return {2, 6, 10, 18, 31, 2, 2};
    case '5':
        return {31, 16, 16, 30, 1, 1, 30};
    case '6':
        return {14, 16, 16, 30, 17, 17, 14};
    case '7':
        return {31, 1, 2, 4, 8, 8, 8};
    case '8':
        return {14, 17, 17, 14, 17, 17, 14};
    case '9':
        return {14, 17, 17, 15, 1, 1, 14};
    default:
        return {};
    }
}
void signText(const std::string &text, Vec3 center, double width, double height, int side, Color c) {
    const double cell = std::min(height / 7, width / std::max(1.0, double(text.size() * 6 - 1)));
    const double left = center.x - cell * (text.size() * 6 - 1) * .5;
    for (size_t letter = 0; letter < text.size(); ++letter) {
        const auto mask = glyph(text[letter]);
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (mask[y] & (1 << (4 - x))) {
                    const double xx = left + (letter * 6 + x) * cell, yy = center.y + (3.5 - y) * cell;
                    // Read the K label from its intended face without moving
                    // the sign, its centre or the original K marker.
                    const double x0 = side > 0 ? 2 * center.x - xx - cell * .88 : xx;
                    const double x1 = side > 0 ? 2 * center.x - xx : xx + cell * .88;
                    const Vec3 a{x0, yy, center.z}, b{x1, yy, center.z}, d{x0, yy - cell * .88, center.z},
                        e{x1, yy - cell * .88, center.z};
                    if (side < 0)
                        quad(a, b, e, d, c);
                    else
                        quad(d, e, b, a, c);
                }
    }
}
struct BoardSpan {
    double left, right, z;
    int segments;
};
double boardBase(const Hill &h, const BoardSpan &span, double x) {
    const double segment = (span.right - span.left) / span.segments;
    const int index = clamp(int(std::floor((x - span.left) / segment)), 0, span.segments - 1);
    const double a = span.left + index * segment, b = a + segment;
    const double t = clamp((x - a) / segment, 0.0, 1.0);
    return terrainY(h, a, span.z) * (1 - t) + terrainY(h, b, span.z) * t;
}
void boardFace(const Hill &h, const BoardSpan &span, double left, double right, double bottom, double top,
               double z, int side, Color color) {
    const double segment = (span.right - span.left) / span.segments;
    for (double x = left; x < right - 1e-8;) {
        const int index = clamp(int(std::floor((x - span.left) / segment + 1e-7)), 0, span.segments - 1);
        const double xx = std::min(right, span.left + (index + 1) * segment);
        const double y = boardBase(h, span, x), yy = boardBase(h, span, xx);
        const Vec3 a{x, y + bottom, z}, b{xx, yy + bottom, z}, c{xx, yy + top, z}, d{x, y + top, z};
        if (side > 0)
            quad(d, c, b, a, color);
        else
            quad(a, b, c, d, color);
        x = xx;
    }
}
void boardText(const Hill &h, const BoardSpan &span, const std::string &text, double x, double offset,
               double width, double height, double z, int side, Color color) {
    const double cell = std::min(height / 7, width / std::max(1.0, double(text.size() * 6 - 1)));
    const double left = -cell * (text.size() * 6 - 1) * .5;
    for (size_t letter = 0; letter < text.size(); ++letter) {
        const auto mask = glyph(text[letter]);
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (mask[row] & (1 << (4 - column))) {
                    const double logicalLeft = left + (letter * 6 + column) * cell;
                    const double logicalRight = logicalLeft + cell * .88;
                    // A reader facing +Z has screen-right along -X. Reverse
                    // pixel positions for that face, rather than mirroring
                    // the letters seen from inside the positive-side arena.
                    const double xx = x + (side > 0 ? -logicalRight : logicalLeft);
                    const double right = x + (side > 0 ? -logicalLeft : logicalRight);
                    const double yy = offset + (3.5 - row) * cell;
                    // Split a glyph cell at the same knots as the board face.
                    // Its top and bottom therefore share that exact curve.
                    boardFace(h, span, xx, right, yy - cell * .88, yy, z, side, color);
                }
    }
}
void advertisement(const Hill &h, double x, double width, double front, int side, Color accent,
                   const std::string &text) {
    const BoardSpan span{x - width * .5, x + width * .5, front, std::max(2, int(std::ceil(width / .55)))};
    const double zFront = front - side * .15, zBack = front + side * .15;
    const double segment = width / span.segments;
    for (int i = 0; i < span.segments; ++i) {
        const double a = span.left + i * segment, b = a + segment;
        const double y = boardBase(h, span, a), yy = boardBase(h, span, b);
        const Vec3 ta{a, y + 1.79, zFront}, tb{a, y + 1.79, zBack}, tc{b, yy + 1.79, zBack},
            td{b, yy + 1.79, zFront};
        const Vec3 ba{a, y + .16, zFront}, bb{b, yy + .16, zFront}, bc{b, yy + .16, zBack},
            bd{a, y + .16, zBack};
        if (side > 0) {
            quad(ta, tb, tc, td, {.73f, .82f, .84f, 1});
            quad(ba, bb, bc, bd, {.31f, .43f, .48f, 1});
        } else {
            quad(td, tc, tb, ta, {.73f, .82f, .84f, 1});
            quad(bd, bc, bb, ba, {.31f, .43f, .48f, 1});
        }
    }
    boardFace(h, span, span.left, span.right, .16, 1.79, zFront, side, {.10f, .28f, .33f, 1});
    boardFace(h, span, span.left, span.right, .16, 1.79, zBack, -side, {.20f, .32f, .37f, 1});
    for (double edge : {span.left, span.right}) {
        const double y = boardBase(h, span, edge);
        quad({edge, y + .16, zFront}, {edge, y + 1.79, zFront}, {edge, y + 1.79, zBack},
             {edge, y + .16, zBack}, {.42f, .54f, .58f, 1});
    }
    for (int face : {side, -side}) {
        const double z = face == side ? zFront : zBack;
        boardFace(h, span, span.left, span.right, 1.745, 1.82, z - face * .02, face, {.73f, .82f, .84f, 1});
        const double accentLeft = face > 0 ? span.right - width * .1025 : span.left + width * .0175;
        const double accentRight = face > 0 ? span.right - width * .0175 : span.left + width * .1025;
        boardFace(h, span, accentLeft, accentRight, .29, 1.61, z - face * .025, face, accent);
        boardText(h, span, text, x - face * width * .025, .97, width * .78, .74, z - face * .035, face,
                  {.91f, .96f, .95f, 1});
    }
    for (double offset : {-.39, .39}) {
        const double xx = x + offset * width, z = zBack + side * .12;
        beam({xx, terrainY(h, xx, z) - .035, z}, {xx, boardBase(h, span, xx) + 1.78, z}, .04,
             {.43f, .54f, .58f, 1});
        beam({xx, boardBase(h, span, xx) + .80, z}, {xx, boardBase(h, span, xx) + .80, zBack}, .035,
             {.43f, .54f, .58f, 1});
    }
}
constexpr std::array<Color, 7> crowdColors{{{.14f, .34f, .43f, 1},
                                            {.80f, .31f, .23f, 1},
                                            {.89f, .66f, .28f, 1},
                                            {.29f, .48f, .45f, 1},
                                            {.28f, .31f, .47f, 1},
                                            {.62f, .31f, .39f, 1},
                                            {.73f, .79f, .80f, 1}}};
struct Spectator {
    Vec3 feet;
    double size = 1, seed = 0, yaw = 0;
    Color coat{};
    int side = 1;
    bool present = true, animated = false, flag = false;
};
Spectator spectatorAt(const Hill &h, int side, int section, int row, int person) {
    const double s = h.scale(), width = 5.8 * s, center = (79 + section * 6.2) * s;
    const double seed = section * 91.0 + row * 13.0 + person * 7.0 + (side < 0 ? 431.0 : 0);
    Spectator p;
    p.size = .74 + hash(seed + 1) * .31;
    p.seed = seed;
    p.coat = crowdColors[int(hash(seed + 9) * crowdColors.size())];
    p.side = side;
    p.yaw = (hash(seed + 23.9) - .5) * .73;
    const int ordinal = section * 12 + row * 4 + person + (side < 0 ? 168 : 0);
    p.animated = ordinal % 17 == 0;
    p.flag = p.animated && ordinal % 51 == 0;
    const int capacity = clamp(int(width / 1.10), 1, 4);
    const double density = .43 + hash(section * 31.17 + row * 12.29 + side * 47.0) * .46;
    p.present = person < capacity && (hash(seed * 1.79 + 5) < density || p.flag);
    if (width < 1.10)
        p.present = p.present && (section + row + (side < 0 ? 1 : 0)) % 2 == 0;
    const double pitch = std::min(1.25, width / (capacity + .30));
    const double room = std::max(0.0, width * .90 - (capacity - 1) * pitch - .58);
    const double cluster = (hash(section * 19.1 + row * 3.93 + side * 7.14) - .5) * room * .83;
    const double jitter = (hash(seed + 31.7) - .5) * pitch * .08;
    const double x = center + (person - (capacity - 1) * .5) * pitch + cluster + jitter;
    const double near = std::max(25 * s, 8.0);
    const double z = side * (near + 1.95 + row * 1.2 + (hash(seed + 41.9) - .5) * .12);
    // Use the actual two triangles of the wooden row floor, including its
    // slope, so irregular placements remain planted on the existing stands.
    const double x0 = center - width * .48, x1 = center + width * .48;
    const double za = side * (near + 1.35 + row * 1.2), zb = za + side * 1.2;
    const double z0 = std::min(za, zb) + .3, z1 = std::max(za, zb) - .3;
    const double tx = clamp((x - x0) / (x1 - x0), 0.0, 1.0), tz = clamp((z - z0) / (z1 - z0), 0.0, 1.0);
    const double a = terrainY(h, x0, z0), b = terrainY(h, x0, z1), c = terrainY(h, x1, z1),
                 d = terrainY(h, x1, z0);
    const double floor = tz >= tx ? a + (b - a) * tz + (c - b) * tx : a + (d - a) * tx + (c - d) * tz;
    p.feet = {x, floor + .78 + row * .70, z};
    return p;
}
Vec3 spectatorPoint(const Spectator &p, Vec3 local) {
    const double c = std::cos(p.yaw), s = std::sin(p.yaw);
    return p.feet + Vec3{local.x * c + local.z * s, local.y, -local.x * s + local.z * c};
}
void spectatorBox(const Spectator &p, Vec3 center, Vec3 size, Color color) {
    const Vec3 a = center - size * .5, b = center + size * .5;
    const auto point = [&](double x, double y, double z) { return spectatorPoint(p, {x, y, z}); };
    quad(point(a.x, b.y, a.z), point(a.x, b.y, b.z), point(b.x, b.y, b.z), point(b.x, b.y, a.z), color);
    quad(point(a.x, a.y, a.z), point(b.x, a.y, a.z), point(b.x, a.y, b.z), point(a.x, a.y, b.z), color);
    quad(point(a.x, a.y, b.z), point(b.x, a.y, b.z), point(b.x, b.y, b.z), point(a.x, b.y, b.z), color);
    quad(point(b.x, a.y, a.z), point(a.x, a.y, a.z), point(a.x, b.y, a.z), point(b.x, b.y, a.z), color);
    quad(point(a.x, a.y, a.z), point(a.x, a.y, b.z), point(a.x, b.y, b.z), point(a.x, b.y, a.z), color);
    quad(point(b.x, a.y, b.z), point(b.x, a.y, a.z), point(b.x, b.y, a.z), point(b.x, b.y, b.z), color);
}
void spectatorBody(const Spectator &p) {
    if (!p.present)
        return;
    const double s = p.size;
    for (double x : {-.095, .095}) {
        beam(spectatorPoint(p, {x * s, .10 * s, 0}), spectatorPoint(p, {x * s, .48 * s, 0}), .075 * s,
             {.105f, .16f, .22f, 1});
        spectatorBox(p, {x * s, .06 * s, -p.side * .045 * s}, {.16 * s, .12 * s, .27 * s},
                     {.075f, .105f, .14f, 1});
    }
    spectatorBox(p, {0, .67 * s, 0}, {.42 * s, .48 * s, .29 * s}, p.coat);
    spectatorBox(p, {0, .90 * s, -p.side * .055 * s}, {.34 * s, .09 * s, .29 * s}, shade(p.coat, .68));
    sphere(spectatorPoint(p, {0, 1.065 * s, 0}), .155 * s, {.77f, .65f, .55f, 1});
    sphere(spectatorPoint(p, {0, 1.155 * s, .025 * p.side * s}), .155 * s, shade(p.coat, .73));
    if (!p.animated)
        for (int hand : {-1, 1}) {
            const Vec3 shoulder = spectatorPoint(p, {hand * .22 * s, .82 * s, 0});
            const Vec3 wrist = spectatorPoint(p, {hand * .27 * s, .53 * s, -p.side * .075 * s});
            beam(shoulder, wrist, .07 * s, shade(p.coat, .94));
            sphere(wrist, .075 * s, {.10f, .15f, .21f, 1});
        }
}
void flag(Vec3 poleBase, double size, double time, double seed) {
    const Vec3 tip = poleBase + Vec3{.16 * std::sin(time * 1.9 + seed), 1.15 * size, 0};
    beam(poleBase, tip, .019 * size, {.64f, .71f, .74f, 1});
    const Color base = hash(seed + 2) > .5 ? Color{.85f, .18f, .20f, 1} : Color{.90f, .94f, .95f, 1};
    const Color crossColor = hash(seed + 2) > .5 ? Color{.92f, .96f, .97f, 1} : Color{.12f, .33f, .62f, 1};
    const Vec3 left = tip + Vec3{0, -.05 * size, 0};
    const auto point = [&](double u, double v) {
        return left + Vec3{u * .75 * size, -(.04 * u + .44 * v) * size,
                           std::sin(time * 3.4 + seed + u * 4) * .10 * u * size};
    };
    const std::array<double, 4> columns{{0, .30, .40, 1}}, rows{{0, .18 / .44, .245 / .44, 1}};
    // A single connected flag surface: the Nordic cross is part of the mesh,
    // not a second shell competing for depth when viewed from the jump.
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y)
            quad(point(columns[x], rows[y]), point(columns[x + 1], rows[y]),
                 point(columns[x + 1], rows[y + 1]), point(columns[x], rows[y + 1]),
                 x == 1 || y == 1 ? crossColor : base);
}
void floodlight(const Hill &h, double x, double z, double size) {
    const double y = terrainY(h, x, z), height = 18 * size + 2;
    box({x, y + .30, z}, {1.1, .60, 1.1}, {.48f, .54f, .56f, 1});
    beam({x, y + .3, z}, {x, y + height, z}, .13, {.40f, .48f, .53f, 1});
    beam({x, y + height - 1, z}, {x + 1.4, y + height, z}, .08, {.36f, .43f, .48f, 1});
    box({x, y + height, z}, {3.4, 1.25, .60}, {.12f, .20f, .25f, 1});
    for (int lamp = 0; lamp < 6; ++lamp) {
        const double xx = x + (lamp % 3 - 1) * 1.02, yy = y + height + (lamp / 3 - .5) * .49;
        box({xx, yy, z}, {.82, .38, .67}, {.51f, .59f, .63f, 1});
        const GLfloat emission[]{.28f, .27f, .22f, 1};
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);
        const double front = z + (z > 0 ? -.345 : .345);
        quad({xx - .35, yy - .125, front}, {xx + .35, yy - .125, front}, {xx + .35, yy + .125, front},
             {xx - .35, yy + .125, front}, {.94f, .91f, .78f, 1});
        const GLfloat noEmission[]{0, 0, 0, 1};
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, noEmission);
    }
}
} // namespace

void Renderer::terrain(const Hill &h) {
    const double s = h.scale();
    size_t index = 0;
    for (size_t i = 0; i < hills().size(); ++i)
        if (h.id == hills()[i].id)
            index = i;
    if (landscapes[index]) {
        glCallList(landscapes[index]);
        return;
    }
    if (!snowTexture)
        snowTexture = makeSnowTexture();
    landscapes[index] = glGenLists(1);
    glNewList(landscapes[index], GL_COMPILE_AND_EXECUTE);
    visual::StaticBatch staticBatch;
    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_POLYGON_BIT);
    const GLfloat matte[]{.025f, .035f, .045f, 1};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matte);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 8);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, snowTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    // Same bounds, cell dimensions, and vertex heights as the existing hill.
    // Grooming, grain, and broad snow color variation are material-only.
    for (double x = std::min(-170 * s, h.inrunStart() - 60 * s); x < std::max(420 * s, h.end() + 80 * s);
         x += 3 * s)
        for (double z = -200 * s; z < 200 * s; z += 10 * s)
            snowQuad(h, {x, terrainY(h, x, z), z}, {x, terrainY(h, x, z + 10 * s), z + 10 * s},
                     {x + 3 * s, terrainY(h, x + 3 * s, z + 10 * s), z + 10 * s},
                     {x + 3 * s, terrainY(h, x + 3 * s, z), z});
    glDisable(GL_TEXTURE_2D);
    // Three broad alpine ridgelines sit well behind the forest. Their lower
    // silhouette opens the sky and lets atmospheric fog separate the layers.
    for (int layer = 2; layer >= 0; --layer)
        mountainRidge(s, layer);
    // All soft ground contacts share one state pass, instead of switching
    // depth/blend/lighting hundreds of times between individual trees.
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_POLYGON_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -1);
    for (int i = 0; i < 420; ++i) {
        const Tree tree = treeAt(h, i);
        if (visibleTree(h, tree))
            contactShadow(h, tree.base, tree.height * .61, tree.height * .48, .20f);
    }
    for (int side : {-1, 1}) {
        const double near = std::max(25 * s, 8.0), front = side * (near - 1.0);
        for (int section = 0; section < 14; ++section) {
            const double x = (79 + section * 6.2) * s;
            contactShadow(h, {x, 0, front + side * 3}, 5.8 * s * .76, 4.3, .18f);
        }
        for (int mast = 0; mast < 5; ++mast)
            contactShadow(h, {(60 + mast * 29) * s, 0, side * (near + 10.5)}, 1.5, 1.5, .19f);
    }
    glPopAttrib();
    for (int i = 0; i < 420; ++i) {
        const Tree tree = treeAt(h, i);
        if (!visibleTree(h, tree))
            continue;
        box(tree.base + Vec3{0, tree.height * .22, 0},
            {tree.height * .085, tree.height * .44, tree.height * .085}, {.28f, .25f, .22f, 1});
        for (int tier = 0; tier < tree.tiers; ++tier)
            if (!(tree.animated && tier == tree.tiers - 1))
                treeBranch(tree, tier);
    }
    // Existing snowy track and its exact h.inrun profile; details sit beside
    // or underneath it, and never alter the track's contour or width.
    for (double x = h.inrunStart(); x < 0; x += 2 * s) {
        const double xx = std::min(0.0, x + 2 * s), y = h.inrun(x), yy = h.inrun(xx);
        glEnable(GL_TEXTURE_2D);
        texturedQuad({x, y, -2.3}, {x, y, 2.3}, {xx, yy, 2.3}, {xx, yy, -2.3}, {.92f, .96f, .97f, 1});
        glDisable(GL_TEXTURE_2D);
        quad({x, y - .62, 2.3}, {xx, yy - .62, 2.3}, {xx, yy, 2.3}, {x, y, 2.3}, {.33f, .29f, .25f, 1});
        quad({x, y - .62, -2.3}, {x, y, -2.3}, {xx, yy, -2.3}, {xx, yy - .62, -2.3}, {.33f, .29f, .25f, 1});
        for (double z : {-.22, .22})
            quad({x, y + .014, z - .055}, {x, y + .014, z + .055}, {xx, yy + .014, z + .055},
                 {xx, yy + .014, z - .055}, {.48f, .67f, .76f, 1});
        for (double z : {-2.2, 2.2}) {
            beam({x, y + .7, z}, {xx, yy + .7, z}, .055, {.68f, .75f, .78f, 1});
            beam({x, y + .39, z}, {xx, yy + .39, z}, .028, {.39f, .49f, .54f, 1});
            const double outer = z < 0 ? -3.35 : 3.35;
            quad({x, y - .1, outer - .42}, {x, y - .1, outer + .42}, {xx, yy - .1, outer + .42},
                 {xx, yy - .1, outer - .42}, {.43f, .35f, .28f, 1});
            quad({x, y + .021, z - .09}, {x, y + .021, z + .09}, {xx, yy + .021, z + .09},
                 {xx, yy + .021, z - .09}, {.19f, .42f, .53f, 1});
        }
        beam({x, y - .58, -2.25}, {x, y - .58, 2.25}, .055, {.39f, .33f, .28f, 1});
    }
    for (double x = h.inrunStart(); x < -1; x += 12 * s) {
        const double y = h.inrun(x), base = h.ground(x) - 3;
        for (double z : {-2., 2.}) {
            beam({x, base, z}, {x, y - .6, z}, .21, {.28f, .35f, .39f, 1});
            box({x, base + .1, z}, {.90, .2, .90}, {.48f, .52f, .53f, 1});
            if (x + 12 * s < 0)
                beam({x, base, z}, {x + 12 * s, h.inrun(x + 12 * s) - .6, z}, .095, {.41f, .48f, .52f, 1});
            beam({x, y, z}, {x, y + .75, z}, .04, {.58f, .66f, .69f, 1});
            beam({x, y - .32, z}, {x, y - .32, -z}, .075, {.39f, .44f, .47f, 1});
        }
    }
    // The old horizontal 4x7 platform intersected the steep track downhill.
    // Narrow side decks follow the existing slope, leaving the track clear.
    for (int side : {-1, 1}) {
        const double a = side * 2.70, b = side * 3.95;
        inrunSideDeck(h, h.start() - 3.8, h.start() + .65, std::min(a, b), std::max(a, b));
        for (double x : {h.start() - 3.4, h.start() + .25}) {
            const double z = side * 3.35, y = h.inrun(x);
            beam({x, y - 1.10, z}, {x, y - .19, z}, .075, {.32f, .40f, .44f, 1});
            beam({x, y - .95, side * 2.12}, {x, y - .22, z}, .05, {.43f, .49f, .51f, 1});
        }
    }
    // Position the slim seat immediately under the unchanged Ready pelvis.
    // Side posts stay clear of both ski grooves and the existing safety rails.
    const auto seat = startSeat(h);
    const double seatX = (seat.rear + seat.front) * .5;
    box({seatX, (seat.top + seat.bottom) * .5, 0},
        {seat.front - seat.rear, seat.top - seat.bottom, 4.10}, {.51f, .38f, .27f, 1});
    beam({seatX, seat.bottom - .018, -2.0}, {seatX, seat.bottom - .018, 2.0},
         .014, {.40f, .49f, .53f, 1});
    for (double z : {-2.0, 2.0})
        beam({seatX, h.inrun(seatX) - .12, z}, {seatX, seat.bottom - .025, z},
             .035, {.55f, .63f, .67f, 1});
    const double cabinX = h.start() - 2.6, cabinZ = -4.45;
    const double cabinFloor = std::max(h.inrun(cabinX - 1.2), h.inrun(cabinX + 1.2)) + .16;
    for (double x : {cabinX - 1.08, cabinX + 1.08})
        for (double z : {cabinZ - .60, cabinZ + .60})
            beam({x, h.inrun(x) - .18, z}, {x, cabinFloor, z}, .075, {.34f, .43f, .47f, 1});
    box({cabinX, cabinFloor - .065, cabinZ}, {2.65, .13, 1.62}, {.38f, .44f, .47f, 1});
    box({cabinX, cabinFloor + 1.35, cabinZ}, {2.4, 2.7, 1.5}, {.24f, .31f, .35f, 1});
    box({cabinX, cabinFloor + 2.80, cabinZ}, {2.80, .20, 1.95}, {.88f, .93f, .95f, 1});
    box({cabinX, cabinFloor + 1.65, cabinZ + .78}, {1.95, 1.03, .05}, {.24f, .51f, .63f, 1});
    for (double x : {cabinX - .65, cabinX + .65})
        beam({x, cabinFloor + 1.13, cabinZ + .815}, {x, cabinFloor + 2.16, cabinZ + .815}, .021,
             {.65f, .74f, .78f, 1});
    signText("PRO", {cabinX, cabinFloor + .55, cabinZ + .762}, 1.55, .43, -1, {.88f, .94f, .94f, 1});
    // Source-defined distance and K markers retain their original positions.
    for (int d = 10; d <= h.k + 50; d += 10) {
        const double x = h.atDistance(d), y = h.ground(x) + .045;
        const Color c = d == h.k ? Color{.91f, .30f, .28f, 1} : Color{.46f, .67f, .74f, .6f};
        quad({x, y, -18 * s}, {x + .18, y + h.slope(x) * .18, -18 * s},
             {x + .18, y + h.slope(x) * .18, 18 * s}, {x, y, 18 * s}, c);
    }
    {
        const double x = h.kMarkerX;
        const Color c{.94f, .28f, .28f, 1};
        quad({x, h.ground(x) + .06, -20 * s}, {x + .4, h.ground(x + .4) + .06, -20 * s},
             {x + .4, h.ground(x + .4) + .06, 20 * s}, {x, h.ground(x) + .06, 20 * s}, c);
        for (int side : {-1, 1}) {
            const double z = side * 20 * s, y = terrainY(h, x, z);
            beam({x, h.ground(x), z}, {x, h.ground(x) + 3, z}, .08, c);
            box({x, y + 1.1, z}, {2.45, 1.12, .22}, {.91f, .24f, .24f, 1});
            signText("K" + std::to_string(h.k), {x, y + 1.12, z - side * .116}, 2.15, .65, side,
                     {.98f, .97f, .92f, 1});
            box({x, y + 3.0, z}, {.24, .20, .24}, {.94f, .95f, .90f, 1});
        }
    }
    // Terraced concrete, warm timber seating, railings, sponsored panels and
    // winter spectators give the existing arena scale and a sporting identity.
    const double arenaNear = std::max(25 * s, 8.0);
    for (int side : {-1, 1}) {
        const double front = side * (arenaNear - 1.0);
        for (int section = 0; section < 14; ++section) {
            const double x = (79 + section * 6.2) * s, width = 5.8 * s;
            const Color accent = section % 3 == 0 ? Color{.86f, .37f, .25f, 1} : Color{.25f, .68f, .65f, 1};
            advertisement(h, x, width, front, side, accent, section % 3 == 0 ? "NORDIC" : "SKI JUMP PRO");
            for (int row = 0; row < 3; ++row) {
                const double z0 = side * (arenaNear + 1.35 + row * 1.2), z1 = z0 + side * 1.2;
                const double floor = .65 + row * .70;
                slopedSlab(h, x - width * .5, x + width * .5, std::min(z0, z1), std::max(z0, z1), -.16, floor,
                           {.43f, .49f, .52f, 1});
                slopedSlab(h, x - width * .48, x + width * .48, std::min(z0, z1) + .3, std::max(z0, z1) - .3,
                           floor + .05, floor + .13, {.40f, .32f, .26f, 1});
                for (int person = 0; person < 4; ++person)
                    spectatorBody(spectatorAt(h, side, section, row, person));
            }
            const double back = side * (arenaNear + 5.0);
            beam({x - width * .5, terrainY(h, x - width * .5, back) + 3.4, back},
                 {x + width * .5, terrainY(h, x + width * .5, back) + 3.4, back}, .046,
                 {.55f, .64f, .68f, 1});
            for (double xx : {x - width * .5, x + width * .5})
                beam({xx, terrainY(h, xx, back) + 2.0, back}, {xx, terrainY(h, xx, back) + 3.45, back}, .045,
                     {.44f, .54f, .58f, 1});
        }
        for (int mast = 0; mast < 5; ++mast) {
            const double x = (60 + mast * 29) * s, z = side * (arenaNear + 10.5);
            floodlight(h, x, z, std::sqrt(s));
        }
    }
    glPopAttrib();
    staticBatch.finish();
    glEndList();
}

void Renderer::scenery(const Hill &h, const Frame &frame, double time) {
    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    glDisable(GL_TEXTURE_2D);
    const double strength = clamp(std::abs(double(frame.windSpeed)) / 8.0, 0.05, 1.0);
    const Vec3 wind{std::cos(frame.windAngle), 0, std::sin(frame.windAngle)};
    // Only one small upper bough on every twenty-sixth tree is animated.
    for (int i = 0; i < 420; i += 26) {
        const Tree tree = treeAt(h, i);
        if (!visibleTree(h, tree))
            continue;
        const double bend =
            (std::sin(time * 1.35 + tree.seed) + .35 * std::sin(time * 2.9 + tree.seed * .7)) * tree.height *
            .016 * strength;
        treeBranch(tree, tree.tiers - 1, wind * bend);
    }
    glDisable(GL_CULL_FACE);
    for (int side : {-1, 1})
        for (int section = 0; section < 14; ++section)
            for (int row = 0; row < 3; ++row)
                for (int person = 0; person < 4; ++person) {
                    const Spectator p = spectatorAt(h, side, section, row, person);
                    if (!p.present || !p.animated)
                        continue;
                    const double s = p.size;
                    for (int hand : {-1, 1}) {
                        const double wave =
                            .5 + .5 * std::sin(time * (1.7 + hash(p.seed) * .65) + p.seed + hand * .6);
                        const Vec3 shoulder = spectatorPoint(p, {hand * .22 * s, .82 * s, 0});
                        const Vec3 elbow =
                            spectatorPoint(p, {hand * .35 * s, (.81 + wave * .22) * s, -side * .045 * s});
                        const Vec3 wrist =
                            spectatorPoint(p, {hand * (.30 + .09 * std::sin(time * 2 + p.seed)) * s,
                                               (1.04 + wave * .25) * s, -side * .06 * s});
                        beam(shoulder, elbow, .065 * s, shade(p.coat, .93));
                        beam(elbow, wrist, .057 * s, p.coat);
                        sphere(wrist, .07 * s, {.12f, .17f, .21f, 1});
                        if (p.flag && hand == 1)
                            flag(wrist, s, time, p.seed);
                    }
                }
    glPopAttrib();
}
} // namespace sjp
