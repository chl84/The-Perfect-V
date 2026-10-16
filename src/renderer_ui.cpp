#include "nationality.h"
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
    // Also clear the world veil before UI previews temporarily enable depth.
    sjp::gfx::backdropHaze({}, {});
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
void Renderer::loadFlagAtlas(const std::string &path) {
    SDL_Surface *bitmap = SDL_LoadBMP(path.c_str());
    if (!bitmap)
        throw std::runtime_error("Could not load flags: " + path);
    SDL_Surface *rgb = SDL_ConvertSurface(bitmap, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(bitmap);
    if (!rgb)
        throw std::runtime_error("Could not decode flag atlas");
    if (rgb->w != FLAG_ATLAS_WIDTH || rgb->h != FLAG_ATLAS_HEIGHT) {
        SDL_DestroySurface(rgb);
        throw std::runtime_error("Flag atlas dimensions do not match its catalog");
    }
    std::vector<uint8_t> pixels(size_t(rgb->w) * rgb->h * 3);
    for (int y = 0; y < rgb->h; ++y) {
        const auto *row = static_cast<const uint8_t *>(rgb->pixels) + y * rgb->pitch;
        std::copy_n(row, rgb->w * 3, pixels.data() + size_t(y) * rgb->w * 3);
    }
    SDL_DestroySurface(rgb);
    gfx::genTextures(1, &flagTexture);
    gfx::bindTexture(gfx::TEXTURE_2D, flagTexture);
    gfx::pixelStorei(gfx::UNPACK_ALIGNMENT, 1);
    gfx::texImage2D(gfx::TEXTURE_2D, 0, gfx::RGB, FLAG_ATLAS_WIDTH, FLAG_ATLAS_HEIGHT, 0, gfx::RGB,
                    gfx::UNSIGNED_BYTE, pixels.data());
    gfx::texParameteri(gfx::TEXTURE_2D, gfx::TEXTURE_MIN_FILTER, gfx::LINEAR);
    gfx::texParameteri(gfx::TEXTURE_2D, gfx::TEXTURE_MAG_FILTER, gfx::LINEAR);
    gfx::texParameteri(gfx::TEXTURE_2D, gfx::TEXTURE_WRAP_S, gfx::CLAMP_TO_EDGE);
    gfx::texParameteri(gfx::TEXTURE_2D, gfx::TEXTURE_WRAP_T, gfx::CLAMP_TO_EDGE);
}
void Renderer::countryFlag(const std::string &country, float x, float y, float w, float h) {
    if (w <= 0 || h <= 0)
        return;
    const auto *nation = nationality(country);
    if (!nation) {
        // An old/custom save with an unknown code must never show a false flag.
        rect(x, y, w, h, {.3f, .4f, .45f, .24f}, 2);
        const float size = std::min(13.f, h - 2);
        text("?", x + (w - textWidth("?", size)) / 2, y + (h - size) / 2 - 1, size, colors::muted);
        return;
    }
    const float scale = std::min(w / nation->flagWidth, h / nation->flagHeight);
    const float fw = nation->flagWidth * scale, fh = nation->flagHeight * scale;
    x += (w - fw) / 2;
    y += (h - fh) / 2;
    // Thin neutral edge helps white flags against snow; no rounding or tint.
    rect(x - .6f, y - .6f, fw + 1.2f, fh + 1.2f, {1, 1, 1, .23f});
    gfx::enable(gfx::TEXTURE_2D);
    gfx::bindTexture(gfx::TEXTURE_2D, flagTexture);
    col({1, 1, 1, 1});
    const float u = float(nation->flagX) / FLAG_ATLAS_WIDTH;
    const float v = float(nation->flagY) / FLAG_ATLAS_HEIGHT;
    const float du = float(nation->flagWidth) / FLAG_ATLAS_WIDTH;
    const float dv = float(nation->flagHeight) / FLAG_ATLAS_HEIGHT;
    gfx::begin(gfx::QUADS);
    gfx::texCoord2f(u, v);
    gfx::vertex2f(x, y);
    gfx::texCoord2f(u + du, v);
    gfx::vertex2f(x + fw, y);
    gfx::texCoord2f(u + du, v + dv);
    gfx::vertex2f(x + fw, y + fh);
    gfx::texCoord2f(u, v + dv);
    gfx::vertex2f(x, y + fh);
    gfx::end();
    gfx::disable(gfx::TEXTURE_2D);
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
void Renderer::jumperPreview(JumperAppearance appearance, float x, float y, float scale) {
    gfx::pushAttrib(gfx::ENABLE_BIT | gfx::CURRENT_BIT | gfx::LIGHTING_BIT | gfx::TEXTURE_BIT |
                    gfx::COLOR_BUFFER_BIT | gfx::DEPTH_BUFFER_BIT);
    gfx::disable(gfx::TEXTURE_2D);
    gfx::disable(gfx::FOG);
    gfx::disable(gfx::CULL_FACE);
    gfx::enable(gfx::DEPTH_TEST);
    gfx::depthMask(true);
    gfx::enable(gfx::LIGHTING);
    gfx::enable(gfx::LIGHT0);
    gfx::enable(gfx::LIGHT1);
    gfx::enable(gfx::COLOR_MATERIAL);
    gfx::enable(gfx::NORMALIZE);
    gfx::matrixMode(gfx::PROJECTION);
    gfx::pushMatrix();
    gfx::loadIdentity();
    gfx::ortho(0, width, height, 0, -2000, 2000);
    gfx::matrixMode(gfx::MODELVIEW);
    gfx::pushMatrix();
    gfx::loadIdentity();
    gfx::arenaLights({}, 0, 1, 1, 0, 1);
    gfx::inrunLights({}, 0, 1, 1, 0, 1);
    gfx::signalLight({{0, 0, 0, 1}, {}}, {}, 1);
    const float ambient[]{.40f, .43f, .47f, 1}, diffuse[]{.78f, .76f, .70f, 1},
        direction[]{-.4f, -.7f, 1, 0}, fill[]{.25f, .29f, .34f, 1},
        fillDirection[]{.8f, .2f, 1, 0}, specular[]{.20f, .20f, .20f, 1};
    gfx::lightModelfv(gfx::LIGHT_MODEL_AMBIENT, ambient);
    gfx::lightfv(gfx::LIGHT0, gfx::DIFFUSE, diffuse);
    gfx::lightfv(gfx::LIGHT0, gfx::POSITION, direction);
    gfx::lightfv(gfx::LIGHT0, gfx::SPECULAR, specular);
    gfx::lightfv(gfx::LIGHT1, gfx::DIFFUSE, fill);
    gfx::lightfv(gfx::LIGHT1, gfx::POSITION, fillDirection);
    gfx::translatef(offsetX + x * uiScale, offsetY + y * uiScale, 0);
    gfx::scalef(scale * uiScale, -scale * uiScale, scale * uiScale);
    constexpr float c = .57357644f, s = -.81915204f;
    const float rotation[]{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    // Look down at the deck as well as across the athlete. This transform
    // belongs only to the selection preview, never the gameplay skeleton.
    constexpr float tiltC = .90630779f, tiltS = .42261826f;
    const float tilt[]{1, 0, 0, 0, 0, tiltC, tiltS, 0, 0, -tiltS, tiltC, 0, 0, 0, 0, 1};
    gfx::multMatrixf(tilt);
    gfx::multMatrixf(rotation);
    jumperPreviewGeometry(appearance);
    gfx::popMatrix();
    gfx::matrixMode(gfx::PROJECTION);
    gfx::popMatrix();
    gfx::matrixMode(gfx::MODELVIEW);
    gfx::popAttrib();
}
} // namespace sjp
