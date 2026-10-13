#include "renderer.h"
#include "original_skeleton.h"
#include "visual_weather.h"
#include "visual_floodlights.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "render_batch.h"
// Only this translation unit batches static terrain compilation. The wrapper
// forwards to the Vulkan command recorder for animated scenery.

namespace sjp {
namespace {
void col(Color c) {
    sjp::visual::color(c.r, c.g, c.b, c.a);
}
void vertex(Vec3 p) {
    sjp::visual::vertex(p.x, p.y, p.z);
}
Color shade(Color c, double value) {
    return {float(c.r * value), float(c.g * value), float(c.b * value), c.a};
}
double hash(double n) {
    return std::fmod(std::abs(std::sin(n * 12.9898) * 43758.5453), 1.0);
}
double smoothNoise(double x, double seed) {
    const double cell = std::floor(x), t = x - cell;
    const double blend = t * t * (3 - 2 * t);
    return hash(cell + seed) * (1 - blend) + hash(cell + seed + 1) * blend;
}
double smoothRange(double low, double high, double x) {
    const double t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    col(color);
    const Vec3 n = cross(b - a, c - a).normalized();
    sjp::visual::begin(sjp::gfx::QUADS);
    sjp::visual::normal(n.x, n.y, n.z);
    vertex(a);
    vertex(b);
    vertex(c);
    vertex(d);
    sjp::visual::end();
}
void triangle(Vec3 a, Vec3 b, Vec3 c, Color color) {
    col(color);
    const Vec3 n = cross(b - a, c - a).normalized();
    sjp::visual::begin(sjp::gfx::TRIANGLES);
    sjp::visual::normal(n.x, n.y, n.z);
    vertex(a);
    vertex(b);
    vertex(c);
    sjp::visual::end();
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
        sjp::visual::begin(sjp::gfx::QUAD_STRIP);
        for (int j = 0; j <= 8; ++j)
            for (double k : {a, b}) {
                const double angle = j * 2 * PI / 8;
                const Vec3 n{std::cos(k) * std::cos(angle), std::sin(k), std::cos(k) * std::sin(angle)};
                sjp::visual::normal(n.x, n.y, n.z);
                vertex(p + n * r);
            }
        sjp::visual::end();
    }
}
// This is the existing decorative valley profile. The central corridor and
// all inrun vertices retain exactly the original hill surface coordinates.
double terrainY(const Hill &h, double x, double z) {
    return visual::arenaGround(h, x, z);
}
double preparedSnowWeight(const Hill &h, double x, double z) {
    const double side = clamp((25 * h.scale() - std::abs(z)) / (8 * h.scale()), 0.0, 1.0);
    // Only the material ends here. The original uphill surface continues intact.
    const double end = h.outrunBottom() + 45 + 2 * std::sin(z * .13);
    const double t = clamp((end - x) / 33, 0.0, 1.0);
    return side * t * t * (3 - 2 * t);
}
Color snowColor(const Hill &h, double x, double z) {
    const double variation = .014 * std::sin(x * .057 + z * .037) + .009 * std::sin(x * .19 - z * .083);
    const double corridor = preparedSnowWeight(h, x, z);
    return {float(.83 + .065 * corridor + variation), float(.89 + .035 * corridor + variation),
            float(.94 + .012 * corridor + variation * .6), 1};
}
void snowQuad(const Hill &h, Vec3 a, Vec3 b, Vec3 c, Vec3 d, bool groomed = false) {
    if (groomed && preparedSnowWeight(h, a.x, a.z) == 0 && preparedSnowWeight(h, b.x, b.z) == 0 &&
        preparedSnowWeight(h, c.x, c.z) == 0 && preparedSnowWeight(h, d.x, d.z) == 0)
        return;
    const Vec3 n = cross(b - a, c - a).normalized();
    sjp::visual::begin(sjp::gfx::QUADS);
    sjp::visual::normal(n.x, n.y, n.z);
    for (Vec3 p : {a, b, c, d}) {
        Color color = snowColor(h, p.x, p.z);
        if (groomed)
            color.a = float(preparedSnowWeight(h, p.x, p.z));
        col(color);
        sjp::visual::texCoord(p.x / 13.0, p.z / 13.0);
        vertex(p);
    }
    sjp::visual::end();
}
void texturedQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    const Vec3 n = cross(b - a, c - a).normalized();
    col(color);
    sjp::visual::begin(sjp::gfx::QUADS);
    sjp::visual::normal(n.x, n.y, n.z);
    for (Vec3 p : {a, b, c, d}) {
        sjp::visual::texCoord(p.x / 13.0, p.z / 13.0);
        vertex(p);
    }
    sjp::visual::end();
}
sjp::gfx::Id makeSnowTexture(bool groomed) {
    sjp::gfx::Id texture = 0;
    sjp::gfx::genTextures(1, &texture);
    sjp::visual::pushAttrib(sjp::gfx::TEXTURE_BIT);
    int oldAlignment = 4;
    sjp::gfx::getIntegerv(sjp::gfx::UNPACK_ALIGNMENT, &oldAlignment);
    sjp::gfx::pixelStorei(sjp::gfx::UNPACK_ALIGNMENT, 1);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, texture);
    // Ordinary snow has grain; only the prepared track has machine-made ridges.
    // Averaged mip levels prevent either material from sparkling at a distance.
    int width = 128;
    std::vector<uint8_t> pixels(width * width);
    for (int y = 0; y < width; ++y)
        for (int x = 0; x < width; ++x) {
            const double ridges =
                groomed ? .006 * std::cos(y * 2 * PI * 8 / 128) + .004 * std::sin((x + y) * 2 * PI / 128) : 0;
            const double grain = (hash(x * 13.71 + y * 8.17) - .5) * .018;
            pixels[y * width + x] = uint8_t(clamp(int((.978 + ridges + grain) * 255), 0, 255));
        }
    for (int level = 0;; ++level) {
        sjp::gfx::texImage2D(sjp::gfx::TEXTURE_2D, level, sjp::gfx::LUMINANCE, width, width, 0, sjp::gfx::LUMINANCE, sjp::gfx::UNSIGNED_BYTE,
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
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MIN_FILTER, sjp::gfx::LINEAR_MIPMAP_LINEAR);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MAG_FILTER, sjp::gfx::LINEAR);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_S, sjp::gfx::REPEAT);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_T, sjp::gfx::REPEAT);
    sjp::gfx::pixelStorei(sjp::gfx::UNPACK_ALIGNMENT, oldAlignment);
    sjp::visual::popAttrib();
    return texture;
}
sjp::gfx::Id makeMarkerTexture(std::vector<uint8_t> pixels, int width, int height) {
    sjp::gfx::Id texture = 0;
    sjp::gfx::genTextures(1, &texture);
    sjp::visual::pushAttrib(sjp::gfx::TEXTURE_BIT);
    int oldAlignment = 4;
    sjp::gfx::getIntegerv(sjp::gfx::UNPACK_ALIGNMENT, &oldAlignment);
    sjp::gfx::pixelStorei(sjp::gfx::UNPACK_ALIGNMENT, 1);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, texture);
    for (int level = 0;; ++level) {
        sjp::gfx::texImage2D(sjp::gfx::TEXTURE_2D, level, sjp::gfx::ALPHA, width, height, 0,
                           sjp::gfx::ALPHA, sjp::gfx::UNSIGNED_BYTE, pixels.data());
        if (width == 1 && height == 1)
            break;
        const int nextWidth = std::max(1, width / 2), nextHeight = std::max(1, height / 2);
        std::vector<uint8_t> next(nextWidth * nextHeight);
        for (int y = 0; y < nextHeight; ++y)
            for (int x = 0; x < nextWidth; ++x) {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                        sum += pixels[std::min(height - 1, y * 2 + dy) * width +
                                      std::min(width - 1, x * 2 + dx)];
                next[y * nextWidth + x] = uint8_t((sum + 2) / 4);
            }
        pixels.swap(next);
        width = nextWidth;
        height = nextHeight;
    }
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MIN_FILTER, sjp::gfx::LINEAR_MIPMAP_LINEAR);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MAG_FILTER, sjp::gfx::LINEAR);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_S, sjp::gfx::REPEAT);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_T, sjp::gfx::CLAMP_TO_EDGE);
    sjp::gfx::pixelStorei(sjp::gfx::UNPACK_ALIGNMENT, oldAlignment);
    sjp::visual::popAttrib();
    return texture;
}
sjp::gfx::Id makeSpruceMarkerTexture() {
    int width = 128, height = 64;
    std::vector<uint8_t> pixels(width * height, 0);
    // Flat overlapping twigs: the alpha mask supplies needles and uneven edges.
    // Wrap strokes across the tile seam; all markers share this small texture.
    auto stroke = [&](double x0, double y0, double x1, double y1, double radius, double opacity) {
        const double dx = x1 - x0, dy = y1 - y0, length2 = dx * dx + dy * dy;
        for (int y = std::max(0, int(std::floor(std::min(y0, y1) - radius - 1)));
             y <= std::min(height - 1, int(std::ceil(std::max(y0, y1) + radius + 1))); ++y)
            for (int x = int(std::floor(std::min(x0, x1) - radius - 1));
                 x <= int(std::ceil(std::max(x0, x1) + radius + 1)); ++x) {
                const double t = clamp(((x + .5 - x0) * dx + (y + .5 - y0) * dy) / length2, 0.0, 1.0);
                const double distance = std::hypot(x + .5 - x0 - t * dx, y + .5 - y0 - t * dy);
                const auto alpha = uint8_t(clamp(radius + .5 - distance, 0.0, 1.0) * opacity * 255);
                auto &pixel = pixels[y * width + (x % width + width) % width];
                pixel = std::max(pixel, alpha);
            }
    };
    for (int twig = 0; twig < 4; ++twig) {
        const double seed = twig * 71.3;
        const double x0 = twig * 32 - 12, y0 = 26 + hash(seed + 1) * 10;
        const double dx = 48 + hash(seed + 2) * 12, dy = (hash(seed + 3) - .5) * 12;
        stroke(x0, y0, x0 + dx, y0 + dy, .85, .95);
        for (int needle = 0; needle < 14; ++needle) {
            const double t = (needle + .5) / 14;
            const double x = x0 + dx * t, y = y0 + dy * t;
            for (int side : {-1, 1}) {
                const double length = (11 + hash(seed + needle * 13 + side) * 11) * (1 - .55 * t);
                stroke(x, y, x + 5 + hash(seed + needle * 7) * 6, y + side * length,
                       .55 + hash(seed + needle * 17 + side) * .35, .8 + hash(seed + needle) * .2);
            }
        }
    }
    return makeMarkerTexture(std::move(pixels), width, height);
}
sjp::gfx::Id makeSprayMarkerTexture() {
    constexpr int width = 128, height = 64;
    std::vector<uint8_t> pixels(width * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double center = .5 + .025 * std::sin(x * 2 * PI / width);
            const double radius = .32 + .02 * std::cos(x * 4 * PI / width);
            const double edge = clamp((radius - std::abs((y + .5) / height - center)) / .19, 0.0, 1.0);
            const double grain = hash(x * 13.71 + y * 8.17);
            pixels[y * width + x] = uint8_t(edge * (.62 + .38 * grain) * 245);
        }
    return makeMarkerTexture(std::move(pixels), width, height);
}
void distanceMarkerStrip(const Hill &h, double x, int distance, double width, double extent, Color color) {
    const double scale = h.scale(), step = 3 * scale, zStep = 10 * scale;
    const double gridStart = std::min(-170 * scale, h.inrunStart() - 60 * scale);
    const double halfWidth = width * .5 / std::hypot(1.0, h.slope(x));
    const double left = x - halfWidth * 1.7, right = x + halfWidth * 1.7;
    const double hillSeed = h.k * 17.13 + h.kMarkerX * .031;
    const double seed = hillSeed + distance * 31.71, phase = hash(seed) * 11;
    const double repeatLength = .75 + hash(seed + 7) * .6;
    const double sampleStep = clamp(1.25 * std::sqrt(scale), .8, 1.5);
    // Jumpers wear a broad, slightly wandering central lane from the landing
    // region into the outrun. The field is fixed per hill, never per frame.
    const double landingWear = smoothRange(h.k * .62, h.k * .95, distance);
    const double laneCenter = (smoothNoise(x / (22 * scale), hillSeed + 3) - .5) * 2.2;
    const double laneWidth = (3.8 + smoothNoise(x / (15 * scale), hillSeed + 9) * 1.4) * std::sqrt(scale);
    auto appearance = [&](Vec3 p) {
        const double broad = smoothNoise(p.z / 3.7, seed + 11);
        const double patch = smoothNoise(p.z / 1.1, seed + 23);
        const double gap = smoothRange(.12, .42, patch);
        const double lane = (p.z - laneCenter) / laneWidth;
        const double wear = landingWear * std::exp(-lane * lane) *
                            (.78 + .22 * smoothNoise(p.z / 2.4 + x / (9 * scale), hillSeed + 41));
        Color c = shade(color, .88 + .19 * broad);
        c.a = float(color.a * (.38 + .62 * broad) * gap * (1 - .88 * wear));
        return c;
    };
    // Use the already rendered snow mesh's straight segments as the receiver.
    // No hill heights, curve samples or measurement positions are modified.
    for (double a = left; a < right - 1e-8;) {
        const double cell = gridStart + std::floor((a - gridStart) / step + 1e-7) * step;
        const double b = std::min(right, cell + step);
        for (double z = -extent * scale; z < extent * scale - 1e-8;) {
            const double zCell = std::floor(z / zStep + 1e-7) * zStep;
            const double zz = std::min({extent * scale, zCell + zStep, z + sampleStep});
            const double y00 = terrainY(h, cell, zCell), y01 = terrainY(h, cell, zCell + zStep),
                         y11 = terrainY(h, cell + step, zCell + zStep), y10 = terrainY(h, cell + step, zCell);
            auto diagonal = [&](Vec3 p) { return (p.z - zCell) / zStep - (p.x - cell) / step; };
            // Clip the strip to both receiver triangles, including their shared
            // diagonal. The overlay then stays exactly on the unchanged snow.
            for (int side : {-1, 1}) {
                const std::array<Vec3, 4> corners{{{a, 0, z}, {a, 0, zz}, {b, 0, zz}, {b, 0, z}}};
                std::vector<Vec3> polygon;
                polygon.reserve(5);
                Vec3 previous = corners.back();
                double before = side * diagonal(previous);
                for (Vec3 p : corners) {
                    const double after = side * diagonal(p);
                    if ((before >= 0) != (after >= 0))
                        polygon.push_back(previous + (p - previous) * (before / (before - after)));
                    if (after >= 0)
                        polygon.push_back(p);
                    previous = p;
                    before = after;
                }
                if (polygon.size() < 3)
                    continue;
                for (auto &p : polygon) {
                    const double u = (p.x - cell) / step, v = (p.z - zCell) / zStep;
                    p.y = side > 0 ? y00 * (1 - v) + y01 * (v - u) + y11 * u
                                   : y00 * (1 - u) + y11 * v + y10 * (u - v);
                }
                sjp::visual::begin(sjp::gfx::TRIANGLE_FAN);
                const Vec3 normal = cross(polygon[1] - polygon[0], polygon[2] - polygon[0]).normalized();
                sjp::visual::normal(normal.x, normal.y, normal.z);
                for (const auto &p : polygon) {
                    // Vary twig size and edge position smoothly, keeping the
                    // nominal measurement location and receiver mesh intact.
                    const double size = .65 + .7 * smoothNoise(p.z / 2.2, seed + 53);
                    const double drift = (smoothNoise(p.z / 3.1, seed + 67) - .5) * halfWidth * .55;
                    col(appearance(p));
                    sjp::visual::texCoord(p.z / repeatLength + phase + .25 * smoothNoise(p.z / 2.7, seed + 79),
                                         .5 + (p.x - x - drift) / (2 * halfWidth * size));
                    vertex(p);
                }
                sjp::visual::end();
            }
            z = zz;
        }
        a = b;
    }
}
void contactShadow(const Hill &h, Vec3 p, double rx, double rz, float opacity) {
    constexpr int sides = 16, rings = 4;
    for (int ring = 0; ring < rings; ++ring) {
        const double inner = ring / double(rings), outer = (ring + 1) / double(rings);
        sjp::visual::begin(sjp::gfx::QUAD_STRIP);
        for (int i = 0; i <= sides; ++i) {
            const double angle = i * 2 * PI / sides;
            for (double radius : {outer, inner}) {
                const double x = p.x + std::cos(angle) * rx * radius, z = p.z + std::sin(angle) * rz * radius;
                col({.12f, .22f, .31f, float(opacity * (1 - radius) * (1 - radius))});
                vertex({x, terrainY(h, x, z) + .018, z});
            }
        }
        sjp::visual::end();
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
void terraceSlab(const visual::CameraSurface &snow, double x0, double x1, double z0, double z1,
                 double top, Color c) {
    const Vec3 a{x0, top, z0}, b{x0, top, z1}, d{x1, top, z0}, e{x1, top, z1};
    const Vec3 aa{x0, snow.height(x0, z0) - .16, z0}, bb{x0, snow.height(x0, z1) - .16, z1},
               dd{x1, snow.height(x1, z0) - .16, z0}, ee{x1, snow.height(x1, z1) - .16, z1};
    quad(a, b, e, d, c);
    quad(aa, dd, ee, bb, c);
    quad(aa, a, d, dd, shade(c, .88));
    quad(bb, ee, e, b, shade(c, .88));
    quad(aa, bb, b, a, shade(c, .94));
    quad(dd, d, e, ee, shade(c, .94));
}
void standGuard(Vec3 a, Vec3 b) {
    const Color post{.44f, .54f, .58f, 1}, rail{.55f, .64f, .68f, 1};
    beam(a, a + Vec3{0, 1.12, 0}, .028, post);
    beam(b, b + Vec3{0, 1.12, 0}, .028, post);
    beam(a + Vec3{0, 1.08, 0}, b + Vec3{0, 1.08, 0}, .033, rail);
    beam(a + Vec3{0, .55, 0}, b + Vec3{0, .55, 0}, .020, post);
}
void standEndGuard(const visual::StandTerrace &t, double x, int side, double near) {
    const Color post{.44f, .54f, .58f, 1}, rail{.55f, .64f, .68f, 1};
    // One continuous stair railing across the three lateral seating tiers.
    // Each post starts on its own horizontal landing, including the turns.
    const std::array<Vec3, 4> feet{{
        {x, t.floor(0), side * (near + 1.40)},
        {x, t.floor(1), side * (near + 2.60)},
        {x, t.floor(2), side * (near + 3.80)},
        {x, t.floor(2), side * (near + 4.90)},
    }};
    for (const auto p : feet)
        beam(p, p + Vec3{0, 1.12, 0}, .028, post);
    for (size_t i = 1; i < feet.size(); ++i) {
        beam(feet[i - 1] + Vec3{0, 1.08, 0}, feet[i] + Vec3{0, 1.08, 0}, .033, rail);
        beam(feet[i - 1] + Vec3{0, .55, 0}, feet[i] + Vec3{0, .55, 0}, .020, post);
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
    case 'F':
        return {31, 16, 16, 30, 16, 16, 16};
    case 'H':
        return {17, 17, 17, 31, 17, 17, 17};
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
    case 'V':
        return {17, 17, 17, 17, 17, 10, 4};
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
struct BoardFrame {
    Vec3 origin, along, up;

    Vec3 point(double x, double height, double z) const {
        const Vec3 p = origin + along * x + up * height;
        return {p.x, p.y, z};
    }
};
void boardFace(const BoardFrame &frame, double left, double right, double bottom, double top,
               double z, int side, Color color) {
    const Vec3 a = frame.point(left, bottom, z), b = frame.point(right, bottom, z),
               c = frame.point(right, top, z), d = frame.point(left, top, z);
    if (side > 0)
        quad(d, c, b, a, color);
    else
        quad(a, b, c, d, color);
}
void boardText(const BoardFrame &frame, const std::string &text, double x, double offset,
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
                    boardFace(frame, xx, right, yy - cell * .88, yy, z, side, color);
                }
    }
}
void advertisement(const Hill &h, double x, double width, double front, int side, Color accent,
                   const std::string &text) {
    // Rotate a rigid rectangle along the existing terrain's local tangent.
    // The valley term is the x derivative of terrainY, not a terrain change.
    const double edge = std::max(0.0, std::abs(front) - 19 * h.scale());
    const double slope = h.slope(x) + std::cos(x * .017 + front * .013) * edge * .018 * .017;
    const Vec3 along = Vec3{1, slope, 0}.normalized();
    BoardFrame frame{{x, terrainY(h, x, front), front}, along, {-along.y, along.x, 0}};
    const double left = -width * .5, right = width * .5;
    const double zFront = front - side * .15, zBack = front + side * .15;
    // A tangent can intersect a curved surface away from its centre. Lift the
    // whole board if necessary, leaving the terrain and the rectangle intact.
    double lift = 0;
    const int samples = std::max(2, int(std::ceil(width / .25)));
    for (int i = 0; i <= samples; ++i)
        for (double z : {zFront, zBack}) {
            const Vec3 p = frame.point(left + width * i / samples, .16, z);
            lift = std::max(lift, terrainY(h, p.x, p.z) + .08 - p.y);
        }
    frame.origin.y += lift;
    const Vec3 ta = frame.point(left, 1.79, zFront), tb = frame.point(left, 1.79, zBack),
               tc = frame.point(right, 1.79, zBack), td = frame.point(right, 1.79, zFront);
    const Vec3 ba = frame.point(left, .16, zFront), bb = frame.point(right, .16, zFront),
               bc = frame.point(right, .16, zBack), bd = frame.point(left, .16, zBack);
    if (side > 0) {
        quad(ta, tb, tc, td, {.73f, .82f, .84f, 1});
        quad(ba, bb, bc, bd, {.31f, .43f, .48f, 1});
    } else {
        quad(td, tc, tb, ta, {.73f, .82f, .84f, 1});
        quad(bd, bc, bb, ba, {.31f, .43f, .48f, 1});
    }
    boardFace(frame, left, right, .16, 1.79, zFront, side, {.10f, .28f, .33f, 1});
    boardFace(frame, left, right, .16, 1.79, zBack, -side, {.20f, .32f, .37f, 1});
    for (double end : {left, right})
        quad(frame.point(end, .16, zFront), frame.point(end, 1.79, zFront),
             frame.point(end, 1.79, zBack), frame.point(end, .16, zBack), {.42f, .54f, .58f, 1});
    for (int face : {side, -side}) {
        const double z = face == side ? zFront : zBack;
        boardFace(frame, left, right, 1.745, 1.82, z - face * .02, face, {.73f, .82f, .84f, 1});
        const double accentLeft = face > 0 ? right - width * .1025 : left + width * .0175;
        const double accentRight = face > 0 ? right - width * .0175 : left + width * .1025;
        boardFace(frame, accentLeft, accentRight, .29, 1.61, z - face * .025, face, accent);
        boardText(frame, text, -face * width * .025, .97, width * .78, .74, z - face * .035, face,
                  {.91f, .96f, .95f, 1});
    }
    for (double offset : {-.39, .39}) {
        const double z = zBack + side * .12;
        const Vec3 foot = frame.point(offset * width, 0, z);
        beam({foot.x, terrainY(h, foot.x, z) - .035, z}, frame.point(offset * width, 1.78, z), .04,
             {.43f, .54f, .58f, 1});
        beam(frame.point(offset * width, .80, z), frame.point(offset * width, .80, zBack), .035,
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
    double size = 1, verticalScale = 1, seed = 0, yaw = 0;
    Color coat{};
    int side = 1;
    bool present = true, animated = false, flag = false;
};
Spectator spectatorAt(const visual::GrandstandLayout &stands, int side, int section, int row, int person) {
    const auto &stand = stands.section(side, section);
    const double width = stand.width, center = stand.center;
    const double seed = section * 91.0 + row * 13.0 + person * 7.0 + (side < 0 ? 431.0 : 0);
    Spectator p;
    p.size = .74 + hash(seed + 1) * .31;
    // Adult standing heights of 1.60-1.90 m, without widening their footprint.
    p.verticalScale = (1.60 + hash(seed + 4.7) * .30 - .155 * p.size) / (1.155 * p.size);
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
    double x = center + (person - (capacity - 1) * .5) * pitch + cluster + jitter;
    const double z = side * (stands.near + 1.95 + row * 1.2 + (hash(seed + 41.9) - .5) * .06);
    const auto &terrace = stand.at(x);
    // Both shoes remain on the same horizontal wooden landing, including
    // during arm animation. Keep them away from an exposed step boundary.
    x = clamp(x, terrace.x0 + .38, terrace.x1 - .38);
    p.feet = {x, terrace.woodTop(row), z};
    return p;
}
Vec3 spectatorPoint(const Spectator &p, Vec3 local) {
    const double c = std::cos(p.yaw), s = std::sin(p.yaw);
    return p.feet + Vec3{local.x * c + local.z * s, local.y * p.verticalScale,
                        -local.x * s + local.z * c};
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
    spectatorBox(p, {0, 1.00 * s, 0}, {.13 * s, .16 * s, .13 * s}, {.77f, .65f, .55f, 1});
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
        // Self-lit warm glass; the vertex shader raises emission with the
        // current lamp strength without rebuilding these cached faces.
        const float emission[]{.28f, .27f, .22f, 1};
        sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::EMISSION, emission);
        const double front = z + (z > 0 ? -.345 : .345);
        quad({xx - .35, yy - .125, front}, {xx + .35, yy - .125, front}, {xx + .35, yy + .125, front},
             {xx - .35, yy + .125, front}, {.94f, .91f, .78f, 1});
        const float noEmission[]{0, 0, 0, 1};
        sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::EMISSION, noEmission);
    }
}
} // namespace

void Renderer::terrain(const Hill &h) {
    const double s = h.scale();
    size_t index = 0;
    for (size_t i = 0; i < hills().size(); ++i)
        if (h.id == hills()[i].id)
            index = i;
    landscapeUse[index] = ++landscapeClock;
    if (landscapes[index]) {
        sjp::gfx::callList(landscapes[index]);
        return;
    }
    if (!snowTexture)
        snowTexture = makeSnowTexture(true);
    if (!naturalSnowTexture)
        naturalSnowTexture = makeSnowTexture(false);
    if (!spruceMarkerTexture)
        spruceMarkerTexture = makeSpruceMarkerTexture();
    if (!sprayMarkerTexture)
        sprayMarkerTexture = makeSprayMarkerTexture();
    // Keep the current and most recently visited venue. The full 32-venue
    // cache retained millions of unused vertices between jumps.
    size_t count = 0, oldest = index;
    for (size_t i = 0; i < landscapes.size(); ++i)
        if (landscapes[i]) {
            ++count;
            if (oldest == index || landscapeUse[i] < landscapeUse[oldest])
                oldest = i;
        }
    if (count >= 2) {
        sjp::gfx::deleteLists(landscapes[oldest], 1);
        landscapes[oldest] = 0;
    }
    landscapes[index] = sjp::gfx::genLists(1);
    sjp::gfx::newList(landscapes[index], sjp::gfx::COMPILE_AND_EXECUTE);
    visual::StaticBatch staticBatch;
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::LIGHTING_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::TEXTURE_BIT | sjp::gfx::POLYGON_BIT);
    const float matte[]{.025f, .035f, .045f, 1};
    sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SPECULAR, matte);
    sjp::visual::materialf(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SHININESS, 8);
    sjp::visual::enable(sjp::gfx::TEXTURE_2D);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, naturalSnowTexture);
    sjp::visual::texEnvi(sjp::gfx::TEXTURE_ENV, sjp::gfx::TEXTURE_ENV_MODE, sjp::gfx::MODULATE);
    // Same bounds, cell dimensions, and vertex heights as the existing hill.
    // Grooming, grain, and broad snow color variation are material-only.
    for (double x = std::min(-170 * s, h.inrunStart() - 60 * s); x < std::max(420 * s, h.end() + 80 * s);
         x += 3 * s)
        for (double z = -200 * s; z < 200 * s; z += 10 * s)
            snowQuad(h, {x, terrainY(h, x, z), z}, {x, terrainY(h, x, z + 10 * s), z + 10 * s},
                     {x + 3 * s, terrainY(h, x + 3 * s, z + 10 * s), z + 10 * s},
                     {x + 3 * s, terrainY(h, x + 3 * s, z), z});
    // Reuse the exact receiver triangles for a finite, softly blended prepared
    // strip. Polygon offset avoids coplanar flicker; this pass never writes depth.
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::TEXTURE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::POLYGON_BIT);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, snowTexture);
    sjp::visual::enable(sjp::gfx::BLEND);
    sjp::visual::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::visual::depthMask(sjp::gfx::FALSE);
    sjp::visual::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    sjp::visual::polygonOffset(-1, -1);
    for (double x = std::min(-170 * s, h.inrunStart() - 60 * s);
         x < std::min(std::max(420 * s, h.end() + 80 * s), h.outrunBottom() + 47); x += 3 * s)
        for (double z = -200 * s; z < 200 * s; z += 10 * s)
            snowQuad(h, {x, terrainY(h, x, z), z}, {x, terrainY(h, x, z + 10 * s), z + 10 * s},
                     {x + 3 * s, terrainY(h, x + 3 * s, z + 10 * s), z + 10 * s},
                     {x + 3 * s, terrainY(h, x + 3 * s, z), z}, true);
    sjp::visual::popAttrib();
    sjp::visual::disable(sjp::gfx::TEXTURE_2D);
    // Three broad alpine ridgelines sit well behind the forest. Their lower
    // silhouette opens the sky and lets atmospheric fog separate the layers.
    for (int layer = 2; layer >= 0; --layer)
        mountainRidge(s, layer);
    // All soft ground contacts share one state pass, instead of switching
    // depth/blend/lighting hundreds of times between individual trees.
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::POLYGON_BIT);
    sjp::visual::disable(sjp::gfx::LIGHTING);
    sjp::visual::disable(sjp::gfx::TEXTURE_2D);
    sjp::visual::enable(sjp::gfx::BLEND);
    sjp::visual::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::visual::depthMask(sjp::gfx::FALSE);
    sjp::visual::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    sjp::visual::polygonOffset(-1, -1);
    for (int i = 0; i < 420; ++i) {
        const Tree tree = treeAt(h, i);
        if (visibleTree(h, tree)) {
            contactShadow(h, tree.base, tree.height * .61, tree.height * .48, .20f);
            visual::flush();
        }
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
    sjp::visual::popAttrib();
    for (int i = 0; i < 420; ++i) {
        const Tree tree = treeAt(h, i);
        if (!visibleTree(h, tree))
            continue;
        box(tree.base + Vec3{0, tree.height * .22, 0},
            {tree.height * .085, tree.height * .44, tree.height * .085}, {.28f, .25f, .22f, 1});
        for (int tier = 0; tier < tree.tiers; ++tier)
            if (!(tree.animated && tier == tree.tiers - 1))
                treeBranch(tree, tier);
        // Preserve tree order while giving each tree its own bounded cached
        // ranges; a distant neighbor cannot keep an off-camera tree visible.
        visual::flush();
    }
    // Existing snowy track and its exact h.inrun profile; details sit beside
    // or underneath it, and never alter the track's contour or width.
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, snowTexture);
    for (double x = h.inrunStart(); x < 0; x += 2 * s) {
        const double xx = std::min(0.0, x + 2 * s), y = h.inrun(x), yy = h.inrun(xx);
        sjp::visual::enable(sjp::gfx::TEXTURE_2D);
        texturedQuad({x, y, -2.3}, {x, y, 2.3}, {xx, yy, 2.3}, {xx, yy, -2.3}, {.92f, .96f, .97f, 1});
        sjp::visual::disable(sjp::gfx::TEXTURE_2D);
        quad({x, y - .62, 2.3}, {xx, yy - .62, 2.3}, {xx, yy, 2.3}, {x, y, 2.3}, {.33f, .29f, .25f, 1});
        quad({x, y - .62, -2.3}, {x, y, -2.3}, {xx, yy, -2.3}, {xx, yy - .62, -2.3}, {.33f, .29f, .25f, 1});
        for (double z : {-.22, .22})
            quad({x, y + .014, z - .055}, {x, y + .014, z + .055}, {xx, yy + .014, z + .055},
                 {xx, yy + .014, z - .055}, {.48f, .67f, .76f, 1});
        for (double z : {-2.2, 2.2}) {
            quad({x, y + .021, z - .09}, {x, y + .021, z + .09}, {xx, yy + .021, z + .09},
                 {xx, yy + .021, z - .09}, {.19f, .42f, .53f, 1});
        }
        if (xx > -.16) {
            // A narrow metal-colored transverse edge, like a real takeoff
            // table. Follow the existing snow triangle, without changing it.
            const double edgeX = std::max(x, -.16);
            const double edgeY = y + (yy - y) * (edgeX - x) / (xx - x);
            if (xx - edgeX > 1e-8)
                quad({edgeX, edgeY + .018, -2.3}, {edgeX, edgeY + .018, 2.3},
                     {xx, yy + .018, 2.3}, {xx, yy + .018, -2.3}, {.16f, .19f, .22f, 1});
        }
    }
    inrunStructure(h, inrunLayouts[index]);
    // The continuous service stairs share these short seat-area outriggers.
    // No additional overlapping/sloping platform covers their treads.
    {
        constexpr int side = visual::inrunServiceSide;
        for (double x : {h.start() - 3.4, h.start() + .25}) {
            const double z = side * 3.35, y = h.inrun(x);
            beam({x, y - .46, side * 2.12}, {x, y - .46, side * 3.65}, .10, {.34f, .41f, .44f, 1});
            beam({x, y - .46, z}, {x, y - .19, z}, .075, {.32f, .40f, .44f, 1});
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
    for (double x : {cabinX - 1.08, cabinX + 1.08}) {
        const double y = h.inrun(x) - .46;
        beam({x, y, -2.12}, {x, y, cabinZ - .75}, .12, {.34f, .41f, .44f, 1});
        beam({x, y, -2.12}, {x, y - .75, -2.12}, .08, {.34f, .41f, .44f, 1});
        beam({x, y - .75, -2.12}, {x, y, cabinZ - .75}, .07, {.43f, .49f, .51f, 1});
        for (double z : {cabinZ - .60, cabinZ + .60})
            beam({x, y, z}, {x, cabinFloor - .12, z}, .075, {.34f, .43f, .47f, 1});
    }
    box({cabinX, cabinFloor - .065, cabinZ}, {2.65, .13, 1.62}, {.38f, .44f, .47f, 1});
    box({cabinX, cabinFloor + 1.35, cabinZ}, {2.4, 2.7, 1.5}, {.24f, .31f, .35f, 1});
    box({cabinX, cabinFloor + 2.80, cabinZ}, {2.80, .20, 1.95}, {.88f, .93f, .95f, 1});
    box({cabinX, cabinFloor + 1.65, cabinZ + .78}, {1.95, 1.03, .05}, {.24f, .51f, .63f, 1});
    for (double x : {cabinX - .65, cabinX + .65})
        beam({x, cabinFloor + 1.13, cabinZ + .815}, {x, cabinFloor + 2.16, cabinZ + .815}, .021,
             {.65f, .74f, .78f, 1});
    // The entrance is on the uphill end, where the horizontal hut floor
    // meets the service stairs. A short landing bridges to the side deck.
    box({cabinX - 1.208, cabinFloor + 1.04, cabinZ}, {.03, 2.08, .70}, {.15f, .22f, .25f, 1});
    box({cabinX - 1.23, cabinFloor + .94, cabinZ + .20}, {.025, .045, .045}, {.70f, .74f, .73f, 1});
    const double landingX = cabinX - 1.55, landingBase = h.inrun(landingX) - .46;
    beam({landingX, landingBase, -2.12}, {landingX, landingBase, -4.95}, .08, {.34f, .41f, .44f, 1});
    if (cabinFloor - .09 > landingBase + .04)
        for (double z : {-3.60, -4.80})
            beam({landingX, landingBase, z}, {landingX, cabinFloor - .09, z}, .05, {.34f, .41f, .44f, 1});
    box({landingX, cabinFloor - .07, -4.20}, {.70, .14, 1.50}, {.43f, .35f, .28f, 1});
    for (Vec3 p : {Vec3{landingX - .35, cabinFloor, -3.68},
                   Vec3{landingX - .35, cabinFloor, -4.95},
                   Vec3{landingX + .35, cabinFloor, -4.95}})
        beam(p, p + Vec3{0, 1.08, 0}, .035, {.45f, .51f, .53f, 1});
    for (double height : {.56, 1.04}) {
        beam({landingX - .35, cabinFloor + height, -3.68},
             {landingX - .35, cabinFloor + height, -4.95}, .030, {.45f, .51f, .53f, 1});
        beam({landingX - .35, cabinFloor + height, -4.95},
             {landingX + .35, cabinFloor + height, -4.95}, .030, {.45f, .51f, .53f, 1});
    }
    signText("PRO", {cabinX, cabinFloor + .55, cabinZ + .762}, 1.55, .43, -1, {.88f, .94f, .94f, 1});
    // Granbar and red spray share a cached overlay pass with two small textures.
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::TEXTURE_BIT | sjp::gfx::DEPTH_BUFFER_BIT |
                          sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::POLYGON_BIT | sjp::gfx::CURRENT_BIT);
    sjp::visual::disable(sjp::gfx::LIGHTING);
    sjp::visual::enable(sjp::gfx::TEXTURE_2D);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, spruceMarkerTexture);
    sjp::visual::enable(sjp::gfx::BLEND);
    sjp::visual::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::visual::depthMask(sjp::gfx::FALSE);
    sjp::visual::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    sjp::visual::polygonOffset(-1, -1);
    for (int d = 10; d <= h.k + 50; d += 10)
        if (d != h.k)
            distanceMarkerStrip(h, h.atDistance(d), d, .52, 18, {.23f, .30f, .24f, .76f});
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, sprayMarkerTexture);
    if (h.k % 10 == 0)
        distanceMarkerStrip(h, h.atDistance(h.k) + .09, h.k, .48, 18, {.76f, .38f, .36f, .72f});
    distanceMarkerStrip(h, h.kMarkerX + .2, h.k, .75, 20, {.79f, .36f, .34f, .76f});
    sjp::visual::popAttrib();
    // Keep the original K-point X/Z coordinates. Upright signs clear the
    // uphill snow across their full width, and their posts embed in the mesh.
    {
        const double x = h.kMarkerX;
        constexpr double width = 2.45, height = 1.12, depth = .22;
        const visual::CameraSurface snow(h);
        const Color c{.94f, .28f, .28f, 1};
        for (int side : {-1, 1}) {
            const double z = side * 20 * s, y = snow.height(x, z);
            double highestSnow = y;
            for (double xx : {x - width * .5, x, x + width * .5})
                for (double zz : {z - depth * .5, z, z + depth * .5})
                    highestSnow = std::max(highestSnow, snow.height(xx, zz));
            const double panelY = std::max(y + 1.1, highestSnow + .35 + height * .5);
            const double postTop = std::max(y + 3.0, panelY + height * .5 + .18);
            beam({x, y - .30, z}, {x, postTop, z}, .08, c);
            box({x, panelY, z}, {width, height, depth}, {.91f, .24f, .24f, 1});
            signText("K" + std::to_string(h.k), {x, panelY + .02, z - side * .116}, 2.15, .65, side,
                     {.98f, .97f, .92f, 1});
            box({x, postTop, z}, {.24, .20, .24}, {.94f, .95f, .90f, 1});
        }
    }
    // Terraced concrete, warm timber seating, railings, sponsored panels and
    // winter spectators give the existing arena scale and a sporting identity.
    const auto &stands = grandstandLayouts[index];
    const visual::CameraSurface arenaSnow(h);
    const double arenaNear = stands.near;
    for (int side : {-1, 1}) {
        const double front = side * (arenaNear - 1.0);
        for (int section = 0; section < visual::GrandstandLayout::sectionCount; ++section) {
            const auto &stand = stands.section(side, section);
            const double x = stand.center, width = stand.width;
            const Color accent = section % 3 == 0 ? Color{.86f, .37f, .25f, 1} : Color{.25f, .68f, .65f, 1};
            advertisement(h, x, width, front, side, accent, section % 3 == 0 ? "NORDIC" : "THE PERFECT V");
            for (size_t bay = 0; bay < stand.terraces.size(); ++bay) {
                const auto &t = stand.terraces[bay];
                const auto *left = bay ? &stand.terraces[bay - 1] :
                    (section ? &stands.section(side, section - 1).terraces.back() : nullptr);
                const auto *right = bay + 1 < stand.terraces.size() ? &stand.terraces[bay + 1] :
                    (section + 1 < visual::GrandstandLayout::sectionCount ?
                     &stands.section(side, section + 1).terraces.front() : nullptr);
                for (int row = 0; row < visual::GrandstandLayout::rowCount; ++row) {
                    const double za = side * (arenaNear + 1.35 + row * 1.2), zb = za + side * 1.2;
                    const double z0 = std::min(za, zb), z1 = std::max(za, zb), floor = t.floor(row);
                    terraceSlab(arenaSnow, t.x0, t.x1, z0, z1, floor, {.43f, .49f, .52f, 1});
                    box({(t.x0 + t.x1) * .5, floor + .09, (z0 + z1) * .5},
                        {t.x1 - t.x0 - .16, .08, .60}, {.40f, .32f, .26f, 1});
                }
                // Lower tiers remain open; substantial drops get one shared
                // edge railing instead of separate barriers around each row.
                if (!left || t.base - left->base > 1.0)
                    standEndGuard(t, t.x0 + .06, side, arenaNear);
                if (!right || t.base - right->base > 1.0)
                    standEndGuard(t, t.x1 - .06, side, arenaNear);
                const double back = side * (arenaNear + 4.90);
                standGuard({t.x0 + .08, t.floor(2), back}, {t.x1 - .08, t.floor(2), back});
                const double lip = side * (arenaNear + 1.39);
                const double frontDrop = t.floor(0) - std::min(arenaSnow.height(t.x0, lip),
                                                             arenaSnow.height(t.x1, lip));
                if (frontDrop > 1.15)
                    standGuard({t.x0 + .08, t.floor(0), lip}, {t.x1 - .08, t.floor(0), lip});
            }
            for (int row = 0; row < visual::GrandstandLayout::rowCount; ++row)
                for (int person = 0; person < 4; ++person)
                    spectatorBody(spectatorAt(stands, side, section, row, person));
        }
        for (int mast = 0; mast < gfx::ARENA_LIGHT_COLUMNS; ++mast) {
            const auto fixture = visual::floodlightMast(h, mast, side);
            floodlight(h, fixture.base.x, fixture.base.z, fixture.size);
        }
    }
    staticBatch.finish();
    // Finish writes current attributes, so restore the caller's state last.
    sjp::visual::popAttrib();
    sjp::gfx::endList();
}

void Renderer::scenery(const Hill &h, const Frame &frame, double time) {
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::LIGHTING_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::TEXTURE_BIT);
    sjp::visual::disable(sjp::gfx::TEXTURE_2D);
    const double strength = clamp(std::abs(double(frame.windSpeed)) / 8.0, 0.05, 1.0);
    const Vec3 wind = visual::windDirection(frame);
    // Only one small upper bough on every twenty-sixth tree is animated.
    for (int i = 0; i < 420; i += 26) {
        const Tree tree = treeAt(h, i);
        if (!visibleTree(h, tree))
            continue;
        const float radius = float(tree.height);
        if (!gfx::boundsVisible({float(tree.base.x) - radius, float(tree.base.y), float(tree.base.z) - radius},
                                {float(tree.base.x) + radius, float(tree.base.y) + radius * 1.5f,
                                 float(tree.base.z) + radius}))
            continue;
        const double bend =
            (std::sin(time * 1.35 + tree.seed) + .35 * std::sin(time * 2.9 + tree.seed * .7)) * tree.height *
            .016 * strength;
        treeBranch(tree, tree.tiers - 1, wind * bend);
    }
    sjp::visual::disable(sjp::gfx::CULL_FACE);
    size_t venue = 0;
    for (size_t i = 0; i < hills().size(); ++i)
        if (h.id == hills()[i].id)
            venue = i;
    const auto &stands = grandstandLayouts[venue];
    for (int side : {-1, 1})
        for (int section = 0; section < 14; ++section)
            for (int row = 0; row < 3; ++row)
                for (int person = 0; person < 4; ++person) {
                    const Spectator p = spectatorAt(stands, side, section, row, person);
                    if (!p.present || !p.animated)
                        continue;
                    const float radius = float(p.size * 2.5);
                    if (!gfx::boundsVisible({float(p.feet.x) - radius, float(p.feet.y) - radius,
                                             float(p.feet.z) - radius},
                                            {float(p.feet.x) + radius,
                                             float(p.feet.y + p.size * (1.6 * p.verticalScale + 1.5)),
                                             float(p.feet.z) + radius}))
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
    sjp::visual::popAttrib();
}
} // namespace sjp
