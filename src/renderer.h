#pragma once
#include "physics.h"
#include <GL/gl.h>
#include <SDL.h>
#include <array>
#include <string>
#include <vector>

namespace sjp {
struct Color {
    float r, g, b, a = 1;
};
namespace colors {
constexpr Color ink{.045f, .09f, .13f, 1}, panel{.065f, .12f, .16f, .94f}, white{.93f, .97f, .98f, 1},
    muted{.57f, .68f, .74f, 1}, teal{.27f, .88f, .78f, 1}, orange{1, .59f, .37f, 1};
}
class Renderer {
  public:
    Renderer(SDL_Window *window, const std::string &assetPath);
    ~Renderer();
    void begin(const Hill &, const Frame &, double time, int camera, bool panorama, bool ghost = false,
               const Frame *ghostFrame = nullptr);
    void overlay();
    void rect(float x, float y, float w, float h, Color c, float radius = 0);
    void gradient(float x, float y, float w, float h, Color a, Color b);
    void glassPanel(float x, float y, float w, float h, float radius = 12, float opacity = .82f);
    void line(float x, float y, float xx, float yy, Color c, float width = 1);
    void text(const std::string &, float x, float y, float size, Color c = colors::white, bool bold = false);
    float textWidth(const std::string &, float size, bool bold = false) const;
    void textRight(const std::string &, float x, float y, float size, Color c = colors::white,
                   bool bold = false);
    void pill(const std::string &, float x, float y, Color c = colors::teal);
    void windArrow(float x, float y, double speed, double angle);
    void finish();
    void capture(const std::string &path);
    void resetCamera() { cameraInit = false; }
    int width = 1440, height = 900;
    float uiScale = 1, offsetX = 0, offsetY = 0;
    std::string device;

  private:
    struct Glyph {
        float u, v, w, h, bearingX, bearingY, advance;
    };
    struct Font {
        GLuint tex = 0;
        std::array<Glyph, 128> glyphs{};
    };
    SDL_Window *window;
    Font regular, boldFont;
    GLuint snowTexture = 0;
    std::array<GLuint, HILL_COUNT> landscapes{};
    Vec3 eye{}, target{};
    bool cameraInit = false;
    Font loadFont(const std::string &);
    void terrain(const Hill &);
    void scenery(const Hill &, const Frame &, double time);
    void athlete(const Hill &, const Frame &, bool ghost);
    void effects(const Hill &, const Frame &, double time);
    struct VisualParticle {
        Vec3 origin, velocity;
        double born = 0, lifetime = 1, size = .04;
    };
    std::vector<VisualParticle> snowParticles;
    Frame lastEffectFrame{};
    std::string effectHill;
    bool effectsInitialized = false;
};
} // namespace sjp
