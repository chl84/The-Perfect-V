#pragma once
#include "physics.h"
#include "jumper_appearance.h"
#include "draw_commands.h"
#include "camera_path.h"
#include "visual_environment.h"
#include "visual_inrun.h"
#include "visual_grandstands.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sjp {
namespace visual { class SnowAccumulation; class SkiTrackCache; struct SunShadowField; }
// Fall contact correction is presentation only. Never feed this Frame to physics,
// input, camera tracking, scoring or replay storage.
Frame displayedAthleteFrame(const Hill &, const Frame &, JumperAppearance = JumperAppearance::Pro);
struct Color {
    float r, g, b, a = 1;
};
namespace colors {
constexpr Color ink{.045f, .09f, .13f, 1}, panel{.065f, .12f, .16f, .94f}, white{.93f, .97f, .98f, 1},
    muted{.57f, .68f, .74f, 1}, teal{.27f, .88f, .78f, 1}, orange{1, .59f, .37f, 1};
}
class Renderer {
  public:
    Renderer(SDL_Window *window, const std::string &assetPath, int msaaSamples = 4,
             bool vsync = true, bool validation = false);
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
    void countryFlag(const std::string &country, float x, float y, float w = 32, float h = 22);
    void windArrow(float x, float y, double speed, double angle);
    void jumperPreview(JumperAppearance, float x, float y, float scale, float yawDegrees = -55);
    void finish();
    void capture(const std::string &path);
    void resetCamera() { cameraInit = false; }
    void setHillRecord(double distance) { hillRecordDistance = distance; }
    void setJumperAppearance(JumperAppearance appearance) { jumperAppearance = appearance; }
    void setJumpWeather(uint32_t seed, environment::Selection selection = {});
    void setEnvironment(environment::Conditions, bool restart = false);
    const environment::Conditions &environmentConditions() const { return sceneEnvironment; }
    // Visual diagnostics can compare identical scenes with the material disabled.
    void setBackdropMist(bool enabled) { backdropMist = enabled; }
    void setSnowGlitter(bool enabled) { snowGlitter = enabled; }
    void setLampGlow(bool enabled) { lampGlow = enabled; }
    void setSceneryShadows(bool enabled) { sceneryShadows = enabled; }
    void setSkiTrackHistory(int hill, const std::vector<Frame> *frames) {
        trackHistoryHill = hill; trackHistory = frames;
    }
    void setSynchronousRendering(bool enabled) { synchronousRendering = enabled; }
    bool sceneReady() const { return terrainReady && sceneryShadowsReady; }
    bool sceneReadyFor(const Hill &hill) const { return sceneReady() && sceneHill == hill.id; }
    // Explicit diagnostics can wait for a complete scene before taking captures.
    void waitForScene();
    int width = 1440, height = 900;
    float uiScale = 1, offsetX = 0, offsetY = 0;
    std::string device;

  private:
    struct Glyph {
        float u, v, w, h, bearingX, bearingY, advance;
    };
    struct Font {
        sjp::gfx::Id tex = 0;
        std::array<Glyph, 128> glyphs{};
    };
    SDL_Window *window;
    Font regular, boldFont;
    sjp::gfx::Id flagTexture = 0;
    sjp::gfx::Id mountainTexture = 0;
    sjp::gfx::Id snowTexture = 0;
    sjp::gfx::Id naturalSnowTexture = 0;
    sjp::gfx::Id contactShadowTexture = 0;
    sjp::gfx::Id concreteTexture = 0;
    sjp::gfx::Id ledPixelTexture = 0;
    sjp::gfx::Id ledGlowTexture = 0;
    sjp::gfx::Id spruceMarkerTexture = 0;
    sjp::gfx::Id sprayMarkerTexture = 0;
    sjp::gfx::Id recordMarkerMesh = 0;
    double hillRecordDistance = 0, cachedRecordDistance = 0;
    std::string recordMarkerHill;
    std::array<sjp::gfx::Id, HILL_COUNT> landscapes{};
    std::array<uint64_t, HILL_COUNT> landscapeUse{};
    std::array<size_t, HILL_COUNT> landscapeBytes{};
    static constexpr size_t landscapeBudget = 128 * 1024 * 1024;
    uint64_t landscapeClock = 0;
    std::future<std::shared_ptr<gfx::PreparedList>> preparingLandscape;
    size_t preparingVenue = 0;
    // Only clear morning, day and evening sun casts baked scenery shadows.
    // Weather merely selects whether they are shown, so it never rebuilds them.
    static constexpr int sunShadowVariants = 3;
    struct SceneryShadow {
        gfx::Id mesh = 0;
        std::array<gfx::Id, 2> textures{};
        size_t bytes = 0;
        uint64_t use = 0;
        std::shared_ptr<const visual::SunShadowField> field;
    };
    std::array<std::array<SceneryShadow, sunShadowVariants>, HILL_COUNT> sceneryShadowMeshes{};
    std::future<std::shared_ptr<gfx::PreparedList>> preparingSceneryShadow;
    size_t preparingShadowVenue = 0;
    int preparingShadowVariant = -1;
    std::array<gfx::Id, 2> preparingShadowTextures{};
    std::shared_ptr<visual::SunShadowField> preparingShadowField;
    uint64_t sceneryShadowClock = 0;
    // Low snow banks rise above the snow mesh. Retain them once per venue so
    // baked sun shadows can darken them as well as the surface beneath.
    std::array<std::vector<std::array<Vec3, 6>>, HILL_COUNT> snowBankReceivers;
    std::array<bool, HILL_COUNT> snowBanksRecorded{};
    bool sceneryShadows = true, sceneryShadowsReady = false;
    bool terrainReady = false;
    std::string sceneHill;
    struct PresentedScene {
        size_t venue;
        Frame frame, ghostFrame;
        double time;
        int camera;
        bool panorama, ghost;
    };
    std::optional<PresentedScene> presentedScene;
    std::array<bool, HILL_COUNT> venueInitialized{};
    std::array<gfx::Id, JUMPER_APPEARANCES.size()> previewMeshes{};
    gfx::Id cartoonPreviewHead = 0;
    gfx::Id heroPreviewHead = 0;
    struct ShadowCache;
    std::unique_ptr<ShadowCache> shadowCache;
    struct ContactCache {
        bool valid = false;
        std::string hill;
        JumperAppearance appearance{};
        Frame input{}, output{};
    };
    std::array<ContactCache, 2> contactCache;
    Vec3 eye{}, target{};
    bool cameraInit = false;
    double cameraTime = 0;
    double cameraProgress = 0;
    std::array<visual::ChaseCameraPath, HILL_COUNT> chasePaths;
    std::array<visual::InrunLayout, HILL_COUNT> inrunLayouts;
    std::array<visual::GrandstandLayout, HILL_COUNT> grandstandLayouts;
    Font loadFont(const std::string &);
    void loadFlagAtlas(const std::string &);
    void loadMountainPanorama(const std::string &);
    void terrain(const Hill &);
    bool prepareTerrain(const Hill &);
    void buildTerrain(const Hill &, size_t venue);
    void installTerrain();
    bool prepareSceneryShadows(const Hill &, size_t venue);
    void installSceneryShadows();
    void buildSceneryShadows(const Hill &, size_t venue, visual::EnvironmentLight, std::array<gfx::Id, 2> textures,
                             visual::SunShadowField &field);
    void releaseSceneryShadow(SceneryShadow &);
    // The baked shadow drawn for this venue's current conditions, if any.
    const SceneryShadow *activeSceneryShadow(size_t venue) const;
    void drawScene(const Hill &, const Frame &, double time, int camera, bool panorama, bool ghost,
                   const Frame *ghostFrame);
    Frame cachedAthleteFrame(const Hill &, const Frame &, bool ghost);
    void shadow(const Hill &, const Frame &, const visual::FloodlightField &, const visual::InrunLayout &);
    void hillRecordMarker(const Hill &);
    void inrunStructure(const Hill &, const visual::InrunLayout &, visual::SnowAccumulation &);
    void startSignalHousing(const Hill &);
    void startSignalLenses(const Hill &, const Frame &);
    void scenery(const Hill &, const Frame &, double time);
    void athlete(const Hill &, const Frame &, bool ghost);
    void jumperPreviewGeometry(JumperAppearance);
    void effects(const Hill &, const Frame &, double time, bool showWeather);
    void lampGlows(const Hill &, const Frame &, const visual::InrunLayout &);
    void skiTracks(const Hill &, const Frame &);
    void floodlightBeams(const Hill &);
    std::unique_ptr<visual::SkiTrackCache> skiTrackCache;
    const std::vector<Frame> *trackHistory = nullptr;
    int trackHistoryHill = -1;
    bool backdropMist = true;
    bool snowGlitter = true;
    bool lampGlow = true;
    bool snowfall = false;
    environment::Conditions sceneEnvironment;
    visual::EnvironmentLight lighting = visual::environmentLight({});
    uint32_t snowfallSeed = 0;
    struct VisualParticle {
        Vec3 origin, velocity;
        double born = 0, lifetime = 1, size = .04;
    };
    std::vector<VisualParticle> snowParticles;
    Frame lastEffectFrame{};
    std::string effectHill;
    bool effectsInitialized = false;
    bool synchronousRendering = false;
    JumperAppearance jumperAppearance = JumperAppearance::Pro;
};
} // namespace sjp
