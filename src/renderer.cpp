#include "renderer.h"
#include "presentation_timing.h"
#include "original_skeleton.h"
#include "visual_weather.h"
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
void sky(int width, int height) {
    sjp::gfx::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::CURRENT_BIT);
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
        col({.79f - smooth * .38f, .86f - smooth * .25f, .88f - smooth * .11f, 1});
        sjp::gfx::vertex2f(0, height * t);
        sjp::gfx::vertex2f(width, height * t);
    }
    sjp::gfx::end();
    sjp::gfx::popMatrix();
    sjp::gfx::matrixMode(sjp::gfx::PROJECTION);
    sjp::gfx::popMatrix();
    sjp::gfx::matrixMode(sjp::gfx::MODELVIEW);
    sjp::gfx::popAttrib();
}
void jumperShadow(const Hill &h, const Frame &f) {
    const bool onInrun = f.x <= 0;
    const auto surface = [&](double x) { return onInrun ? h.inrun(x) : h.ground(x); };
    const double clearance = std::max(0.0, f.y - surface(f.x));
    const double offsetX = onInrun ? 0 : std::min(14.0, clearance * .32);
    const double offsetZ = onInrun ? 0 : -std::min(10.0, clearance * .25);
    const double softness = .035 + std::min(.12, clearance * .003);
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
    const auto project = [&](OriginalJoint joint) {
        const Vec3 local = skeleton[joint];
        const double lift = std::max(0.0, local.y);
        return Vec3{f.x + local.x + offsetX + lift * .12, 0, local.z + offsetZ - lift * .15};
    };
    sjp::gfx::pushAttrib(sjp::gfx::ENABLE_BIT | sjp::gfx::DEPTH_BUFFER_BIT | sjp::gfx::CURRENT_BIT | sjp::gfx::POLYGON_BIT | sjp::gfx::COLOR_BUFFER_BIT);
    sjp::gfx::disable(sjp::gfx::LIGHTING);
    sjp::gfx::disable(sjp::gfx::TEXTURE_2D);
    sjp::gfx::enable(sjp::gfx::BLEND);
    sjp::gfx::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::gfx::depthMask(sjp::gfx::FALSE);
    sjp::gfx::enable(sjp::gfx::POLYGON_OFFSET_FILL);
    sjp::gfx::polygonOffset(-1, -1);
    const auto shadowVertex = [&](Vec3 p, float alpha) {
        if (onInrun) {
            // The receiver is the actual ramp, bounded by its lip and rails.
            p.x = std::min(0.0, p.x);
            p.z = clamp(p.z, -2.25, 2.25);
        }
        col({.055f, .10f, .14f, alpha});
        sjp::gfx::vertex3d(p.x, surface(p.x) + .025, p.z);
    };
    const auto ellipse = [&](Vec3 center, double rx, double rz, float opacity) {
        constexpr int sides = 24;
        sjp::gfx::begin(sjp::gfx::TRIANGLE_FAN);
        shadowVertex(center, opacity);
        for (int i = 0; i <= sides; ++i) {
            const double angle = i * 2 * PI / sides;
            shadowVertex(center + Vec3{std::cos(angle) * rx * .66, 0, std::sin(angle) * rz * .66},
                         opacity * .93f);
        }
        sjp::gfx::end();
        sjp::gfx::begin(sjp::gfx::TRIANGLE_STRIP);
        for (int i = 0; i <= sides; ++i) {
            const double angle = i * 2 * PI / sides;
            shadowVertex(center + Vec3{std::cos(angle) * rx * .66, 0, std::sin(angle) * rz * .66},
                         opacity * .93f);
            shadowVertex(center + Vec3{std::cos(angle) * rx, 0, std::sin(angle) * rz}, 0);
        }
        sjp::gfx::end();
    };
    const auto segment = [&](OriginalJoint from, OriginalJoint to, double radius, float opacity) {
        const Vec3 a = project(from), b = project(to);
        const double dx = b.x - a.x, dz = b.z - a.z, length = std::hypot(dx, dz);
        if (length < 1e-5) {
            ellipse(a, radius + softness, radius + softness, opacity);
            return;
        }
        const Vec3 side{-dz / length, 0, dx / length};
        sjp::gfx::begin(sjp::gfx::QUADS);
        shadowVertex(a - side * radius, opacity);
        shadowVertex(b - side * radius, opacity);
        shadowVertex(b + side * radius, opacity);
        shadowVertex(a + side * radius, opacity);
        sjp::gfx::end();
        for (double sign : {-1.0, 1.0}) {
            sjp::gfx::begin(sjp::gfx::TRIANGLE_STRIP);
            shadowVertex(a + side * (radius * sign), opacity);
            shadowVertex(a + side * ((radius + softness) * sign), 0);
            shadowVertex(b + side * (radius * sign), opacity);
            shadowVertex(b + side * ((radius + softness) * sign), 0);
            sjp::gfx::end();
        }
        ellipse(a, radius + softness, radius + softness, opacity * .7f);
        ellipse(b, radius + softness, radius + softness, opacity * .7f);
    };
    // A restrained soft footprint ties the silhouette to the snow. Most of
    // the contrast now comes from the visible body and ski shapes, rather
    // than a large pale oval that loses the athlete's outline in flight.
    const double spread = std::min(.65, clearance * .016);
    ellipse({f.x + offsetX + .12, 0, offsetZ}, 1.35 + spread, .43 + spread * .45, visibility * .32f);
    segment(OriginalJoint::HipCenter, OriginalJoint::ShoulderCenter, .17, visibility * .43f);
    segment(OriginalJoint::ShoulderCenter, OriginalJoint::Head, .09, visibility * .34f);
    ellipse(project(OriginalJoint::Head), .20 + softness, .19 + softness, visibility * .42f);
    const OriginalJoint ankles[]{OriginalJoint::Ankle1, OriginalJoint::Ankle2};
    const OriginalJoint knees[]{OriginalJoint::Knee1, OriginalJoint::Knee2};
    const OriginalJoint hips[]{OriginalJoint::Hip1, OriginalJoint::Hip2};
    const OriginalJoint shoulders[]{OriginalJoint::Shoulder1, OriginalJoint::Shoulder2};
    const OriginalJoint elbows[]{OriginalJoint::Elbow1, OriginalJoint::Elbow2};
    const OriginalJoint hands[]{OriginalJoint::Hand1, OriginalJoint::Hand2};
    const OriginalJoint rear[]{OriginalJoint::SkiRear1, OriginalJoint::SkiRear2};
    const OriginalJoint front[]{OriginalJoint::SkiFront1, OriginalJoint::SkiFront2};
    const OriginalJoint tips[]{OriginalJoint::SkiTip1, OriginalJoint::SkiTip2};
    for (size_t side = 0; side < 2; ++side) {
        segment(hips[side], knees[side], .105, visibility * .28f);
        segment(knees[side], ankles[side], .075, visibility * .24f);
        segment(shoulders[side], elbows[side], .06, visibility * .22f);
        segment(elbows[side], hands[side], .05, visibility * .22f);
        segment(rear[side], front[side], .036, visibility * .29f);
        segment(front[side], tips[side], .033, visibility * .26f);
    }
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
    sjp::gfx::clearColor(.79f, .86f, .88f, 1);
    sjp::gfx::clear(sjp::gfx::COLOR_BUFFER_BIT | sjp::gfx::DEPTH_BUFFER_BIT);
    sjp::gfx::matrixMode(sjp::gfx::PROJECTION);
    sjp::gfx::loadIdentity();
    double near = .25, aspect = double(width) / height;
    double top = near * std::tan(48 * PI / 360);
    sjp::gfx::frustum(-top * aspect, top * aspect, -top, top, near, 2400 * h.scale());
    sjp::gfx::matrixMode(sjp::gfx::MODELVIEW);
    sjp::gfx::loadIdentity();
    sky(width, height);
    double s = h.scale();
    Vec3 wantEye, wantTarget;
    if (panorama) {
        wantEye = {35 * s + std::sin(time * .05) * 10 * s, 37 * s, 145 * s};
        wantTarget = {24 * s, -13 * s, -4 * s};
    } else if (camera == 1) {
        wantEye = {f.x - 13, f.y + 7, 9};
        wantTarget = {f.x + 18, f.y - 3, 0};
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
        cameraInit = true;
    } else {
        const double blend = visual::cameraBlend(time - cameraTime);
        eye = eye + (wantEye - eye) * blend;
        target = target + (wantTarget - target) * blend;
    }
    cameraTime = time;
    lookAt(eye, target);
    sjp::gfx::enable(sjp::gfx::DEPTH_TEST);
    sjp::gfx::enable(sjp::gfx::LIGHTING);
    sjp::gfx::enable(sjp::gfx::LIGHT0);
    sjp::gfx::enable(sjp::gfx::COLOR_MATERIAL);
    float ambient[] = {.28f, .34f, .40f, 1}, diffuse[] = {.78f, .73f, .64f, 1},
            position[] = {-120, 240, 150, 0}, sunAmbient[] = {.025f, .028f, .032f, 1};
    sjp::gfx::lightModelfv(sjp::gfx::LIGHT_MODEL_AMBIENT, ambient);
    sjp::gfx::lightfv(sjp::gfx::LIGHT0, sjp::gfx::AMBIENT, sunAmbient);
    sjp::gfx::lightfv(sjp::gfx::LIGHT0, sjp::gfx::DIFFUSE, diffuse);
    sjp::gfx::lightfv(sjp::gfx::LIGHT0, sjp::gfx::POSITION, position);
    float fill[] = {.13f, .19f, .24f, 1}, fillPosition[] = {90, 110, -160, 0};
    sjp::gfx::enable(sjp::gfx::LIGHT1);
    sjp::gfx::lightfv(sjp::gfx::LIGHT1, sjp::gfx::DIFFUSE, fill);
    sjp::gfx::lightfv(sjp::gfx::LIGHT1, sjp::gfx::POSITION, fillPosition);
    sjp::gfx::enable(sjp::gfx::FOG);
    float fog[] = {.79f, .86f, .88f, 1};
    sjp::gfx::fogfv(sjp::gfx::FOG_COLOR, fog);
    sjp::gfx::fogi(sjp::gfx::FOG_MODE, sjp::gfx::LINEAR);
    sjp::gfx::fogf(sjp::gfx::FOG_START, 170 * s);
    sjp::gfx::fogf(sjp::gfx::FOG_END, 1050 * s);
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
    jumperShadow(h, display);
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
