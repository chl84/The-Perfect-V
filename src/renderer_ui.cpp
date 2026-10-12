#include "original_skeleton.h"
#include "renderer.h"
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
void roundedGradient(float x, float y, float w, float h, float radius, Color top, Color bottom) {
    radius = std::min(radius, std::min(w, h) / 2);
    auto point = [&](float px, float py) {
        float t = clamp((py - y) / h, 0.f, 1.f);
        col({top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t,
             top.a + (bottom.a - top.a) * t});
        glVertex2f(px, py);
    };
    glBegin(GL_TRIANGLE_FAN);
    point(x + w / 2, y + h / 2);
    for (int corner = 0; corner < 4; ++corner) {
        double a = -PI / 2 + corner * PI / 2;
        float cx = corner == 0 || corner == 1 ? x + w - radius : x + radius;
        float cy = corner == 0 || corner == 3 ? y + radius : y + h - radius;
        for (int i = 0; i <= 12; ++i) {
            double b = a + i * PI / 24;
            point(cx + std::cos(b) * radius, cy + std::sin(b) * radius);
        }
    }
    point(x + w - radius, y);
    glEnd();
}
} // namespace
void Renderer::overlay() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    uiScale = std::min(width / 1440.f, height / 900.f);
    offsetX = (width - 1440 * uiScale) / 2;
    offsetY = (height - 900 * uiScale) / 2;
    glTranslatef(offsetX, offsetY, 0);
    glScalef(uiScale, uiScale, 1);
}
void Renderer::rect(float x, float y, float w, float h, Color c, float radius) {
    if (w <= 0 || h <= 0)
        return;
    col(c);
    if (radius <= 0) {
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();
        return;
    }
    radius = std::min(radius, std::min(w, h) / 2);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x + w / 2, y + h / 2);
    for (int corner = 0; corner < 4; corner++) {
        double a = -PI / 2 + corner * PI / 2;
        float cx = corner == 0 || corner == 1 ? x + w - radius : x + radius,
              cy = corner == 0 || corner == 3 ? y + radius : y + h - radius;
        for (int i = 0; i <= 8; i++) {
            double b = a + i * PI / 16;
            glVertex2f(cx + std::cos(b) * radius, cy + std::sin(b) * radius);
        }
    }
    glVertex2f(x + w - radius, y);
    glEnd();
}
void Renderer::glassPanel(float x, float y, float w, float h, float radius, float opacity) {
    if (w <= 2 || h <= 2)
        return;
    // Layered translucent shapes produce a quiet, soft edge without a framebuffer blur pass.
    rect(x - 9, y - 5, w + 18, h + 20, {.01f, .025f, .035f, .025f}, radius + 9);
    rect(x - 5, y - 2, w + 10, h + 13, {.01f, .025f, .035f, .05f}, radius + 5);
    rect(x - 2, y + 2, w + 4, h + 5, {.01f, .025f, .035f, .12f}, radius + 2);
    roundedGradient(x, y, w, h, radius, {.47f, .65f, .7f, .31f}, {.23f, .4f, .46f, .13f});
    roundedGradient(x + 1, y + 1, w - 2, h - 2, std::max(0.f, radius - 1), {.068f, .13f, .17f, opacity},
                    {.031f, .071f, .105f, opacity + (1 - opacity) * .32f});
    line(x + radius + 3, y + 1, x + w - radius - 3, y + 1, {.79f, .92f, .95f, .13f});
}
void Renderer::gradient(float x, float y, float w, float h, Color a, Color b) {
    glBegin(GL_QUADS);
    col(a);
    glVertex2f(x, y);
    glVertex2f(x, y + h);
    col(b);
    glVertex2f(x + w, y + h);
    glVertex2f(x + w, y);
    glEnd();
}
void Renderer::line(float x, float y, float xx, float yy, Color c, float width) {
    col(c);
    glLineWidth(width * uiScale);
    glBegin(GL_LINES);
    glVertex2f(x, y);
    glVertex2f(xx, yy);
    glEnd();
}
float Renderer::textWidth(const std::string &t, float size, bool b) const {
    const Font &f = b ? boldFont : regular;
    float w = 0;
    for (unsigned char c : t)
        if (c < 128)
            w += f.glyphs[c].advance * size / 64;
    return w;
}
void Renderer::text(const std::string &t, float x, float y, float size, Color c, bool bold) {
    Font &f = bold ? boldFont : regular;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    col(c);
    float scale = size / 64;
    glBegin(GL_QUADS);
    for (unsigned char ch : t) {
        if (ch >= 128)
            continue;
        const auto &g = f.glyphs[ch];
        float xx = x + g.bearingX * scale, yy = y + size - g.bearingY * scale, w = g.w * scale,
              h = g.h * scale;
        glTexCoord2f(g.u, g.v);
        glVertex2f(xx, yy);
        glTexCoord2f(g.u + g.w / 1024, g.v);
        glVertex2f(xx + w, yy);
        glTexCoord2f(g.u + g.w / 1024, g.v + g.h / 1024);
        glVertex2f(xx + w, yy + h);
        glTexCoord2f(g.u, g.v + g.h / 1024);
        glVertex2f(xx, yy + h);
        x += g.advance * scale;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}
void Renderer::textRight(const std::string &t, float x, float y, float size, Color c, bool bold) {
    text(t, x - textWidth(t, size, bold), y, size, c, bold);
}
void Renderer::pill(const std::string &t, float x, float y, Color c) {
    float w = textWidth(t, 12, true) + 28;
    rect(x, y, w, 27, {c.r, c.g, c.b, .22f}, 7);
    rect(x + 1, y + 1, w - 2, 25, {.035f, .095f, .13f, .66f}, 6);
    text(t, x + 14, y + 5, 12, c, true);
}
void Renderer::windArrow(float x, float y, double speed, double angle) {
    float dx = std::cos(angle), dy = -std::sin(angle);
    if (speed < .01) {
        line(x - 6, y, x + 6, y, colors::muted, 2);
        return;
    }
    line(x - 17 * dx, y - 17 * dy, x + 17 * dx, y + 17 * dy, colors::teal, 2);
    line(x + 17 * dx, y + 17 * dy, x + 9 * dx - 7 * dy, y + 9 * dy + 7 * dx, colors::teal, 2);
    line(x + 17 * dx, y + 17 * dy, x + 9 * dx + 7 * dy, y + 9 * dy - 7 * dx, colors::teal, 2);
}
void Renderer::finish() {
    SDL_GL_SwapWindow(window);
}
void Renderer::capture(const std::string &path) {
    std::vector<unsigned char> p(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, p.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; y--)
        out.write(reinterpret_cast<const char *>(p.data() + y * width * 3), width * 3);
}
} // namespace sjp
