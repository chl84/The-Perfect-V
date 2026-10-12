#include "renderer.h"
#include "original_skeleton.h"
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
    glColor4f(c.r, c.g, c.b, c.a);
}
void vertex(Vec3 p) {
    glVertex3d(p.x, p.y, p.z);
}
void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
    col(color);
    Vec3 n = cross(b - a, c - a).normalized();
    glBegin(GL_QUADS);
    glNormal3d(n.x, n.y, n.z);
    vertex(a);
    vertex(b);
    vertex(c);
    vertex(d);
    glEnd();
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
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= 12; j++)
            for (double k : {a, b}) {
                double t = j * 2 * PI / 12;
                Vec3 n{std::cos(k) * std::cos(t), std::sin(k), std::cos(k) * std::sin(t)};
                glNormal3d(n.x, n.y, n.z);
                vertex(p + n * r);
            }
        glEnd();
    }
}
// A sky gradient supplies the atmospheric backdrop without moving the camera.
void sky(int width, int height) {
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, 0, height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glBegin(GL_QUAD_STRIP);
    for (int band = 0; band <= 24; ++band) {
        const float t = band / 24.f;
        const float smooth = t * t * (3 - 2 * t);
        col({.79f - smooth * .38f, .86f - smooth * .25f, .88f - smooth * .11f, 1});
        glVertex2f(0, height * t);
        glVertex2f(width, height * t);
    }
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
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
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT | GL_POLYGON_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -1);
    const auto shadowVertex = [&](Vec3 p, float alpha) {
        if (onInrun) {
            // The receiver is the actual ramp, bounded by its lip and rails.
            p.x = std::min(0.0, p.x);
            p.z = clamp(p.z, -2.25, 2.25);
        }
        col({.055f, .10f, .14f, alpha});
        glVertex3d(p.x, surface(p.x) + .025, p.z);
    };
    const auto ellipse = [&](Vec3 center, double rx, double rz, float opacity) {
        constexpr int sides = 24;
        glBegin(GL_TRIANGLE_FAN);
        shadowVertex(center, opacity);
        for (int i = 0; i <= sides; ++i) {
            const double angle = i * 2 * PI / sides;
            shadowVertex(center + Vec3{std::cos(angle) * rx * .66, 0, std::sin(angle) * rz * .66},
                         opacity * .93f);
        }
        glEnd();
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= sides; ++i) {
            const double angle = i * 2 * PI / sides;
            shadowVertex(center + Vec3{std::cos(angle) * rx * .66, 0, std::sin(angle) * rz * .66},
                         opacity * .93f);
            shadowVertex(center + Vec3{std::cos(angle) * rx, 0, std::sin(angle) * rz}, 0);
        }
        glEnd();
    };
    const auto segment = [&](OriginalJoint from, OriginalJoint to, double radius, float opacity) {
        const Vec3 a = project(from), b = project(to);
        const double dx = b.x - a.x, dz = b.z - a.z, length = std::hypot(dx, dz);
        if (length < 1e-5) {
            ellipse(a, radius + softness, radius + softness, opacity);
            return;
        }
        const Vec3 side{-dz / length, 0, dx / length};
        glBegin(GL_QUADS);
        shadowVertex(a - side * radius, opacity);
        shadowVertex(b - side * radius, opacity);
        shadowVertex(b + side * radius, opacity);
        shadowVertex(a + side * radius, opacity);
        glEnd();
        for (double sign : {-1.0, 1.0}) {
            glBegin(GL_TRIANGLE_STRIP);
            shadowVertex(a + side * (radius * sign), opacity);
            shadowVertex(a + side * ((radius + softness) * sign), 0);
            shadowVertex(b + side * (radius * sign), opacity);
            shadowVertex(b + side * ((radius + softness) * sign), 0);
            glEnd();
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
    glPopAttrib();
}
void lookAt(Vec3 eye, Vec3 center) {
    Vec3 f = (center - eye).normalized(), s = cross(f, {0, 1, 0}).normalized(), u = cross(s, f);
    GLfloat m[] = {float(s.x), float(u.x), float(-f.x), 0, float(s.y), float(u.y), float(-f.y), 0,
                   float(s.z), float(u.z), float(-f.z), 0, 0,          0,          0,           1};
    glMultMatrixf(m);
    glTranslated(-eye.x, -eye.y, -eye.z);
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
    glGenTextures(1, &f.tex);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, atlas, atlas, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return f;
}
Renderer::Renderer(SDL_Window *w, const std::string &assets) : window(w) {
    regular = loadFont(assets + "/fonts/Interim-Regular.ttf");
    boldFont = loadFont(assets + "/fonts/Interim-Bold.ttf");
    const auto *d = glGetString(GL_RENDERER);
    device = d ? reinterpret_cast<const char *>(d) : "Unknown OpenGL renderer";
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glHint(GL_FOG_HINT, GL_NICEST);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
Renderer::~Renderer() {
    for (auto l : landscapes)
        if (l)
            glDeleteLists(l, 1);
    glDeleteTextures(1, &regular.tex);
    glDeleteTextures(1, &boldFont.tex);
    if (snowTexture)
        glDeleteTextures(1, &snowTexture);
    if (naturalSnowTexture)
        glDeleteTextures(1, &naturalSnowTexture);
}
void Renderer::begin(const Hill &h, const Frame &f, double time, int camera, bool panorama, bool ghost,
                     const Frame *gf) {
    SDL_GL_GetDrawableSize(window, &width, &height);
    width = std::max(1, width);
    height = std::max(1, height);
    glViewport(0, 0, width, height);
    glClearColor(.79f, .86f, .88f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    double near = .25, aspect = double(width) / height;
    double top = near * std::tan(48 * PI / 360);
    glFrustum(-top * aspect, top * aspect, -top, top, near, 2400 * h.scale());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
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
        eye = eye + (wantEye - eye) * .16;
        target = target + (wantTarget - target) * .16;
    }
    lookAt(eye, target);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    GLfloat ambient[] = {.28f, .34f, .40f, 1}, diffuse[] = {.78f, .73f, .64f, 1},
            position[] = {-120, 240, 150, 0}, sunAmbient[] = {.025f, .028f, .032f, 1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_AMBIENT, sunAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    GLfloat fill[] = {.13f, .19f, .24f, 1}, fillPosition[] = {90, 110, -160, 0};
    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, fill);
    glLightfv(GL_LIGHT1, GL_POSITION, fillPosition);
    glEnable(GL_FOG);
    GLfloat fog[] = {.79f, .86f, .88f, 1};
    glFogfv(GL_FOG_COLOR, fog);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 170 * s);
    glFogf(GL_FOG_END, 1050 * s);
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
    Vec3 end{wx - 2.2, wy - .3 - std::sin(time * 3) * .12, -23 * s};
    beam({wx, wy, -23 * s}, end, .23, colors::orange);
    const Frame display = displayedAthleteFrame(h, f);
    jumperShadow(h, display);
    athlete(h, display, false);
    if (ghost && gf) {
        glDepthMask(GL_FALSE);
        athlete(h, displayedAthleteFrame(h, *gf), true);
        glDepthMask(GL_TRUE);
    }
    effects(h, display, time);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
}
} // namespace sjp
