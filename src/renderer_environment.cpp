#include "original_skeleton.h"
#include "renderer.h"
#include "visual_contact_shadow.h"
#include "visual_floodlights.h"
#include "visual_mountains.h"
#include "visual_weather.h"
#include "visual_snow_accumulation.h"
#include "visual_scenery_shadow.h"
#include "visual_scenery_mask.h"
#include "visual_snow_material.h"
#include "visual_snow_shadow.h"
#include "visual_terrain_sun.h"
#include "visual_sun_shadow_field.h"
#include "visual_shadow.h"
#include "visual_tree_geometry.h"
#include <algorithm>
#include <array>
#include <chrono>
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
// The ski corridor, landing area and inrun retain the original coordinates.
// Distant decorative flanks and the extension past the prepared arena turn downhill.
double terrainY(const Hill &h, double x, double z) {
    return visual::backdropGround(h, x, z);
}
using visual::preparedSnowWeight;
Color snowColor(const Hill &h, double x, double z) {
    // Broad wind-shaped tonal bands, baked once into the existing vertices.
    const double variation = .024 * std::sin(x * .047 + z * .031) + .012 * std::sin(x * .13 - z * .071);
    const double corridor = preparedSnowWeight(h, x, z);
    const double pocket = smoothRange(.006, .08, h.slope(x + 3 * h.scale()) - h.slope(x - 3 * h.scale()));
    const double edge = std::exp(-std::pow((std::abs(z) - 24 * h.scale()) / (3 * h.scale()), 2)) * corridor;
    const Color color{float(.83 + .065 * corridor + variation - .027 * pocket - .025 * edge),
                      float(.89 + .035 * corridor + variation - .014 * pocket - .023 * edge),
                      float(.94 + .012 * corridor + variation * .6 - .003 * pocket - .016 * edge), 1};
    double occlusion = 0;
    if (x >= h.inrunStart() && x <= 0 && std::abs(z) < 4.8) {
        const double gap = std::max(0.0, h.inrun(x) - .62 - terrainY(h, x, z));
        occlusion = (1 - smoothRange(2.1, 4.8, std::abs(z))) * (1 - smoothRange(.2, 6.0, gap));
    }
    // Soft ambient contact under low decking, baked into both ground layers.
    // The separate ski-track material never receives this darkening.
    return shade(color, 1 - .10 * occlusion);
}
Vec3 terrainSnowNormal(const Hill &h, Vec3 p) {
    const double dx = 3 * h.scale(), dz = 10 * h.scale();
    return Vec3{-(terrainY(h, p.x + dx, p.z) - terrainY(h, p.x - dx, p.z)) / (2 * dx), 1,
                -(terrainY(h, p.x, p.z + dz) - terrainY(h, p.x, p.z - dz)) / (2 * dz)}
        .normalized();
}
void snowQuad(const Hill &h, Vec3 a, Vec3 b, Vec3 c, Vec3 d, bool groomed = false) {
    if (groomed && preparedSnowWeight(h, a.x, a.z) == 0 && preparedSnowWeight(h, b.x, b.z) == 0 &&
        preparedSnowWeight(h, c.x, c.z) == 0 && preparedSnowWeight(h, d.x, d.z) == 0)
        return;
    // Both snow layers share smooth vertex normals across the entire valley.
    // Only lighting changes; the original terrain vertices remain untouched.
    sjp::visual::begin(sjp::gfx::QUADS);
    for (Vec3 p : {a, b, c, d}) {
        const Vec3 normal = terrainSnowNormal(h, p);
        sjp::visual::normal(normal.x, normal.y, normal.z);
        Color color = snowColor(h, p.x, p.z);
        if (groomed)
            color.a = float(preparedSnowWeight(h, p.x, p.z) * visual::valleyBackOpacity(h, p.x));
        else
            color.a = float(visual::valleyEdgeOpacity(h, p.z, p.x));
        col(color);
        sjp::visual::texCoord(p.x / 13.0, p.z / 13.0);
        vertex(p);
    }
    sjp::visual::end();
}
void snowPatch(const Hill &h, double x, double z, bool groomed = false) {
    const double dx = 3 * h.scale(), dz = 10 * h.scale();
    if (!visual::refinedSnowCell(h, x) || std::abs(z + dz * .5) > 20 * h.scale()) {
        snowQuad(h, {x, terrainY(h, x, z), z}, {x, terrainY(h, x, z + dz), z + dz},
                 {x + dx, terrainY(h, x + dx, z + dz), z + dz}, {x + dx, terrainY(h, x + dx, z), z}, groomed);
        return;
    }
    visual::snowCellTriangles(h, x, z, [&](double xx, double zz) { return terrainY(h, xx, zz); },
                             [&](Vec3 a, Vec3 b, Vec3 c) {
        if (groomed && preparedSnowWeight(h, a.x, a.z) == 0 &&
            preparedSnowWeight(h, b.x, b.z) == 0 && preparedSnowWeight(h, c.x, c.z) == 0)
            return;
        sjp::visual::begin(sjp::gfx::TRIANGLES);
        for (Vec3 p : {a, b, c}) {
            const auto normal = terrainSnowNormal(h, p);
            sjp::visual::normal(normal.x, normal.y, normal.z);
            Color color = snowColor(h, p.x, p.z);
            color.a = float(groomed ? preparedSnowWeight(h, p.x, p.z) * visual::valleyBackOpacity(h, p.x)
                                    : visual::valleyEdgeOpacity(h, p.z, p.x));
            col(color);
            sjp::visual::texCoord(p.x / 13.0, p.z / 13.0);
            vertex(p);
        }
        sjp::visual::end();
    });
}
void inrunSnowQuad(const Hill &h, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    col(color);
    sjp::visual::begin(sjp::gfx::QUADS);
    for (Vec3 p : {a, b, c, d}) {
        // Sample only the existing inrun profile, including its clipped end.
        // These vertex normals are computed once and retained in the cached mesh.
        const double left = std::max(h.inrunStart(), p.x - 2 * h.scale());
        const double right = std::min(0.0, p.x + 2 * h.scale());
        const Vec3 n = Vec3{-(h.inrun(right) - h.inrun(left)) / (right - left), 1, 0}.normalized();
        sjp::visual::normal(n.x, n.y, n.z);
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
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MAX_ANISOTROPY, 4);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MAG_FILTER, sjp::gfx::LINEAR);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_S, sjp::gfx::REPEAT);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_T, sjp::gfx::REPEAT);
    sjp::gfx::pixelStorei(sjp::gfx::UNPACK_ALIGNMENT, oldAlignment);
    sjp::visual::popAttrib();
    return texture;
}
sjp::gfx::Id makeMarkerTexture(std::vector<uint8_t> pixels, int width, int height,
                               sjp::gfx::Enum format = sjp::gfx::ALPHA, bool transparentRim = false) {
    sjp::gfx::Id texture = 0;
    sjp::gfx::genTextures(1, &texture);
    sjp::visual::pushAttrib(sjp::gfx::TEXTURE_BIT);
    int oldAlignment = 4;
    sjp::gfx::getIntegerv(sjp::gfx::UNPACK_ALIGNMENT, &oldAlignment);
    sjp::gfx::pixelStorei(sjp::gfx::UNPACK_ALIGNMENT, 1);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, texture);
    for (int level = 0;; ++level) {
        if (transparentRim) {
            // Clamp-to-edge must see empty snow outside a marking at every
            // mip level. Tiny mips fade out instead of becoming a solid ribbon.
            for (int x = 0; x < width; ++x) {
                pixels[size_t(x)] = 0;
                pixels[size_t((height - 1) * width + x)] = 0;
            }
        }
        sjp::gfx::texImage2D(sjp::gfx::TEXTURE_2D, level, format, width, height, 0,
                           format, sjp::gfx::UNSIGNED_BYTE, pixels.data());
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
constexpr double ledPixelPitch = .025;
sjp::gfx::Id makeLedPixelTexture() {
    constexpr int size = 16;
    std::vector<uint8_t> pixels(size * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double dx = std::abs((x + .5) / size - .5);
            const double dy = std::abs((y + .5) / size - .5);
            const double radius = std::pow(dx * dx * dx * dx + dy * dy * dy * dy, .25);
            const double diode = 1 - smoothRange(.37, .46, radius);
            const double glow = .10 * std::exp(-radius * radius / .16);
            pixels[size_t(y * size + x)] = uint8_t(std::lround(255 *
                (.012 + .988 * std::min(1.0, diode + glow))));
        }
    // One rounded diode per repeat, shared by all panels. Averaged mipmaps
    // turn the physical 25 mm grid into steady color when it is subpixel.
    const auto texture = makeMarkerTexture(std::move(pixels), size, size, sjp::gfx::LUMINANCE);
    sjp::visual::pushAttrib(sjp::gfx::TEXTURE_BIT);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, texture);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_S, sjp::gfx::REPEAT);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_T, sjp::gfx::REPEAT);
    sjp::visual::popAttrib();
    return texture;
}
constexpr double concreteTextureMeters = 2.0;
sjp::gfx::Id makeConcreteTexture() {
    // A two-metre tile gives roughly 8 mm texels: small pores, decimetre
    // weathering and 25 cm formwork courses, independent of terrace width.
    constexpr int size = 256;
    std::vector<uint8_t> pixels(size * size);
    auto noise = [](double u, double v, int columns, int rows, double seed) {
        const double x = u * columns, y = v * rows;
        const int ix = int(std::floor(x)), iy = int(std::floor(y));
        const double tx = x - ix, ty = y - iy;
        const double bx = tx * tx * (3 - 2 * tx), by = ty * ty * (3 - 2 * ty);
        auto cell = [&](int cx, int cy) { return hash(cx % columns + (cy % rows) * 37.13 + seed); };
        // Wrap both axes so world-sized repeats have no dark tile seams.
        return (cell(ix, iy) * (1 - bx) + cell(ix + 1, iy) * bx) * (1 - by) +
               (cell(ix, iy + 1) * (1 - bx) + cell(ix + 1, iy + 1) * bx) * by;
    };
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double u = (x + .5) / size, v = (y + .5) / size;
            const double mottling = (noise(u, v, 6, 4, 211) - .5) * .28 +
                                    (noise(u, v, 16, 12, 317) - .5) * .10;
            const double grain = (hash(x * 13.71 + y * 8.17 + 61) - .5) * .08;
            const double pore = hash(x * 91.3 + y * 37.7 + 9);
            const double pores = pore > .977 ? .15 + .10 * (pore - .977) / .023 : 0;
            const double streak = .18 * smoothRange(.48, .88, noise(u, v, 28, 2, 89)) *
                                  (.35 + .65 * noise(u, v, 8, 3, 157));
            const double board = .025 * (1 - smoothRange(0, 1.3,
                std::abs(std::remainder(y + .5, size / 8.0))));
            pixels[size_t(y * size + x)] = uint8_t(std::lround(255 * clamp(
                .94 + .70 * (mottling + grain - pores - streak - board), 0.0, 1.0)));
        }
    const auto texture = makeMarkerTexture(std::move(pixels), size, size, sjp::gfx::LUMINANCE);
    sjp::visual::pushAttrib(sjp::gfx::TEXTURE_BIT);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, texture);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_S, sjp::gfx::REPEAT);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_T, sjp::gfx::REPEAT);
    sjp::visual::popAttrib();
    return texture;
}
sjp::gfx::Id makeContactShadowTexture() {
    const auto mask = visual::contactShadowMask();
    const auto texture = makeMarkerTexture({mask.begin(), mask.end()}, visual::contactShadowTextureSize,
                                          visual::contactShadowTextureSize);
    sjp::visual::pushAttrib(sjp::gfx::TEXTURE_BIT);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, texture);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_S, sjp::gfx::CLAMP_TO_EDGE);
    sjp::visual::popAttrib();
    return texture;
}
sjp::gfx::Id makeSpruceMarkerTexture() {
    constexpr int width = 512, height = 128;
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
    // A longer tile carries individually angled branches with uneven gaps,
    // rather than repeating the same four silhouettes every metre.
    for (int twig = 0; twig < 10; ++twig) {
        const double seed = twig * 71.3;
        const double x0 = twig * (width / 10.) - 16 + hash(seed + 4) * 14;
        const double y0 = height * .5 + (hash(seed + 1) - .5) * 24;
        const double dx = 42 + hash(seed + 2) * 50, dy = (hash(seed + 3) - .5) * 36;
        const double length = std::hypot(dx, dy), ux = dx / length, uy = dy / length;
        const double opacity = .72 + .23 * hash(seed + 6);
        stroke(x0, y0, x0 + dx, y0 + dy, .85 + hash(seed + 5) * .45, opacity);
        const int count = 11 + int(hash(seed + 7) * 7);
        for (int needle = 0; needle < count; ++needle) {
            const double t = (needle + .5) / count;
            const double x = x0 + dx * t, y = y0 + dy * t;
            for (int side : {-1, 1}) {
                const double reach = (17 + hash(seed + needle * 13 + side) * 25) * (1 - .62 * t);
                const double forward = 5 + hash(seed + needle * 7) * 13;
                stroke(x, y, x + forward * ux - side * reach * uy,
                       y + forward * uy + side * reach * ux,
                       .65 + hash(seed + needle * 17 + side) * .55, opacity);
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        const double edge = smoothRange(0, 8, std::min(y, height - 1 - y));
        for (int x = 0; x < width; ++x)
            pixels[size_t(y * width + x)] = uint8_t(std::lround(pixels[size_t(y * width + x)] * edge));
    }
    return makeMarkerTexture(std::move(pixels), width, height, sjp::gfx::ALPHA, true);
}
sjp::gfx::Id makeSprayMarkerTexture() {
    constexpr int width = 128, height = 64;
    std::vector<uint8_t> pixels(width * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double u = x / double(width), v = (y + .5) / height;
            const double center = .5 + .04 * std::sin(u * 2 * PI) + .018 * std::sin(u * 6 * PI + .7);
            const double radius = .28 + .035 * std::cos(u * 4 * PI) + .025 * std::sin(u * 10 * PI);
            const double edge = smoothRange(0, .17, radius - std::abs(v - center));
            const double grain = smoothRange(.12, .90, hash(x * 13.71 + y * 8.17));
            pixels[y * width + x] = uint8_t(edge * grain * 245);
        }
    return makeMarkerTexture(std::move(pixels), width, height, sjp::gfx::ALPHA, true);
}
void distanceMarkerStrip(const Hill &h, double x, double distance, double width, double extent, Color color, bool foliage = false) {
    const double scale = h.scale(), step = 3 * scale, zStep = 10 * scale;
    const double gridStart = std::min(-170 * scale, h.inrunStart() - 60 * scale);
    const double halfWidth = width * .5 / std::hypot(1.0, h.slope(x));
    const double left = x - halfWidth * 1.7, right = x + halfWidth * 1.7;
    const double hillSeed = h.k * 17.13 + h.kMarkerX * .031;
    const double seed = hillSeed + distance * 31.71, phase = hash(seed) * 11;
    const double repeatLength = foliage ? 3.6 + hash(seed + 7) * 2.0 : 2.1 + hash(seed + 7) * 1.3;
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
        const double endFade = 1 - smoothRange((extent - 1.0) * scale, extent * scale, std::abs(p.z));
        c.a = float(color.a * (.38 + .62 * broad) * gap * (1 - .88 * wear) * endFade);
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
            visual::snowCellTriangles(h, cell, zCell,
                [&](double xx, double z) { return terrainY(h, xx, z); }, [&](Vec3 aa, Vec3 bb, Vec3 cc) {
                visual::clipSnowTriangle(aa, bb, cc, a, b, z, zz, [&](const auto &polygon, size_t count) {
                    sjp::visual::begin(sjp::gfx::TRIANGLE_FAN);

                    for (size_t i = 0; i < count; ++i) {
                        const auto &p = polygon[i];
                        // Vary twig size and edge position smoothly, keeping the
                        // nominal measurement location and receiver mesh intact.
                        const double size = .65 + .7 * smoothNoise(p.z / 2.2, seed + 53);
                        const double drift = (smoothNoise(p.z / 3.1, seed + 67) - .5) * halfWidth * .55;
                        col(appearance(p));
                        const auto normal = terrainSnowNormal(h, p);
                        sjp::visual::normal(normal.x, normal.y, normal.z);
                        sjp::visual::texCoord(p.z / repeatLength + phase + .25 * smoothNoise(p.z / 2.7, seed + 79),
                                             .5 + (p.x - x - drift) / (2 * halfWidth * size));
                        vertex(p);
                    }
                    sjp::visual::end();
                });
            });
            z = zz;
        }
        a = b;
    }
}
void contactShadow(const Hill &h, Vec3 p, double rx, double rz, float opacity) {
    col({.12f, .22f, .31f, opacity});
    sjp::visual::begin(sjp::gfx::TRIANGLES);
    visual::contactShadowTriangles(h, p, rx, rz, [&](double x, double z) { return terrainY(h, x, z); },
                                   [&](Vec3 a, Vec3 b, Vec3 c) {
        for (Vec3 v : {a, b, c}) {
            sjp::visual::texCoord(clamp((v.x - p.x + rx) / (2 * rx), 0.0, 1.0),
                                 clamp((v.z - p.z + rz) / (2 * rz), 0.0, 1.0));
            vertex(v + Vec3{0, .018, 0});
        }
    });
    sjp::visual::end();
}
struct Tree {
    Vec3 base;
    double height = 1, seed = 0;
    Color needles;
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
    // Distinct dark, green, blue-green and warmer evergreen tones per tree.
    // All boughs, including animated tops, share the tint; this cosmetic hash
    // never consumes wind RNG.
    constexpr std::array<Color, 5> palette{{
        {.065f, .175f, .145f, 1}, {.095f, .255f, .195f, 1}, {.105f, .265f, .285f, 1},
        {.170f, .305f, .195f, 1}, {.145f, .340f, .260f, 1}
    }};
    tree.needles = shade(palette[size_t(hash(tree.seed + 83.71) * palette.size())],
                         .94 + .12 * hash(tree.seed + 231.7));
    tree.tiers = 4 + int(hash(i * 11.71) * 3);
    tree.animated = i % 26 == 0;
    return tree;
}
bool visibleTree(const Hill &h, const Tree &tree) {
    const double s = h.scale();
    return !(tree.base.x > 74 * s && tree.base.x < 242 * s && std::abs(tree.base.z) < std::max(47 * s, 18.0));
}
void branch(const visual::TreeBoughGeometry &geometry, double seed, Color needles) {
    constexpr int count = 7;
    const Vec3 tip = geometry.tip;
    std::array<Vec3, count + 1> bases{}, snow{};
    for (int i = 0; i < count; ++i) {
        bases[i] = geometry.base[size_t(i)];
        const double fraction = .28 + hash(seed + i * 8.12) * .28;
        snow[i] = bases[i] + (tip - bases[i]) * fraction;
    }
    bases[count] = bases[0];
    snow[count] = snow[0];
    const Color cap{.82f, .9f, .94f, 1};
    for (int i = 0; i < count; ++i) {
        quad(bases[i], snow[i], snow[i + 1], bases[i + 1], shade(needles, .94 + hash(seed + i * 4.39) * .12));
        triangle(snow[i], tip, snow[i + 1], shade(cap, .96 + hash(seed + i * 9.13) * .05));
    }
}
void treeBranch(const Tree &tree, int tier, Vec3 sway = {}) {
    branch(visual::treeTierGeometry(tree.base, tree.height, tree.seed, tree.tiers, tier, sway),
           tree.seed + tier * 29.7, tree.needles);
}
void terraceSlab(const visual::CameraSurface &snow, double x0, double x1, double z0, double z1,
                 double top, Color c) {
    const Vec3 a{x0, top, z0}, b{x0, top, z1}, d{x1, top, z0}, e{x1, top, z1};
    const Vec3 aa{x0, snow.height(x0, z0) - .16, z0}, bb{x0, snow.height(x0, z1) - .16, z1},
               dd{x1, snow.height(x1, z0) - .16, z0}, ee{x1, snow.height(x1, z1) - .16, z1};
    const double bottom = std::min({aa.y, bb.y, dd.y, ee.y});
    const Color tint = shade(c, .98 + .04 * hash(x0 * 1.13 + z0 * .79 + 17));
    auto face = [&](Vec3 p, Vec3 q, Vec3 r, Vec3 s, double brightness) {
        const Vec3 normal = cross(q - p, r - p).normalized();
        sjp::visual::begin(sjp::gfx::QUADS);
        sjp::visual::normal(normal.x, normal.y, normal.z);
        for (Vec3 v : {p, q, r, s}) {
            const double dry = clamp((v.y - bottom) / (top - bottom), 0.0, 1.0);
            col(shade(tint, brightness * (.94 + .06 * dry)));
            const double u = (std::abs(normal.x) > .5 ? v.z : v.x) / concreteTextureMeters;
            const double vv = (std::abs(normal.y) > .5 ? v.z : v.y) / concreteTextureMeters;
            sjp::visual::texCoord(u, vv);
            vertex(v);
        }
        sjp::visual::end();
    };
    face(a, b, e, d, 1);
    face(aa, dd, ee, bb, 1);
    face(aa, a, d, dd, .88);
    face(bb, ee, e, b, .88);
    face(aa, bb, b, a, .94);
    face(dd, d, e, ee, .94);
}
void standGuard(Vec3 a, Vec3 b, visual::SnowAccumulation &snow) {
    const Color post{.44f, .54f, .58f, 1}, rail{.55f, .64f, .68f, 1};
    beam(a, a + Vec3{0, 1.12, 0}, .028, post);
    beam(b, b + Vec3{0, 1.12, 0}, .028, post);
    beam(a + Vec3{0, 1.08, 0}, b + Vec3{0, 1.08, 0}, .033, rail);
    snow.caps(a + Vec3{0, 1.11, 0}, b + Vec3{0, 1.11, 0}, .15, .18, a.x * 1.3 + a.z * 4.7);
    beam(a + Vec3{0, .55, 0}, b + Vec3{0, .55, 0}, .020, post);
}
void standEndGuard(const visual::StandTerrace &t, double x, int side, double near, visual::SnowAccumulation &snow) {
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
        snow.caps(feet[i - 1] + Vec3{0, 1.11, 0}, feet[i] + Vec3{0, 1.11, 0},
                  .15, .18, x * 1.3 + i * 4.7 + side);
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
sjp::gfx::Id makeLedGlowTexture() {
    // Two softly blurred word masks. Each board needs only one extra quad;
    // no per-diode geometry, framebuffer blur or additional scene lights.
    constexpr int width = 512, rowHeight = 64, height = rowHeight * 2, glyphScale = 4;
    std::vector<double> pixels(width * height), temporary(width * height);
    const std::array<std::string, 2> labels{{"NORDIC", "THE PERFECT V"}};
    for (size_t label = 0; label < labels.size(); ++label) {
        const auto &text = labels[label];
        const int left = (width - int(text.size() * 6 - 1) * glyphScale) / 2;
        const int top = int(label) * rowHeight + (rowHeight - 7 * glyphScale) / 2;
        for (size_t letter = 0; letter < text.size(); ++letter) {
            const auto mask = glyph(text[letter]);
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 5; ++x)
                    if (mask[y] & (1 << (4 - x)))
                        for (int dy = 0; dy < glyphScale; ++dy)
                            for (int dx = 0; dx < glyphScale; ++dx) {
                                const int xx = left + (int(letter) * 6 + x) * glyphScale + dx;
                                const int yy = top + y * glyphScale + dy;
                                // Match the geometry lettering's 88% filled cells.
                                pixels[size_t(yy * width + xx)] =
                                    std::min(1.0, glyphScale * .88 - dx) *
                                    std::min(1.0, glyphScale * .88 - dy);
                            }
        }
    }
    constexpr int radius = 12;
    std::array<double, radius * 2 + 1> weights{};
    double total = 0;
    for (int i = -radius; i <= radius; ++i)
        total += weights[size_t(i + radius)] = std::exp(-i * i / 32.0);
    for (auto &weight : weights)
        weight /= total;
    for (bool horizontal : {true, false}) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                double value = 0;
                for (int i = -radius; i <= radius; ++i) {
                    const int xx = x + (horizontal ? i : 0), yy = y + (horizontal ? 0 : i);
                    if (xx >= 0 && xx < width && yy / rowHeight == y / rowHeight && yy >= 0 && yy < height)
                        value += pixels[size_t(yy * width + xx)] * weights[size_t(i + radius)];
                }
                temporary[size_t(y * width + x)] = value;
            }
        pixels.swap(temporary);
    }
    std::vector<uint8_t> mask(pixels.size());
    for (size_t i = 0; i < mask.size(); ++i)
        mask[i] = uint8_t(std::lround(255 * pixels[i]));
    const auto texture = makeMarkerTexture(std::move(mask), width, height);
    sjp::visual::pushAttrib(sjp::gfx::TEXTURE_BIT);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, texture);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_WRAP_S, sjp::gfx::CLAMP_TO_EDGE);
    sjp::visual::popAttrib();
    return texture;
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
void texturedBoardFace(const BoardFrame &frame, double left, double right, double bottom, double top,
                       double z, int side, Color color, const std::array<double, 4> &bounds) {
    const Vec3 a = frame.point(left, bottom, z), b = frame.point(right, bottom, z),
               c = frame.point(right, top, z), d = frame.point(left, top, z);
    const Vec3 normal = cross(frame.along, frame.up) * -side;
    col(color);
    sjp::visual::begin(sjp::gfx::QUADS);
    sjp::visual::normal(normal.x, normal.y, normal.z);
    const std::array<Vec3, 4> positions{{a, b, c, d}};
    const std::array<std::array<double, 2>, 4> uv{{
        {bounds[0], bounds[2]}, {bounds[1], bounds[2]},
        {bounds[1], bounds[3]}, {bounds[0], bounds[3]}}};
    for (int i : side > 0 ? std::array<int, 4>{{3, 2, 1, 0}} : std::array<int, 4>{{0, 1, 2, 3}}) {
        sjp::visual::texCoord(uv[size_t(i)][0], uv[size_t(i)][1]);
        vertex(positions[size_t(i)]);
    }
    sjp::visual::end();
}
void boardFace(const BoardFrame &frame, double left, double right, double bottom, double top,
               double z, int side, Color color) {
    // All colored fields and glyphs share the same grid in board-local metres.
    texturedBoardFace(frame, left, right, bottom, top, z, side, color,
                      {left / ledPixelPitch, right / ledPixelPitch, bottom / ledPixelPitch, top / ledPixelPitch});
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
struct LedBoard {
    BoardFrame frame;
    double width, zFront;
    int side;
    Color accent;
    std::string text;
};
void ledBoard(const LedBoard &board) {
    const auto &[frame, width, zFront, side, accent, text] = board;
    const double left = -width * .5, right = width * .5;
    boardFace(frame, left, right, .16, 1.79, zFront, side, {.10f, .28f, .33f, 1});
    const double accentLeft = side > 0 ? right - width * .1025 : left + width * .0175;
    const double accentRight = side > 0 ? right - width * .0175 : left + width * .1025;
    boardFace(frame, accentLeft, accentRight, .29, 1.61, zFront - side * .025, side, shade(accent, 2.1));
    boardText(frame, text, -side * width * .025, .97, width * .78, .74, zFront - side * .035, side,
              {1.65f, 1.75f, 1.72f, 1});
}
void ledBoardGlow(const LedBoard &board) {
    const auto &[frame, width, zFront, side, accent, text] = board;
    const double cells = double(text.size() * 6 - 1);
    const double cell = std::min(.74 / 7, width * .78 / cells);
    const double center = -side * width * .025, halfWidth = (cells * .5 + 4) * cell;
    const double u0 = .5 - (cells * 2 + 16) / 512, u1 = 1 - u0;
    const int row = text == "NORDIC" ? 0 : 1;
    // The padded mask follows the front face, so the board, snow and people
    // occlude it normally. Keep the rear panel dark and the diode gaps visible.
    texturedBoardFace(frame, center - halfWidth, center + halfWidth, .97 - 7.5 * cell, .97 + 7.5 * cell,
                      zFront - side * .04, side, {1, .98f, .93f, .42f},
                      {side > 0 ? u1 : u0, side > 0 ? u0 : u1,
                       (row * 64 + 62) / 128.0, (row * 64 + 2) / 128.0});
}
LedBoard advertisement(const Hill &h, double x, double width, double front, int side, Color accent,
                       const std::string &text, visual::SnowAccumulation &snow) {
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
    snow.caps(frame.point(left, 1.82, front), frame.point(right, 1.82, front),
              .27, .24, x * 1.3 + front * 4.7, 1.8);
    for (int face : {-1, 1}) {
        const double z = front + face * .15;
        snow.bank(frame.point(left, .60, z), frame.point(right, .60, z), {0,0,double(face)},
                  .72, .46, x + front * 2 + face,
                  [&](double xx, double zz) { return terrainY(h, xx, zz); });
    }
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
    boardFace(frame, left, right, .16, 1.79, zBack, -side, {.20f, .32f, .37f, 1});
    for (double end : {left, right})
        quad(frame.point(end, .16, zFront), frame.point(end, 1.79, zFront),
             frame.point(end, 1.79, zBack), frame.point(end, .16, zBack), {.42f, .54f, .58f, 1});
    for (int face : {side, -side}) {
        const double z = face == side ? zFront : zBack;
        boardFace(frame, left, right, 1.745, 1.82, z - face * .02, face, {.73f, .82f, .84f, 1});
    }
    for (double offset : {-.39, .39}) {
        const double z = zBack + side * .12;
        const Vec3 foot = frame.point(offset * width, 0, z);
        beam({foot.x, terrainY(h, foot.x, z) - .035, z}, frame.point(offset * width, 1.78, z), .04,
             {.43f, .54f, .58f, 1});
        beam(frame.point(offset * width, .80, z), frame.point(offset * width, .80, zBack), .035,
             {.43f, .54f, .58f, 1});
    }
    // Only the face towards the jumping hill carries sponsor graphics.
    return {frame, width, zFront, side, accent, text};
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
void floodlight(const Hill &h, double x, double z, double size, visual::SnowAccumulation &snow) {
    const double y = terrainY(h, x, z), height = 18 * size + 2;
    // Double all foundation dimensions, keeping the original top at y + .60.
    // The added depth extends downward to y - .60, into the snow.
    box({x, y, z}, {2.2, 1.20, 2.2}, {.48f, .54f, .56f, 1});
    snow.footing(h, x, z, 1.1, 1.1, y + .60, x + z * 7);
    beam({x, y + .3, z}, {x, y + height, z}, .13, {.40f, .48f, .53f, 1});
    beam({x, y + height - 1, z}, {x + 1.4, y + height, z}, .08, {.36f, .43f, .48f, 1});
    box({x, y + height, z}, {3.4, 1.25, .60}, {.12f, .20f, .25f, 1});
    // Size a substantial, asymmetric drift to the broad floodlight housing.
    const double seed = x*2.3+z*5.7;
    const double snowLength = 2.3+.9*visual::SnowAccumulation::variation(seed+1.3);
    const double snowLeft = x-1.7+(3.4-snowLength)*visual::SnowAccumulation::variation(seed+7.1);
    snow.cap({snowLeft,y+height+.625,z}, {snowLeft+snowLength,y+height+.625,z},
             .34+.14*visual::SnowAccumulation::variation(seed+13.7),
             .44+.22*visual::SnowAccumulation::variation(seed+21.9),seed);
    const double frontSide = z > 0 ? -1 : 1;
    for (int lamp = 0; lamp < 6; ++lamp) {
        const double xx = x + (lamp % 3 - 1) * 1.02, yy = y + height + (lamp / 3 - .5) * .49;
        // Shallow lamp frames protrude only from the hill-facing surface.
        // Their rear ends stay inside the housing's uninterrupted back plate.
        box({xx, yy, z + frontSide * .295}, {.82, .38, .08}, {.51f, .59f, .63f, 1});
        // Self-lit warm glass; the vertex shader raises emission with the
        // current lamp strength without rebuilding these cached faces.
        const float emission[]{.28f, .27f, .22f, 1};
        sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::EMISSION, emission);
        const double front = z + frontSide * .345;
        quad({xx - .35, yy - .125, front}, {xx + .35, yy - .125, front}, {xx + .35, yy + .125, front},
             {xx - .35, yy + .125, front}, {.94f, .91f, .78f, 1});
        const float noEmission[]{0, 0, 0, 1};
        sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::EMISSION, noEmission);
    }
}
} // namespace

void Renderer::installTerrain() {
    const auto prepared = preparingLandscape.get();
    const size_t index = preparingVenue;
    landscapes[index] = gfx::installList(prepared);
    landscapeBytes[index] = gfx::listBytes(landscapes[index]);
    landscapeUse[index] = ++landscapeClock;
    size_t bytes = 0;
    for (size_t n : landscapeBytes)
        bytes += n;
    while (bytes > landscapeBudget) {
        size_t oldest = landscapes.size();
        for (size_t i = 0; i < landscapes.size(); ++i)
            if (i != index && landscapes[i] &&
                (oldest == landscapes.size() || landscapeUse[i] < landscapeUse[oldest]))
                oldest = i;
        if (oldest == landscapes.size())
            break; // A single venue may exceed the budget; never discard the active one.
        gfx::deleteLists(landscapes[oldest], 1);
        landscapes[oldest] = 0;
        bytes -= landscapeBytes[oldest];
        landscapeBytes[oldest] = 0;
    }
}
void Renderer::waitForScene() {
    if (preparingLandscape.valid())
        installTerrain();
    for (size_t i = 0; i < hills().size(); ++i)
        if (hills()[i].id == sceneHill && landscapes[i]) {
            // An older job may finish first after rapid selection changes.
            // Only explicit captures/tests wait; normal frames remain asynchronous.
            while (!prepareSceneryShadows(hills()[i], i))
                installSceneryShadows();
            sceneryShadowsReady = true;
            break;
        }
}
void Renderer::releaseSceneryShadow(SceneryShadow &entry) {
    if (entry.mesh)
        gfx::deleteLists(entry.mesh, 1);
    for (auto texture : entry.textures)
        if (texture)
            gfx::deleteTextures(1, &texture);
    entry = {};
}
void Renderer::installSceneryShadows() {
    auto prepared = preparingSceneryShadow.get();
    auto &entry = sceneryShadowMeshes[preparingShadowVenue][size_t(preparingShadowVariant)];
    releaseSceneryShadow(entry);
    entry.mesh = gfx::installList(std::move(prepared));
    entry.textures = preparingShadowTextures;
    preparingShadowTextures = {};
    entry.field = std::move(preparingShadowField);
    entry.bytes = gfx::listBytes(entry.mesh) + entry.field->textureBytes;
    entry.use = ++sceneryShadowClock;
    // One bounded cache across venues and sun directions. Least recently
    // drawn entries go first; the entry just installed is always retained.
    for (;;) {
        size_t total = 0;
        SceneryShadow *oldest = nullptr;
        for (auto &venue : sceneryShadowMeshes)
            for (auto &item : venue) {
                total += item.bytes;
                if (item.mesh && &item != &entry && (!oldest || item.use < oldest->use))
                    oldest = &item;
            }
        if (total <= 24 * 1024 * 1024 || !oldest)
            break;
        releaseSceneryShadow(*oldest);
    }
}
const Renderer::SceneryShadow *Renderer::activeSceneryShadow(size_t venue) const {
    const int variant = int(sceneEnvironment.time);
    if (!sceneryShadows || !visual::bakedSunShadows(lighting) || variant < 0 || variant >= sunShadowVariants)
        return nullptr;
    const auto &entry = sceneryShadowMeshes[venue][size_t(variant)];
    return entry.mesh ? &entry : nullptr;
}
bool Renderer::prepareSceneryShadows(const Hill &, size_t venue) {
    if (preparingSceneryShadow.valid() &&
        preparingSceneryShadow.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        installSceneryShadows();
    const int variant = int(sceneEnvironment.time);
    if (!sceneryShadows || !visual::bakedSunShadows(lighting) || variant < 0 || variant >= sunShadowVariants)
        return true;
    auto &entry = sceneryShadowMeshes[venue][size_t(variant)];
    if (entry.mesh) {
        entry.use = ++sceneryShadowClock;
        return true;
    }
    if (!preparingSceneryShadow.valid()) {
        preparingShadowVenue = venue;
        preparingShadowVariant = variant;
        gfx::genTextures(2, preparingShadowTextures.data());
        preparingShadowField = std::make_shared<visual::SunShadowField>();
        auto *field = preparingShadowField.get();
        const auto textures = preparingShadowTextures;
        const auto light = lighting;
        const auto *hill = &hills()[venue];
        preparingSceneryShadow = gfx::prepareList([this, hill, venue, light, textures, field] {
            buildSceneryShadows(*hill, venue, light, textures, *field);
        });
        if (preparingSceneryShadow.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            installSceneryShadows();
            return true;
        }
    }
    return false;
}
void Renderer::buildSceneryShadows(const Hill &h, size_t venue, visual::EnvironmentLight light,
                                   std::array<gfx::Id, 2> textures, visual::SunShadowField &field) {
    visual::StaticBatch batch;
    std::vector<visual::SceneryShadowTriangle> receivers;
    visual::pushAttrib(gfx::ENABLE_BIT | gfx::CURRENT_BIT | gfx::TEXTURE_BIT |
                       gfx::COLOR_BUFFER_BIT | gfx::DEPTH_BUFFER_BIT | gfx::POLYGON_BIT);
    visual::disable(gfx::LIGHTING);
    visual::disable(gfx::CULL_FACE);
    visual::enable(gfx::BLEND);
    visual::blendFunc(gfx::SRC_ALPHA, gfx::ONE_MINUS_SRC_ALPHA);
    visual::depthMask(false);
    visual::enable(gfx::POLYGON_OFFSET_FILL);
    // Constant depth units plus 1 mm of ground clearance avoid rounding
    // artifacts without dragging shadows through the bank with a slope bias.
    visual::polygonOffset(0, -2);
    const double scale = h.scale(), strength = visual::sunShadowOpacity(light);
    const Color tint{visual::shadowColor[0], visual::shadowColor[1], visual::shadowColor[2], 1};
    // The horizon is the snow alone. The elevated inrun is an open structure
    // and casts its own deck/pier shadow below; as a height field it would
    // also block the sunlight that passes underneath the deck.
    field.horizon = std::make_unique<visual::TerrainSunHorizon>(
        Vec3{visual::snowGridStart(h),0,-200*scale}, Vec3{std::max(420*scale,h.end()+80*scale),0,200*scale},
        light.direction,.5*std::sqrt(scale),[&](double x,double z) { return visual::terrainSnowHeight(h,x,z); });
    const auto &horizon = *field.horizon;
    const auto exposure = [&](Vec3 p) { return visual::scenerySunExposure(h, p, light.direction); };
    auto footprint = [&](const std::vector<Vec3> &sources, double opacity = 1) {
        const auto litSources = horizon.sunlitCaster(sources);
        if (litSources.size()<3) return;
        visual::SceneryShadowVolume volume(litSources,light.direction,200*std::sqrt(h.scale()));
        if (volume.planes.empty()) return;
        // Only first snow hits of sun rays from the caster can be lit shadow:
        // anything beyond lies in the ridge's own shadow. Bound the receiver
        // search by those hits instead of the volume's full 200 m reach.
        Vec3 low = litSources.front(), high = low;
        for (const auto &p : litSources) {
            const auto hit = visual::projectToSnow(h, p, light.direction*-1);
            for (const auto &q : {p, hit ? *hit : p-light.direction.normalized()*(200*std::sqrt(h.scale()))}) {
                low.x = std::min(low.x,q.x); low.z = std::min(low.z,q.z);
                high.x = std::max(high.x,q.x); high.z = std::max(high.z,q.z);
            }
        }
        const double margin = 2 * std::sqrt(h.scale());
        volume.low.x = std::max(volume.low.x, low.x-margin); volume.low.z = std::max(volume.low.z, low.z-margin);
        volume.high.x = std::min(volume.high.x, high.x+margin); volume.high.z = std::min(volume.high.z, high.z+margin);
        volume.receivers(h,[&](Vec3 a,Vec3 b,Vec3 c) {
            visual::SceneryShadowTriangle triangle{{a,b,c},{}};
            for (size_t i=0;i<3;++i)
                triangle.opacity[i]=strength*opacity*exposure(triangle.points[i])*horizon.visibility(triangle.points[i]);
            if (*std::max_element(triangle.opacity.begin(),triangle.opacity.end())>.001)
                receivers.push_back(triangle);
        });
    };
    // Every caster shares the one filtered atlas, so a whole mast or pier has
    // the same soft edge. Poles narrower than an atlas cell are widened to a
    // full cell; thinner strips break into faint dashes when rasterized.
    const double atlasSpacing = .40*std::sqrt(h.scale());
    const Vec3 sideways = cross(light.direction, {0,1,0}).normalized();
    struct Pole { Vec3 base, top; double radius; };
    std::vector<Pole> poles;
    auto pole = [&](Vec3 base, Vec3 top, double radius) { poles.push_back({base, top, radius}); };
    for (int i = 0; i < 420; ++i) {
        const auto tree = treeAt(h,i);
        if (!visibleTree(h,tree)) continue;
        const auto trunk = visual::treeTrunkGeometry(tree.base,tree.height);
        footprint({trunk.begin(),trunk.end()},.85);
        for (int tier=0;tier<tree.tiers;++tier) {
            const auto bough = visual::treeTierGeometry(tree.base,tree.height,tree.seed,tree.tiers,tier);
            std::vector<Vec3> points(bough.base.begin(),bough.base.end());
            points.push_back(bough.tip);
            footprint(points,.85);
        }
    }
    const auto &stands = grandstandLayouts[venue];
    for (int side : {-1,1}) {
        for (int column = 0; column < visual::floodlightColumns(h); ++column) {
            const auto mast = visual::floodlightMast(h,column,side);
            pole(mast.base, mast.lamp-Vec3{0,.625,0}, .13);
            std::vector<Vec3> head;
            for (int x : {-1,1}) for (int y : {-1,1}) for (int z : {-1,1})
                head.push_back(mast.lamp+Vec3{x*1.7,y*.625,z*.30});
            footprint(head);
        }
        for (int section = 0; section < visual::GrandstandLayout::sectionCount; ++section) {
            const auto &stand = stands.section(side,section);
            for (const auto &t : stand.terraces) {
                std::vector<Vec3> slab;
                for (double x : {t.x0,t.x1})
                    for (int back : {0,1}) {
                        const double z = side*(stands.near+(back?4.95:1.35));
                        slab.push_back({x,visual::terrainSnowHeight(h,x,z),z});
                        slab.push_back({x,t.floor(back?2:0)+.25,z});
                    }
                footprint(slab, .80);
            }
            // The sponsor board in front of each section (1.6 m panel).
            const double front = side*(stands.near-1.0);
            std::vector<Vec3> board;
            for (double x : {stand.center-stand.width*.5, stand.center+stand.width*.5})
                for (double z : {front-side*.15, front+side*.15}) {
                    const double ground = visual::terrainSnowHeight(h,x,z);
                    board.push_back({x,ground+.16,z});
                    board.push_back({x,ground+1.82,z});
                }
            footprint(board, .90);
        }
    }
    // The elevated inrun deck with its service walkway, in short straight
    // pieces along the curved profile. Exposed piers are thin poles.
    for (double x = h.inrunStart(); x < 0;) {
        const double xx = std::min(0.0, x + 4 * std::sqrt(scale));
        std::vector<Vec3> deck;
        for (double px : {x,xx})
            for (double z : {double(visual::inrunServiceSide)*visual::inrunWalkOuter, 2.3})
                for (double y : {-.62, .06})
                    deck.push_back({px,h.inrun(px)+y,z});
        footprint(deck);
        x = xx;
    }
    for (const auto &support : inrunLayouts[venue].supports)
        if (support.exposed())
            for (int side : {-1,1}) {
                const double z = side*visual::inrunColumnZ;
                pole({support.x,support.footingTop,z},{support.x,support.deck,z},
                     clamp(.12+support.height()*.003,.12,.32));
            }
    std::unique_ptr<visual::SceneryShadowMask> mask;
    double spacing=atlasSpacing;
    const size_t solidReceivers = receivers.size();
    for (;;) {
        // Pole width follows the final cell size, so a coarser atlas on a
        // large hill does not turn the pole into a fainter partial cell.
        receivers.resize(solidReceivers);
        for (const auto &p : poles) {
            const double half = std::max(p.radius, .5*spacing);
            footprint({p.base-sideways*half, p.base+sideways*half, p.top+sideways*half, p.top-sideways*half});
        }
        mask=std::make_unique<visual::SceneryShadowMask>(spacing);
        bool complete=true;
        for (const auto &triangle:receivers)
            if (!mask->rasterize(triangle)) { complete=false; break; }
        if (complete) break;
        spacing*=2; // Keep the per-venue mask bounded on large flying hills.
    }
    const auto atlas=mask->atlas();
    visual::bindTexture(gfx::TEXTURE_2D,textures[0]);
    gfx::pixelStorei(gfx::UNPACK_ALIGNMENT,1);
    gfx::texImage2D(gfx::TEXTURE_2D,0,gfx::ALPHA,atlas.width,atlas.height,0,gfx::ALPHA,gfx::UNSIGNED_BYTE,atlas.pixels.data());
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_MIN_FILTER,gfx::LINEAR);
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_MAG_FILTER,gfx::LINEAR);
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_WRAP_S,gfx::CLAMP_TO_EDGE);
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_WRAP_T,gfx::CLAMP_TO_EDGE);
    // Ridges shade the snow behind them, consistently with the casters they hide.
    field.terrain = std::make_unique<visual::TerrainShadowGrid>(h,horizon,strength,exposure,2*std::sqrt(scale));
    const auto &ground = *field.terrain;
    std::vector<uint8_t> groundPixels(ground.opacity.size());
    for (size_t i = 0; i < groundPixels.size(); ++i)
        groundPixels[i] = uint8_t(std::lround(255 * clamp(double(ground.opacity[i]),0.,1.)));
    visual::bindTexture(gfx::TEXTURE_2D,textures[1]);
    gfx::texImage2D(gfx::TEXTURE_2D,0,gfx::ALPHA,ground.columns,ground.rows,0,gfx::ALPHA,gfx::UNSIGNED_BYTE,groundPixels.data());
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_MIN_FILTER,gfx::LINEAR);
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_MAG_FILTER,gfx::LINEAR);
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_WRAP_S,gfx::CLAMP_TO_EDGE);
    gfx::texParameteri(gfx::TEXTURE_2D,gfx::TEXTURE_WRAP_T,gfx::CLAMP_TO_EDGE);
    field.textureBytes = (size_t(atlas.width)*atlas.height + groundPixels.size())*4;

    visual::enable(gfx::TEXTURE_2D);
    visual::bindTexture(gfx::TEXTURE_2D,textures[0]);
    col(tint);
    for (size_t index=0;index<atlas.keys.size();++index) {
        const auto key=atlas.keys[index];
        const double left=key.second*visual::SceneryShadowMask::core*spacing;
        const double front=key.first*visual::SceneryShadowMask::core*spacing;
        const double size=visual::SceneryShadowMask::core*spacing;
        const double u=double(index%atlas.columns)*visual::SceneryShadowMask::stride+visual::SceneryShadowMask::gutter;
        const double v=double(index/atlas.columns)*visual::SceneryShadowMask::stride+visual::SceneryShadowMask::gutter;
        visual::begin(gfx::TRIANGLES);
        visual::snowDecalTriangles(h,{{left,0,front},{left+size,0,front},{left+size,0,front+size},{left,0,front+size}},
                                  [&](Vec3 a,Vec3 b,Vec3 c) {
            for (auto p:{a,b,c}) {
                visual::texCoord((u+(p.x-left)/spacing)/atlas.width,(v+(p.z-front)/spacing)/atlas.height);
                vertex(p+Vec3{0,.001,0});
            }
        });
        visual::end();
        visual::flush();
    }
    // Terrain shadow: only snow cells that actually receive some of it.
    visual::bindTexture(gfx::TEXTURE_2D,textures[1]);
    const double dx = 3*scale, dz = 10*scale, gridStart = visual::snowGridStart(h);
    const double gridEnd = std::max(420*scale,h.end()+80*scale);
    for (double x = gridStart; x < gridEnd; x += dx) {
        visual::begin(gfx::TRIANGLES);
        for (double z = -200*scale; z < 200*scale; z += dz) {
            if (ground.maximum(x,x+dx,z,z+dz) < .5/255) continue;
            visual::snowCellTriangles(h,x,z,[&](double xx,double zz){return visual::backdropGround(h,xx,zz);},
                                      [&](Vec3 a,Vec3 b,Vec3 c) {
                for (auto p:{a,b,c}) {
                    visual::texCoord((p.x-ground.x0)/(ground.columns*ground.step),
                                     (p.z-ground.z0)/(ground.rows*ground.step));
                    vertex(p+Vec3{0,.001,0});
                }
            });
        }
        visual::end();
        visual::flush();
    }
    // The same filtered textures also shade the ground-connected snow.
    // Project each raised vertex along sunlight onto the atlas receiver,
    // then split UVs at tile boundaries without lifting the original surface.
    // Clipped vertices need a few constant depth units for float rounding on
    // steep close-ups. A slope bias would pull the buried skirt into view.
    visual::polygonOffset(0, -8);
    using BankTriangle = std::array<visual::SnowShadowVertex,3>;
    std::vector<BankTriangle> banks;
    std::map<visual::SceneryShadowMask::Key,size_t> tileIndices;
    for (size_t i=0;i<atlas.keys.size();++i) tileIndices[atlas.keys[i]]=i;
    std::map<visual::SceneryShadowMask::Key,std::vector<BankTriangle>> bankTiles;
    const double tileSize=visual::SceneryShadowMask::core*spacing;
    for (const auto &bank:snowBankReceivers[venue]) {
        BankTriangle triangle;
        for (size_t i=0;i<3;++i)
            triangle[i]=visual::snowShadowVertex(h,bank[i],bank[i+3],light.direction);
        if (std::all_of(triangle.begin(),triangle.end(),[](const auto &v){return v.weight==0;})) continue;
        banks.push_back(triangle);
        const double left=std::min({triangle[0].receiver.x,triangle[1].receiver.x,triangle[2].receiver.x});
        const double right=std::max({triangle[0].receiver.x,triangle[1].receiver.x,triangle[2].receiver.x});
        const double front=std::min({triangle[0].receiver.z,triangle[1].receiver.z,triangle[2].receiver.z});
        const double back=std::max({triangle[0].receiver.z,triangle[1].receiver.z,triangle[2].receiver.z});
        for (int z=int(std::floor(front/tileSize));z<=int(std::floor(back/tileSize));++z)
            for (int x=int(std::floor(left/tileSize));x<=int(std::floor(right/tileSize));++x) {
                const visual::SceneryShadowMask::Key key{z,x};
                if (!tileIndices.count(key)) continue;
                visual::snowShadowTile(triangle,x*tileSize,(x+1)*tileSize,z*tileSize,(z+1)*tileSize,
                                       [&](auto a,auto b,auto c) {bankTiles[key].push_back({a,b,c});});
            }
    }
    visual::bindTexture(gfx::TEXTURE_2D,textures[0]);
    for (const auto &[key,triangles]:bankTiles) {
        const size_t index=tileIndices.at(key);
        const double u=double(index%atlas.columns)*visual::SceneryShadowMask::stride+visual::SceneryShadowMask::gutter;
        const double v=double(index/atlas.columns)*visual::SceneryShadowMask::stride+visual::SceneryShadowMask::gutter;
        visual::begin(gfx::TRIANGLES);
        for (const auto &triangle:triangles) for (const auto &p:triangle) {
            col({tint.r,tint.g,tint.b,float(p.weight)});
            visual::texCoord((u+(p.receiver.x-key.second*tileSize)/spacing)/atlas.width,
                             (v+(p.receiver.z-key.first*tileSize)/spacing)/atlas.height);
            vertex(p.position);
        }
        visual::end();
        visual::flush();
    }
    visual::bindTexture(gfx::TEXTURE_2D,textures[1]);
    visual::begin(gfx::TRIANGLES);
    for (const auto &triangle:banks) {
        const double left=std::min({triangle[0].receiver.x,triangle[1].receiver.x,triangle[2].receiver.x});
        const double right=std::max({triangle[0].receiver.x,triangle[1].receiver.x,triangle[2].receiver.x});
        const double front=std::min({triangle[0].receiver.z,triangle[1].receiver.z,triangle[2].receiver.z});
        const double back=std::max({triangle[0].receiver.z,triangle[1].receiver.z,triangle[2].receiver.z});
        if (ground.maximum(left,right,front,back)<.5/255) continue;
        for (const auto &p:triangle) {
            col({tint.r,tint.g,tint.b,float(p.weight)});
            visual::texCoord((p.receiver.x-ground.x0)/(ground.columns*ground.step),
                             (p.receiver.z-ground.z0)/(ground.rows*ground.step));
            vertex(p.position);
        }
    }
    visual::end();
    field.scenery = std::move(mask);
    batch.finish();
    visual::popAttrib();
}

bool Renderer::prepareTerrain(const Hill &h) {
    size_t index = 0;
    for (size_t i = 0; i < hills().size(); ++i)
        if (h.id == hills()[i].id)
            index = i;
    if (preparingLandscape.valid() &&
        preparingLandscape.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        installTerrain();
    if (landscapes[index]) {
        landscapeUse[index] = ++landscapeClock;
        return true;
    }
    // Only one bounded preparation job exists. Rapid selection changes never
    // join/cancel a worker on the UI thread or queue all 32 venues.
    if (preparingLandscape.valid())
        return false;
    if (!snowTexture)
        snowTexture = makeSnowTexture(true);
    if (!naturalSnowTexture)
        naturalSnowTexture = makeSnowTexture(false);
    if (!spruceMarkerTexture)
        spruceMarkerTexture = makeSpruceMarkerTexture();
    if (!sprayMarkerTexture)
        sprayMarkerTexture = makeSprayMarkerTexture();
    if (!contactShadowTexture)
        contactShadowTexture = makeContactShadowTexture();
    if (!concreteTexture)
        concreteTexture = makeConcreteTexture();
    if (!ledPixelTexture)
        ledPixelTexture = makeLedPixelTexture();
    if (!ledGlowTexture)
        ledGlowTexture = makeLedGlowTexture();
    preparingVenue = index;
    const Hill *hill = &hills()[index];
    preparingLandscape = gfx::prepareList([this, hill, index] {
        if (!venueInitialized[index]) {
            chasePaths[index].build(*hill);
            inrunLayouts[index].build(*hill);
            grandstandLayouts[index].build(*hill);
            venueInitialized[index] = true;
        }
        buildTerrain(*hill, index);
    });
    if (preparingLandscape.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        installTerrain();
        return true;
    }
    return false;
}
void Renderer::terrain(const Hill &h) {
    for (size_t i = 0; i < hills().size(); ++i)
        if (h.id == hills()[i].id) {
            gfx::callList(landscapes[i]);
            return;
        }
}
void Renderer::buildTerrain(const Hill &h, size_t index) {
    const double s = h.scale();
    visual::StaticBatch staticBatch;
    visual::SnowAccumulation accumulatedSnow;
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::LIGHTING_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::TEXTURE_BIT | sjp::gfx::POLYGON_BIT);
    const float matte[]{.025f, .035f, .045f, 1};
    sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SPECULAR, matte);
    sjp::visual::materialf(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SHININESS, 8);
    sjp::visual::materialf(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SNOW_GLITTER, 1);
    sjp::visual::enable(sjp::gfx::TEXTURE_2D);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, naturalSnowTexture);
    sjp::visual::texEnvi(sjp::gfx::TEXTURE_ENV, sjp::gfx::TEXTURE_ENV_MODE, sjp::gfx::MODULATE);
    const float edge[]{float(visual::valleyEdgeStart * s), float(visual::valleyEdgeEnd * s), 1, float(visual::valleyBackStart(h))};
    const float noEdge[]{0, 0, 0, 0};
    sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::VALLEY_EDGE, edge);
    // A conservative interior: every groomed receiver vertex surrounding it
    // has alpha 1, including the start/tail fade and stitched mesh rows.
    const auto coverage = visual::opaquePreparedCoverage(h);
    sjp::visual::materialfv(gfx::FRONT_AND_BACK,gfx::SNOW_COVERAGE,coverage.data());
    // Fixed fine ski corridor, stitched to the unchanged coarse valley. Both
    // snow layers use the exact same triangles and original Hill samples.
    for (double x = std::min(-170 * s, h.inrunStart() - 60 * s); x < std::max(420 * s, h.end() + 80 * s);
         x += 3 * s)
        for (double z = -200 * s; z < 200 * s; z += 10 * s)
            snowPatch(h, x, z);
    sjp::visual::materialfv(sjp::gfx::FRONT_AND_BACK, sjp::gfx::VALLEY_EDGE, noEdge);
    sjp::visual::materialfv(gfx::FRONT_AND_BACK,gfx::SNOW_COVERAGE,noEdge);
    // Reuse the exact receiver triangles for a finite, softly blended prepared
    // strip. It also supplies depth where its opaque interior replaces the
    // redundant natural layer. Receiver geometry and edge blending stay intact.
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::TEXTURE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::POLYGON_BIT);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, snowTexture);
    sjp::visual::enable(sjp::gfx::BLEND);
    sjp::visual::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::visual::depthMask(sjp::gfx::TRUE);
    sjp::visual::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    sjp::visual::polygonOffset(0, -1);
    for (double x = std::min(-170 * s, h.inrunStart() - 60 * s);
         x < std::min(std::max(420 * s, h.end() + 80 * s), h.outrunBottom() + 47); x += 3 * s)
        for (double z = -200 * s; z < 200 * s; z += 10 * s)
            snowPatch(h, x, z, true);
    sjp::visual::popAttrib();
    sjp::visual::materialf(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SNOW_GLITTER, 0);
    sjp::visual::disable(sjp::gfx::TEXTURE_2D);
    // Distant photographic mountains are drawn with the sky, behind this
    // valley and its descending decorative continuation.
    // All soft ground contacts share one state pass, instead of switching
    // depth/blend/lighting hundreds of times between individual trees.
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::POLYGON_BIT | sjp::gfx::TEXTURE_BIT);
    sjp::visual::disable(sjp::gfx::LIGHTING);
    sjp::visual::enable(sjp::gfx::TEXTURE_2D);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, contactShadowTexture);
    sjp::visual::enable(sjp::gfx::BLEND);
    sjp::visual::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::visual::depthMask(sjp::gfx::FALSE);
    sjp::visual::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    // Prepared snow writes depth at one bias layer; decals sit one above it.
    sjp::visual::polygonOffset(0, -2);
    for (int i = 0; i < 420; ++i) {
        const Tree tree = treeAt(h, i);
        if (visibleTree(h, tree)) {
            contactShadow(h, tree.base, tree.height * .61, tree.height * .48, .20f);
            visual::flush();
        }
    }
    for (const auto &support : inrunLayouts[index].supports) {
        if (!support.exposed())
            continue;
        for (int side : {-1, 1})
            contactShadow(h, {support.x, 0, side * visual::inrunColumnZ}, 1.15, 1.05, .24f);
        visual::flush();
    }
    for (int side : {-1, 1}) {
        const double near = std::max(25 * s, 8.0), front = side * (near - 1.0);
        for (int section = 0; section < 14; ++section) {
            const double x = (79 + section * 6.2) * s;
            contactShadow(h, {x, 0, front + side * 3}, 5.8 * s * .76, 4.3, .18f);
        }
        for (int mast = 0; mast < visual::floodlightColumns(h); ++mast) {
            const auto fixture = visual::floodlightMast(h, mast, side);
            contactShadow(h, fixture.base, 1.5, 1.5, .19f);
        }
    }
    sjp::visual::popAttrib();
    for (int i = 0; i < 420; ++i) {
        const Tree tree = treeAt(h, i);
        if (!visibleTree(h, tree))
            continue;
        box(visual::treeTrunkCenter(tree.base,tree.height), visual::treeTrunkSize(tree.height),
            {.28f, .25f, .22f, 1});
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
        const double xx = std::min(0.0, x + 2 * s);
        sjp::visual::enable(sjp::gfx::TEXTURE_2D);
        sjp::visual::materialf(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SNOW_GLITTER, 1);
        for (int i = 0; i < visual::snowSubdivisions; ++i) {
            const double a = x + i * visual::inrunSnowStep(h);
            if (a >= xx)
                break;
            const double b = std::min(xx, a + visual::inrunSnowStep(h));
            inrunSnowQuad(h, {a, h.inrun(a), -2.3}, {a, h.inrun(a), 2.3},
                          {b, h.inrun(b), 2.3}, {b, h.inrun(b), -2.3}, {.92f, .96f, .97f, 1});
        }
        sjp::visual::materialf(sjp::gfx::FRONT_AND_BACK, sjp::gfx::SNOW_GLITTER, 0);
        sjp::visual::disable(sjp::gfx::TEXTURE_2D);
        const double cellX = x, cellEnd = xx;
        for (int i = 0; i < visual::snowSubdivisions; ++i) {
            const double x = cellX + i * visual::inrunSnowStep(h);
            if (x >= cellEnd)
                break;
            const double xx = std::min(cellEnd, x + visual::inrunSnowStep(h));
            const double y = h.inrun(x), yy = h.inrun(xx);
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
                // The muted table trim stops at both ski grooves, which remain
                // continuous to the edge. Keep the original snow profile intact.
                const double edgeX = std::max(x, -.16);
                const double edgeY = y + (yy - y) * (edgeX - x) / (xx - x);
                if (xx - edgeX > 1e-8)
                    for (const auto &span : std::array<std::pair<double,double>,3>{{
                             {-2.3,-.22-.055}, {-.22+.055,.22-.055}, {.22+.055,2.3}}})
                        quad({edgeX, edgeY + .018, span.first}, {edgeX, edgeY + .018, span.second},
                             {xx, yy + .018, span.second}, {xx, yy + .018, span.first}, {.46f, .55f, .59f, 1});
            }
        }
    }
    inrunStructure(h, inrunLayouts[index], accumulatedSnow);
    startSignalHousing(h);
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
    sjp::visual::enable(sjp::gfx::LIGHTING);
    sjp::visual::enable(sjp::gfx::TEXTURE_2D);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, spruceMarkerTexture);
    sjp::visual::enable(sjp::gfx::BLEND);
    sjp::visual::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::visual::depthMask(sjp::gfx::FALSE);
    sjp::visual::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    // Coplanar clipped markings need slope bias at grazing camera angles;
    // constant units alone flicker against the prepared snow's depth.
    sjp::visual::polygonOffset(-2, -2);
    for (int d = 10; d <= h.k + 50; d += 10)
        if (d != h.k)
            distanceMarkerStrip(h, h.atDistance(d), d, .52, 18, {.23f, .30f, .24f, .76f}, true);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, sprayMarkerTexture);
    if (h.k % 10 == 0)
        distanceMarkerStrip(h, h.atDistance(h.k) + .09, h.k, .48, 18, {.76f, .38f, .36f, .72f});
    distanceMarkerStrip(h, h.kMarkerX + .2, h.k, .75, 20, {.79f, .36f, .34f, .76f});
    sjp::visual::popAttrib();
    // Keep the original K-point X/Z coordinates. Upright signs clear the
    // uphill snow across their full width, and their posts embed in the mesh.
    {
        const double x = h.kMarkerX;
        constexpr double width = 1.60, height = .74, depth = .16;
        const visual::CameraSurface snow(h);
        const Color c{.50f, .28f, .26f, 1};
        for (int side : {-1, 1}) {
            const double z = side * 20 * s, y = snow.height(x, z);
            double highestSnow = y;
            for (double xx : {x - width * .5, x, x + width * .5})
                for (double zz : {z - depth * .5, z, z + depth * .5})
                    highestSnow = std::max(highestSnow, snow.height(xx, zz));
            const double panelY = std::max(y + .90, highestSnow + .25 + height * .5);
            const double postTop = panelY + height * .5 + .12;
            beam({x, y - .30, z}, {x, postTop, z}, .08, c);
            box({x, panelY, z}, {width, height, depth}, {.68f, .31f, .29f, 1});
            accumulatedSnow.caps({x-width*.5, panelY+height*.5, z}, {x+width*.5, panelY+height*.5, z},
                                 .18, .20, x+z*3, 1.6);
            accumulatedSnow.footing(h, x, z, .10, .10, y+.30, x+z*7);
            signText("K" + std::to_string(h.k), {x, panelY + .01, z - side * (depth * .5 + .006)}, 1.38, .43,
                     side, {.98f, .97f, .92f, 1});
            box({x, postTop, z}, {.24, .20, .24}, {.94f, .95f, .90f, 1});
        }
    }
    // Terraced concrete, warm timber seating, railings, sponsored panels and
    // winter spectators give the existing arena scale and a sporting identity.
    const auto &stands = grandstandLayouts[index];
    const visual::CameraSurface arenaSnow(h);
    const double arenaNear = stands.near;
    std::vector<LedBoard> ledBoards;
    ledBoards.reserve(visual::GrandstandLayout::sectionCount * 2);
    // One opaque material group for all concrete, rather than texture switches
    // around every timber seat. Coordinates and triangle counts stay identical.
    sjp::visual::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::TEXTURE_BIT | sjp::gfx::CURRENT_BIT);
    sjp::visual::enable(sjp::gfx::TEXTURE_2D);
    sjp::visual::bindTexture(sjp::gfx::TEXTURE_2D, concreteTexture);
    for (int side : {-1, 1})
        for (int section = 0; section < visual::GrandstandLayout::sectionCount; ++section) {
            for (const auto &t : stands.section(side, section).terraces)
                for (int row = 0; row < visual::GrandstandLayout::rowCount; ++row) {
                    const double za = side * (arenaNear + 1.35 + row * 1.2), zb = za + side * 1.2;
                    terraceSlab(arenaSnow, t.x0, t.x1, std::min(za, zb), std::max(za, zb), t.floor(row),
                                {.43f, .49f, .52f, 1});
                }
            visual::flush();
        }
    sjp::visual::popAttrib();
    for (int side : {-1, 1}) {
        const double front = side * (arenaNear - 1.0);
        for (int section = 0; section < visual::GrandstandLayout::sectionCount; ++section) {
            const auto &stand = stands.section(side, section);
            const double x = stand.center, width = stand.width;
            const Color accent = section % 3 == 0 ? Color{.86f, .37f, .25f, 1} : Color{.25f, .68f, .65f, 1};
            ledBoards.push_back(advertisement(h, x, width, front, side, accent,
                                             section % 3 == 0 ? "NORDIC" : "THE PERFECT V", accumulatedSnow));
            for (int edge : {0,1}) {
                const double z = side * (arenaNear + (edge ? 4.95 : 1.35));
                const auto &first = stand.terraces.front(), &last = stand.terraces.back();
                accumulatedSnow.standBank({first.x0,0,z}, {last.x1,0,z}, {0,0,double(side*(edge?1:-1))},
                                         x+z*3,
                                         [&](double xx,double zz) { return visual::terrainSnowHeight(h,xx,zz); },
                                         [&](double xx,double) {
                                             // Cover a whole mesh cell beside each floor step.
                                             return std::min({stand.at(xx-1).floor(edge?2:0),
                                                              stand.at(xx).floor(edge?2:0),
                                                              stand.at(xx+1).floor(edge?2:0)})-.08;
                                         });
            }
            for (size_t bay = 0; bay < stand.terraces.size(); ++bay) {
                const auto &t = stand.terraces[bay];
                for (int end : {-1,1}) {
                    if ((end<0 && bay) || (end>0 && bay+1!=stand.terraces.size())) continue;
                    for (int row = 0; row < visual::GrandstandLayout::rowCount; ++row) {
                        const double xx = end<0 ? t.x0 : t.x1, top = t.floor(row) - .08;
                        const double za = side * (arenaNear + 1.35 + row * 1.2), zb = za + side * 1.2;
                        accumulatedSnow.standBank({xx,0,za}, {xx,0,zb}, {double(end),0,0}, xx+za*3,
                                                 [&](double a,double b) { return visual::terrainSnowHeight(h,a,b); },
                                                 [&](double,double) { return top; });
                    }
                }
                const auto *left = bay ? &stand.terraces[bay - 1] :
                    (section ? &stands.section(side, section - 1).terraces.back() : nullptr);
                const auto *right = bay + 1 < stand.terraces.size() ? &stand.terraces[bay + 1] :
                    (section + 1 < visual::GrandstandLayout::sectionCount ?
                     &stands.section(side, section + 1).terraces.front() : nullptr);
                for (int row = 0; row < visual::GrandstandLayout::rowCount; ++row) {
                    const double za = side * (arenaNear + 1.35 + row * 1.2), zb = za + side * 1.2;
                    const double z0 = std::min(za, zb), z1 = std::max(za, zb), floor = t.floor(row);
                    box({(t.x0 + t.x1) * .5, floor + .09, (z0 + z1) * .5},
                        {t.x1 - t.x0 - .16, .08, .60}, {.40f, .32f, .26f, 1});
                }
                // Lower tiers remain open; substantial drops get one shared
                // edge railing instead of separate barriers around each row.
                if (!left || t.base - left->base > 1.0)
                    standEndGuard(t, t.x0 + .06, side, arenaNear, accumulatedSnow);
                if (!right || t.base - right->base > 1.0)
                    standEndGuard(t, t.x1 - .06, side, arenaNear, accumulatedSnow);
                const double back = side * (arenaNear + 4.90);
                standGuard({t.x0 + .08, t.floor(2), back}, {t.x1 - .08, t.floor(2), back}, accumulatedSnow);
                const double lip = side * (arenaNear + 1.39);
                const double frontDrop = t.floor(0) - std::min(arenaSnow.height(t.x0, lip),
                                                             arenaSnow.height(t.x1, lip));
                if (frontDrop > 1.15)
                    standGuard({t.x0 + .08, t.floor(0), lip}, {t.x1 - .08, t.floor(0), lip}, accumulatedSnow);
            }
            for (int row = 0; row < visual::GrandstandLayout::rowCount; ++row)
                for (int person = 0; person < 4; ++person)
                    spectatorBody(spectatorAt(stands, side, section, row, person));
        }
        for (int mast = 0; mast < visual::floodlightColumns(h); ++mast) {
            const auto fixture = visual::floodlightMast(h, mast, side);
            floodlight(h, fixture.base.x, fixture.base.z, fixture.size, accumulatedSnow);
        }
    }
    // All LED fronts share one opaque material group in the cached venue.
    // The unlit colors are constant emission, with fog but no lighting work,
    // new lights or shadow passes. Frames and rear panels remain normally lit.
    visual::pushAttrib(gfx::ENABLE_BIT | gfx::TEXTURE_BIT | gfx::CURRENT_BIT);
    visual::enable(gfx::TEXTURE_2D);
    visual::bindTexture(gfx::TEXTURE_2D, ledPixelTexture);
    visual::disable(gfx::LIGHTING);
    for (const auto &board : ledBoards)
        ledBoard(board);
    visual::popAttrib();
    // One opaque snow group, compiled into the venue's existing cached mesh.
    // Reuse terrain UVs/albedo. Blend rim normals into the original terrain;
    // buried edges then join without a bright outline or an alpha-blended pass.
    visual::enable(gfx::TEXTURE_2D);
    visual::bindTexture(gfx::TEXTURE_2D, naturalSnowTexture);
    visual::disable(gfx::BLEND);
    // Written once by the first build of this venue, before any shadow job
    // for it can start; later rebuilds produce identical banks and skip this.
    if (!snowBanksRecorded[index]) {
        for (size_t i:accumulatedSnow.groundTriangles) {
            const auto &triangle=accumulatedSnow.triangles[i];
            std::array<Vec3,6> receiver;
            for (size_t j=0;j<3;++j) {
                const auto &v=triangle[j];
                const double elevation=v.position.y-visual::terrainSnowHeight(h,v.position.x,v.position.z);
                const double raised=smoothRange(.015,.14,elevation);
                receiver[j]=v.position;
                receiver[j+3]=(terrainSnowNormal(h,v.position)*(1-raised)+v.normal*raised).normalized();
            }
            visual::visibleSnowBank(h,receiver,[&](const auto &visible) {
                snowBankReceivers[index].push_back(visible);
            });
        }
        snowBanksRecorded[index] = true;
    }
    visual::begin(gfx::TRIANGLES);
    for (const auto &triangle : accumulatedSnow.triangles)
        for (const auto &v : triangle) {
            const Color c = snowColor(h, v.position.x, v.position.z);
            const double elevation = v.position.y - visual::terrainSnowHeight(h,v.position.x,v.position.z);
            const double raised = smoothRange(.015,.14,elevation);
            const Vec3 normal = (terrainSnowNormal(h,v.position)*(1-raised)+v.normal*raised).normalized();
            col(c);
            visual::normal(normal.x, normal.y, normal.z);
            visual::texCoord(v.position.x/13.,v.position.z/13.);
            vertex(v.position);
        }
    visual::end();
    // A cached, local halo only around each word: 56 triangles for the arena.
    // Alpha blending and depth testing keep this independent of scene lights.
    visual::pushAttrib(gfx::ENABLE_BIT | gfx::TEXTURE_BIT | gfx::CURRENT_BIT |
                       gfx::DEPTH_BUFFER_BIT | gfx::COLOR_BUFFER_BIT);
    visual::enable(gfx::TEXTURE_2D);
    visual::bindTexture(gfx::TEXTURE_2D, ledGlowTexture);
    visual::disable(gfx::LIGHTING);
    visual::enable(gfx::BLEND);
    visual::blendFunc(gfx::SRC_ALPHA, gfx::ONE_MINUS_SRC_ALPHA);
    visual::depthMask(false);
    for (const auto &board : ledBoards)
        ledBoardGlow(board);
    visual::popAttrib();
    staticBatch.finish();
    // Finish writes current attributes, so restore the caller's state last.
    sjp::visual::popAttrib();
}

void Renderer::hillRecordMarker(const Hill &h) {
    const double distance = hillRecordDistance;
    if (!std::isfinite(distance) || distance <= 0 || distance > 1000) {
        if (recordMarkerMesh) {
            gfx::deleteLists(recordMarkerMesh, 1);
            recordMarkerMesh = 0;
        }
        return;
    }
    if (recordMarkerMesh && recordMarkerHill == h.id && cachedRecordDistance == distance) {
        gfx::callList(recordMarkerMesh);
        return;
    }
    if (recordMarkerMesh)
        gfx::deleteLists(recordMarkerMesh, 1);
    recordMarkerHill = h.id;
    cachedRecordDistance = distance;
    // Only this tiny mesh changes when a human improves the record; the hill
    // cache, snow profile and original distance integration remain intact.
    recordMarkerMesh = gfx::genLists(1);
    gfx::newList(recordMarkerMesh, gfx::COMPILE_AND_EXECUTE);
    visual::StaticBatch batch;
    visual::pushAttrib(gfx::ENABLE_BIT | gfx::TEXTURE_BIT | gfx::DEPTH_BUFFER_BIT | gfx::COLOR_BUFFER_BIT |
                       gfx::POLYGON_BIT | gfx::CURRENT_BIT);
    visual::enable(gfx::LIGHTING);
    visual::enable(gfx::TEXTURE_2D);
    visual::bindTexture(gfx::TEXTURE_2D, sprayMarkerTexture);
    visual::enable(gfx::BLEND);
    visual::blendFunc(gfx::SRC_ALPHA, gfx::ONE_MINUS_SRC_ALPHA);
    visual::depthMask(gfx::FALSE);
    visual::enable(gfx::POLYGON_OFFSET_FILL);
    visual::polygonOffset(-2, -2);
    distanceMarkerStrip(h, h.atDistance(distance), distance, .85, 20, {.25f, .41f, .65f, .82f});
    batch.finish();
    visual::popAttrib();
    gfx::endList();
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
