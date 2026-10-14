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
    sjp::gfx::color4f(c.r, c.g, c.b, c.a);
}
} // namespace
void Renderer::overlay() {
    sjp::gfx::disable(sjp::gfx::DEPTH_TEST);
    sjp::gfx::disable(sjp::gfx::LIGHTING);
    sjp::gfx::disable(sjp::gfx::FOG);
    sjp::gfx::disable(sjp::gfx::TEXTURE_2D);
    sjp::gfx::enable(sjp::gfx::BLEND);
    sjp::gfx::blendFunc(sjp::gfx::SRC_ALPHA, sjp::gfx::ONE_MINUS_SRC_ALPHA);
    sjp::gfx::matrixMode(sjp::gfx::PROJECTION);
    sjp::gfx::loadIdentity();
    sjp::gfx::ortho(0, width, height, 0, -1, 1);
    sjp::gfx::matrixMode(sjp::gfx::MODELVIEW);
    sjp::gfx::loadIdentity();
    uiScale = std::min(width / 1440.f, height / 900.f);
    offsetX = (width - 1440 * uiScale) / 2;
    offsetY = (height - 900 * uiScale) / 2;
    sjp::gfx::translatef(offsetX, offsetY, 0);
    sjp::gfx::scalef(uiScale, uiScale, 1);
}
void Renderer::rect(float x, float y, float w, float h, Color c, float radius) {
    if (w <= 0 || h <= 0)
        return;
    col(c);
    if (radius <= 0) {
        sjp::gfx::begin(sjp::gfx::QUADS);
        sjp::gfx::vertex2f(x, y);
        sjp::gfx::vertex2f(x + w, y);
        sjp::gfx::vertex2f(x + w, y + h);
        sjp::gfx::vertex2f(x, y + h);
        sjp::gfx::end();
        return;
    }
    radius = std::min(radius, std::min(w, h) / 2);
    sjp::gfx::begin(sjp::gfx::TRIANGLE_FAN);
    sjp::gfx::vertex2f(x + w / 2, y + h / 2);
    for (int corner = 0; corner < 4; corner++) {
        double a = -PI / 2 + corner * PI / 2;
        float cx = corner == 0 || corner == 1 ? x + w - radius : x + radius,
              cy = corner == 0 || corner == 3 ? y + radius : y + h - radius;
        for (int i = 0; i <= 8; i++) {
            double b = a + i * PI / 16;
            sjp::gfx::vertex2f(cx + std::cos(b) * radius, cy + std::sin(b) * radius);
        }
    }
    sjp::gfx::vertex2f(x + w - radius, y);
    sjp::gfx::end();
}
void Renderer::glassPanel(float x, float y, float w, float h, float radius, float opacity) {
    if (w <= 2 || h <= 2)
        return;
    // A single dark fill keeps the UI readable without blur or layered shadows.
    rect(x, y, w, h, {.042f, .097f, .125f, opacity}, std::min(radius, 7.f));
    line(x + 8, y + 1, x + w - 8, y + 1, {.65f, .78f, .81f, .13f});
}
void Renderer::gradient(float x, float y, float w, float h, Color a, Color b) {
    sjp::gfx::begin(sjp::gfx::QUADS);
    col(a);
    sjp::gfx::vertex2f(x, y);
    sjp::gfx::vertex2f(x, y + h);
    col(b);
    sjp::gfx::vertex2f(x + w, y + h);
    sjp::gfx::vertex2f(x + w, y);
    sjp::gfx::end();
}
void Renderer::line(float x, float y, float xx, float yy, Color c, float width) {
    col(c);
    sjp::gfx::lineWidth(width * uiScale);
    sjp::gfx::begin(sjp::gfx::LINES);
    sjp::gfx::vertex2f(x, y);
    sjp::gfx::vertex2f(xx, yy);
    sjp::gfx::end();
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
    sjp::gfx::enable(sjp::gfx::TEXTURE_2D);
    sjp::gfx::bindTexture(sjp::gfx::TEXTURE_2D, f.tex);
    col(c);
    float scale = size / 64;
    sjp::gfx::begin(sjp::gfx::QUADS);
    for (unsigned char ch : t) {
        if (ch >= 128)
            continue;
        const auto &g = f.glyphs[ch];
        float xx = x + g.bearingX * scale, yy = y + size - g.bearingY * scale, w = g.w * scale,
              h = g.h * scale;
        sjp::gfx::texCoord2f(g.u, g.v);
        sjp::gfx::vertex2f(xx, yy);
        sjp::gfx::texCoord2f(g.u + g.w / 1024, g.v);
        sjp::gfx::vertex2f(xx + w, yy);
        sjp::gfx::texCoord2f(g.u + g.w / 1024, g.v + g.h / 1024);
        sjp::gfx::vertex2f(xx + w, yy + h);
        sjp::gfx::texCoord2f(g.u, g.v + g.h / 1024);
        sjp::gfx::vertex2f(xx, yy + h);
        x += g.advance * scale;
    }
    sjp::gfx::end();
    sjp::gfx::disable(sjp::gfx::TEXTURE_2D);
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
    sjp::gfx::present(synchronousRendering);
}
void Renderer::capture(const std::string &path) {
    sjp::gfx::captureImage(path);
}
} // namespace sjp
