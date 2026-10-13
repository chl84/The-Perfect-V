#include "renderer.h"
#include "presentation_timing.h"
#include "original_skeleton.h"
#include "visual_weather.h"
#include "visual_shadow.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace sjp {
namespace {
void col(Color c) {
    sjp::gfx::color4f(c.r, c.g, c.b, c.a);
}
void vertex(Vec3 p) {
    sjp::gfx::vertex3d(p.x, p.y, p.z);
}
void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    col(color);
    Vec3 n = cross(b - a, c - a).normalized();
    sjp::gfx::begin(sjp::gfx::QUADS);
    sjp::gfx::normal3d(n.x, n.y, n.z);
    vertex(a);
    vertex(b);
    vertex(c);
    vertex(d);
    sjp::gfx::end();
}
void beam(Vec3 a, Vec3 b, double r, Color c) {
    Vec3 dir = (b - a).normalized(), right = cross(dir, {0, 0, 1}).normalized() * r,
         up = cross(right, dir).normalized() * r;
    quad(a + right + up, b + right + up, b - right + up, a - right + up, c);
    quad(a - right - up, b - right - up, b + right - up, a + right - up, c);
    quad(a - right + up, b - right + up, b - right - up, a - right - up, c);
    quad(a + right - up, b + right - up, b + right + up, a + right + up, c);
}
void sphere(Vec3 p, double r, Color c) {
    col(c);
    for (int i = 0; i < 8; i++) {
        double a = -PI / 2 + i * PI / 8, b = a + PI / 8;
        sjp::gfx::begin(sjp::gfx::QUAD_STRIP);
        for (int j = 0; j <= 12; j++)
            for (double k : {a, b}) {
                double t = j * 2 * PI / 12;
                Vec3 n{std::cos(k) * std::cos(t), std::sin(k), std::cos(k) * std::sin(t)};
                sjp::gfx::normal3d(n.x, n.y, n.z);
                vertex(p + n * r);
            }
        sjp::gfx::end();
    }
}
// A small open, tapered cloth tube. Its tail follows the same displayed angle
// as the HUD arrow; only gentle sag and flutter change the drawn geometry.
void windsock(Vec3 origin, const Frame &frame, double time) {
    constexpr int sections = 6, sides = 8;
    for (int section = 0; section < sections; ++section) {
        const Vec3 axis = (visual::windsockPoint(frame, time, (section + 1) / double(sections)) -
                           visual::windsockPoint(frame, time, section / double(sections))).normalized();
        const Vec3 up = cross(axis, {0, 0, 1}).normalized(), side = cross(up, axis).normalized();
        col(section % 2 ? Color{.94f, .93f, .87f, 1} : colors::orange);
        sjp::gfx::begin(sjp::gfx::QUAD_STRIP);
        for (int corner = 0; corner <= sides; ++corner) {
            const double angle = corner * 2 * PI / sides;
            const Vec3 normal = side * std::cos(angle) + up * std::sin(angle);
            sjp::gfx::normal3d(normal.x, normal.y, normal.z);
            for (int end : {section, section + 1}) {
                const double fraction = end / double(sections);
                const double radius = .23 - fraction * .16;
                vertex(origin + visual::windsockPoint(frame, time, fraction) + normal * radius);
            }
        }
        sjp::gfx::end();
    }
}
// A sky gradient supplies the atmospheric backdrop without moving the camera.
void sky(int width, int height, const visual::EnvironmentLight &light, Vec3 eye, Vec3 target) {
    sjp::gfx::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::CURRENT_BIT |
                         sjp::gfx::COLOR_BUFFER_BIT);
    sjp::gfx::disable(sjp::gfx::DEPTH_TEST);
    sjp::gfx::depthMask(sjp::gfx::FALSE);
    sjp::gfx::disable(sjp::gfx::LIGHTING);
    sjp::gfx::disable(sjp::gfx::FOG);
    sjp::gfx::disable(sjp::gfx::TEXTURE_2D);
    sjp::gfx::matrixMode(sjp::gfx::PROJECTION);
    sjp::gfx::pushMatrix();
    sjp::gfx::loadIdentity();
    sjp::gfx::ortho(0, width, 0, height, -1, 1);
    sjp::gfx::matrixMode(sjp::gfx::MODELVIEW);
    sjp::gfx::pushMatrix();
    sjp::gfx::loadIdentity();
    sjp::gfx::begin(sjp::gfx::QUAD_STRIP);
    for (int band = 0; band <= 24; ++band) {
        const float t = band / 24.f;
        const float smooth = t * t * (3 - 2 * t);
        const Vec3 color = visual::mix(light.skyHorizon, light.skyTop, smooth);
        col({float(color.x), float(color.y), float(color.z), 1});
        sjp::gfx::vertex2f(0, height * t);
        sjp::gfx::vertex2f(width, height * t);
    }
    sjp::gfx::end();
    const Vec3 forward = (target - eye).normalized(), right = cross(forward, {0, 1, 0}).normalized(),
               up = cross(right, forward).normalized();
    auto screen = [&](Vec3 direction, double &x, double &y) {
        const double depth = dot(direction, forward), halfFov = std::tan(48 * PI / 360);
        if (depth <= .01)
            return false;
        x = width * .5 + dot(direction, right) / (depth * halfFov) * height * .5;
        y = height * .5 + dot(direction, up) / (depth * halfFov) * height * .5;
        return x > -20 && x < width + 20 && y > -20 && y < height + 20;
    };
    sjp::gfx::enable(sjp::gfx::BLEND);
    sjp::gfx::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    if (light.celestialVisibility > 0) {
        double x, y;
        if (screen(light.direction, x, y)) {
            const double radius = height * .0055;
            const Color color = light.moon ? Color{.83f, .89f, .98f, 1} : Color{1, .91f, .69f, 1};
            sjp::gfx::begin(sjp::gfx::TRIANGLE_FAN);
            col({color.r, color.g, color.b, .12f});
            sjp::gfx::vertex2f(float(x), float(y));
            col({color.r, color.g, color.b, 0});
            for (int i = 0; i <= 24; ++i)
                sjp::gfx::vertex2f(float(x + std::cos(i * 2 * PI / 24) * radius * 5),
                                  float(y + std::sin(i * 2 * PI / 24) * radius * 5));
            sjp::gfx::end();
            sjp::gfx::begin(sjp::gfx::TRIANGLE_FAN);
            col(color);
            sjp::gfx::vertex2f(float(x), float(y));
            for (int i = 0; i <= 24; ++i)
                sjp::gfx::vertex2f(float(x + std::cos(i * 2 * PI / 24) * radius),
                                  float(y + std::sin(i * 2 * PI / 24) * radius));
            sjp::gfx::end();
        }
        if (light.moon) {
            sjp::gfx::begin(sjp::gfx::QUADS);
            for (uint32_t i = 0; i < 48; ++i) {
                const double azimuth = (environment::hash(i + 701) / double(UINT32_MAX)) * 2 * PI;
                const double altitude = .2 + (environment::hash(i + 1701) / double(UINT32_MAX)) * 1.2;
                const Vec3 star{std::cos(azimuth) * std::cos(altitude), std::sin(altitude),
                                std::sin(azimuth) * std::cos(altitude)};
                if (!screen(star, x, y))
                    continue;
                col({.75f, .82f, .94f, .58f});
                const float r = .6f + float(i % 3) * .2f;
                for (const auto &p : std::array<std::array<float, 2>, 4>{{{-r, -r}, {r, -r}, {r, r}, {-r, r}}})
                    sjp::gfx::vertex2f(float(x) + p[0], float(y) + p[1]);
            }
            sjp::gfx::end();
        }
    }
    sjp::gfx::popMatrix();
    sjp::gfx::matrixMode(sjp::gfx::PROJECTION);
    sjp::gfx::popMatrix();
    sjp::gfx::matrixMode(sjp::gfx::MODELVIEW);
    sjp::gfx::popAttrib();
}
void jumperShadow(const Hill &h, const Frame &f, const visual::EnvironmentLight &light) {
    const bool onInrun = f.x <= 0;
    const auto surface = [&](double x) { return onInrun ? h.inrun(x) : h.ground(x); };
    const double clearance = std::max(0.0, f.y - surface(f.x));
    const double slope = onInrun ? h.inrunSlope(f.x) : h.slope(f.x);
    const Vec3 cast{-light.direction.x / light.direction.y, 0, -light.direction.z / light.direction.y};
    const double facing = dot(Vec3{-slope, 1, 0}.normalized(), light.direction);
    if (facing <= .06)
        return;
    const double softness = (.025 + std::min(.18, clearance * .007)) * light.shadowSoftness;
    const float visibility = float(std::exp(-clearance / 85));
    // Use exactly the same displayed joints as the athlete. Shadow projection
    // is presentation only; neither this pose nor the origin is changed.
    auto pose = f.pose;
    if (pose[18] == 0 && pose[20] == 0) {
        OriginalPose fallback;
        fallback.reset(std::atan(onInrun ? h.inrunSlope(f.x) : h.slope(f.x)));
        std::copy(fallback.angles.begin(), fallback.angles.end(), pose.begin());
        std::copy(fallback.offsets.begin(), fallback.offsets.end(), pose.begin() + 18);
    }
    const auto skeleton =
        f.hasRenderSkeleton ? OriginalSkeleton{f.renderSkeleton} : originalSkeleton(pose, f.leadingLeg);
    visual::ShadowMask silhouette;
    silhouette.build(visual::shadowSilhouette(skeleton, cast, clearance, slope), softness);
    const visual::CameraSurface receiver(h);
    sjp::gfx::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::POLYGON_BIT | sjp::gfx::COLOR_BUFFER_BIT);
    sjp::gfx::disable(sjp::gfx::LIGHTING);
    sjp::gfx::disable(sjp::gfx::TEXTURE_2D);
    sjp::gfx::enable(sjp::gfx::BLEND);
    sjp::gfx::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::gfx::depthMask(sjp::gfx::FALSE);
    sjp::gfx::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    sjp::gfx::polygonOffset(-1, -1);
    const bool central = onInrun ||
                         std::max(std::abs(silhouette.z),
                                  std::abs(silhouette.z + (visual::ShadowMask::rows - 1) * silhouette.dz)) <=
                             19 * h.scale();
    std::array<double, visual::ShadowMask::columns> xs{}, ground{}, ramp{};
    for (int column = 0; column < visual::ShadowMask::columns; ++column) {
        xs[size_t(column)] = f.x + silhouette.x + column * silhouette.dx;
        if (onInrun)
            xs[size_t(column)] = std::min(0.0, xs[size_t(column)]);
        if (central) {
            ground[size_t(column)] = receiver.height(xs[size_t(column)], 2.31);
            ramp[size_t(column)] = receiver.height(xs[size_t(column)], 0);
        }
    }
    const auto shadowVertex = [&](int column, int row) {
        const double x = xs[size_t(column)];
        double z = silhouette.z + row * silhouette.dz;
        if (onInrun)
            z = clamp(z, -2.25, 2.25);
        const float alpha = silhouette.coverage[size_t(row * visual::ShadowMask::columns + column)];
        col({.09f, .15f, .20f, alpha * visibility * float(light.shadowStrength * std::min(1.0, facing * 4))});
        const double y = central ? (std::abs(z) <= 2.3 ? ramp[size_t(column)] : ground[size_t(column)])
                                 : receiver.height(x, z);
        sjp::gfx::vertex3d(x, y + .012, z);
    };
    // Each receiver cell is drawn once. Body/limb/ski overlap has already been
    // resolved in the mask; no oval or second contact shadow is drawn.
    sjp::gfx::begin(sjp::gfx::QUADS);
    for (int row = 0; row < visual::ShadowMask::rows - 1; ++row)
        for (int column = 0; column < visual::ShadowMask::columns - 1; ++column) {
            const size_t at = size_t(row * visual::ShadowMask::columns + column);
            if (std::max({silhouette.coverage[at], silhouette.coverage[at + 1],
                          silhouette.coverage[at + visual::ShadowMask::columns],
                          silhouette.coverage[at + visual::ShadowMask::columns + 1]}) * visibility < .004f)
                continue;
            shadowVertex(column, row);
            shadowVertex(column, row + 1);
            shadowVertex(column + 1, row + 1);
            shadowVertex(column + 1, row);
        }
    sjp::gfx::end();
    sjp::gfx::popAttrib();
}
void lookAt(Vec3 eye, Vec3 center) {
    Vec3 f = (center - eye).normalized(), s = cross(f, {0, 1, 0}).normalized(), u = cross(s, f);
    float m[] = {float(s.x), float(u.x), float(-f.x), 0, float(s.y), float(u.y), float(-f.y), 0,
                   float(s.z), float(u.z), float(-f.z), 0, 0,          0,          0,           1};
    sjp::gfx::multMatrixf(m);
    sjp::gfx::translated(-eye.x, -eye.y, -eye.z);
}
} // namespace
Renderer::Font Renderer::loadFont(const std::string &path) {
    FT_Library library;
    FT_Face face;
    if (FT_Init_FreeType(&library))
        throw std::runtime_error("FreeType initialization failed");
    if (FT_New_Face(library, path.c_str(), 0, &face)) {
        FT_Done_FreeType(library);
        throw std::runtime_error("Could not load font: " + path);
    }
    FT_Set_Pixel_Sizes(face, 0, 64);
    Font f;
    constexpr int atlas = 1024;
    std::vector<unsigned char> pixels(atlas * atlas, 0);
    int x = 1, y = 1, row = 0;
    for (int i = 32; i < 127; i++) {
        if (FT_Load_Char(face, i, FT_LOAD_RENDER))
            continue;
        auto g = face->glyph;
        int w = g->bitmap.width, h = g->bitmap.rows;
        if (x + w + 1 >= atlas) {
            x = 1;
            y += row + 2;
            row = 0;
        }
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
                pixels[(y + yy) * atlas + x + xx] = g->bitmap.buffer[yy * g->bitmap.pitch + xx];
        f.glyphs[i] = {
            float(x) / atlas,     float(y) / atlas,        float(w), float(h), float(g->bitmap_left),
            float(g->bitmap_top), float(g->advance.x) / 64};
        x += w + 2;
        row = std::max(row, h);
    }
    sjp::gfx::genTextures(1, &f.tex);
    sjp::gfx::bindTexture(sjp::gfx::TEXTURE_2D, f.tex);
    sjp::gfx::pixelStorei(sjp::gfx::UNPACK_ALIGNMENT, 1);
    sjp::gfx::texImage2D(sjp::gfx::TEXTURE_2D, 0, sjp::gfx::ALPHA, atlas, atlas, 0, sjp::gfx::ALPHA, sjp::gfx::UNSIGNED_BYTE, pixels.data());
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MIN_FILTER, sjp::gfx::LINEAR);
    sjp::gfx::texParameteri(sjp::gfx::TEXTURE_2D, sjp::gfx::TEXTURE_MAG_FILTER, sjp::gfx::LINEAR);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return f;
}
Renderer::Renderer(SDL_Window *w, const std::string &assets, bool noMsaa, bool vsync, bool validation)
    : window(w) {
    for (size_t i = 0; i < chasePaths.size(); ++i)
        chasePaths[i].build(hills()[i]);
    sjp::gfx::initialize(window, noMsaa, vsync, validation);
    try {
        regular = loadFont(assets + "/fonts/Interim-Regular.ttf");
        boldFont = loadFont(assets + "/fonts/Interim-Bold.ttf");
        const auto *d = sjp::gfx::getString(sjp::gfx::RENDERER);
        device = d ? reinterpret_cast<const char *>(d) : "Unknown Vulkan device";
        sjp::gfx::enable(sjp::gfx::DEPTH_TEST);
        sjp::gfx::enable(sjp::gfx::NORMALIZE);
        sjp::gfx::shadeModel(sjp::gfx::SMOOTH);
        sjp::gfx::hint(sjp::gfx::FOG_HINT, sjp::gfx::NICEST);
        sjp::gfx::enable(sjp::gfx::COLOR_MATERIAL);
        sjp::gfx::colorMaterial(sjp::gfx::FRONT_AND_BACK, sjp::gfx::AMBIENT_AND_DIFFUSE);
        sjp::gfx::enable(sjp::gfx::BLEND);
        sjp::gfx::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    } catch (...) {
        sjp::gfx::shutdown();
        throw;
    }
}
Renderer::~Renderer() {
    for (auto l : landscapes)
        if (l)
            sjp::gfx::deleteLists(l, 1);
    sjp::gfx::deleteTextures(1, &regular.tex);
    sjp::gfx::deleteTextures(1, &boldFont.tex);
    if (snowTexture)
        sjp::gfx::deleteTextures(1, &snowTexture);
    if (naturalSnowTexture)
        sjp::gfx::deleteTextures(1, &naturalSnowTexture);
    if (spruceMarkerTexture)
        sjp::gfx::deleteTextures(1, &spruceMarkerTexture);
    if (sprayMarkerTexture)
        sjp::gfx::deleteTextures(1, &sprayMarkerTexture);
    sjp::gfx::shutdown();
}
void Renderer::begin(const Hill &h, const Frame &f, double time, int camera, bool panorama, bool ghost,
                     const Frame *gf) {
    SDL_GetWindowSizeInPixels(window, &width, &height);
    width = std::max(1, width);
    height = std::max(1, height);
    sjp::gfx::beginFrame(width, height);
    sjp::gfx::viewport(0, 0, width, height);
    sjp::gfx::clearColor(float(lighting.fog.x), float(lighting.fog.y), float(lighting.fog.z), 1);
    sjp::gfx::clear(sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::DEPTH_BUFFER_BIT);
    sjp::gfx::matrixMode(sjp::gfx::PROJECTION);
    sjp::gfx::loadIdentity();
    double near = .25, aspect = double(width) / height;
    double top = near * std::tan(48 * PI / 360);
    sjp::gfx::frustum(-top * aspect, top * aspect, -top, top, near, 2400 * h.scale());
    sjp::gfx::matrixMode(sjp::gfx::MODELVIEW);
    sjp::gfx::loadIdentity();
    double s = h.scale();
    const visual::ChaseCameraPath *chasePath = nullptr;
    if (!panorama && camera == 1) {
        size_t index = 0;
        for (size_t i = 0; i < hills().size(); ++i)
            if (h.id == hills()[i].id)
                index = i;
        chasePath = &chasePaths[index];
    }
    Vec3 wantEye, wantTarget;
    if (panorama) {
        wantEye = {35 * s + std::sin(time * .05) * 10 * s, 37 * s, 145 * s};
        wantTarget = {24 * s, -13 * s, -4 * s};
    } else if (camera == 1) {
        wantEye = chasePath->eye(f.x);
        wantTarget = {f.x, f.y + 1.1, 0};
    } else if (camera == 2) {
        wantEye = {f.x + 12, f.y + 22, 65};
        wantTarget = {f.x + 13, f.y - 9, 0};
    } else {
        wantEye = {f.x + 3, f.y + 7, 26};
        wantTarget = {f.x + 10, f.y - 3, 0};
    }
    if (!cameraInit || panorama) {
        eye = wantEye;
        target = wantTarget;
        cameraProgress = f.x;
        cameraInit = true;
    } else {
        const double blend = visual::cameraBlend(time - cameraTime);
        if (chasePath) {
            cameraProgress += (f.x - cameraProgress) * blend;
            eye = chasePath->eye(cameraProgress);
        } else {
            eye = eye + (wantEye - eye) * blend;
        }
        target = target + (wantTarget - target) * blend;
    }
    cameraTime = time;
    sky(width, height, lighting, eye, target);
    lookAt(eye, target);
    sjp::gfx::enable(sjp::gfx::DEPTH_TEST);
    sjp::gfx::enable(sjp::gfx::LIGHTING);
    sjp::gfx::enable(sjp::gfx::LIGHT0);
    sjp::gfx::enable(sjp::gfx::COLOR_MATERIAL);
    float ambient[] = {float(lighting.ambient.x), float(lighting.ambient.y), float(lighting.ambient.z), 1},
          diffuse[] = {float(lighting.direct.x), float(lighting.direct.y), float(lighting.direct.z), 1},
          position[] = {float(lighting.direction.x), float(lighting.direction.y), float(lighting.direction.z), 0},
          sunAmbient[] = {0, 0, 0, 1};
    sjp::gfx::lightModelfv(sjp::gfx::LIGHT_MODEL_AMBIENT, ambient);
    sjp::gfx::lightfv(sjp::gfx::LIGHT0, sjp::gfx::AMBIENT, sunAmbient);
    sjp::gfx::lightfv(sjp::gfx::LIGHT0, sjp::gfx::DIFFUSE, diffuse);
    sjp::gfx::lightfv(sjp::gfx::LIGHT0, sjp::gfx::POSITION, position);
    float fill[] = {float(lighting.fill.x), float(lighting.fill.y), float(lighting.fill.z), 1},
          fillPosition[] = {90, 110, -160, 0};
    sjp::gfx::enable(sjp::gfx::LIGHT1);
    sjp::gfx::lightfv(sjp::gfx::LIGHT1, sjp::gfx::DIFFUSE, fill);
    sjp::gfx::lightfv(sjp::gfx::LIGHT1, sjp::gfx::POSITION, fillPosition);
    sjp::gfx::enable(sjp::gfx::FOG);
    float fog[] = {float(lighting.fog.x), float(lighting.fog.y), float(lighting.fog.z), 1};
    sjp::gfx::fogfv(sjp::gfx::FOG_COLOR, fog);
    sjp::gfx::fogi(sjp::gfx::FOG_MODE, sjp::gfx::LINEAR);
    sjp::gfx::fogf(sjp::gfx::FOG_START, lighting.fogStart * s);
    sjp::gfx::fogf(sjp::gfx::FOG_END, lighting.fogEnd * s);
    terrain(h);
    scenery(h, f, time);
    // The start signal and the moving windsock.
    double gx = h.start() + 2, gy = h.inrun(gx);
    beam({gx, gy, -3}, {gx, gy + 2.6, -3}, .09, {.2f, .32f, .35f, 1});
    bool ready = Phase(f.phase) == Phase::Ready;
    bool blink = ready && f.elapsed >= 5 && std::fmod(f.elapsed, .7) > .35;
    sphere({gx, gy + 2.7, -3}, .22, ready ? blink ? colors::ink : colors::teal : colors::orange);
    double wx = 10 * s, wy = h.ground(wx) + 10;
    beam({wx, h.ground(wx), -23 * s}, {wx, wy, -23 * s}, .09, colors::muted);
    windsock({wx, wy, -23 * s}, f, f.elapsed);
    const Frame display = displayedAthleteFrame(h, f);
    jumperShadow(h, display, lighting);
    athlete(h, display, false);
    if (ghost && gf) {
        sjp::gfx::depthMask(sjp::gfx::FALSE);
        athlete(h, displayedAthleteFrame(h, *gf), true);
        sjp::gfx::depthMask(sjp::gfx::TRUE);
    }
    effects(h, display, time, !panorama);
    sjp::gfx::disable(sjp::gfx::FOG);
    sjp::gfx::disable(sjp::gfx::LIGHTING);
}
} // namespace sjp
