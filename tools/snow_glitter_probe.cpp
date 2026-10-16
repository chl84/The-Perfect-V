// Explicit Vulkan visual regression probe. All artifacts live in the supplied
// output directory; no saves are read or written.
#include "renderer.h"
#include "visual_snow_glitter.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using namespace sjp;
namespace fs = std::filesystem;
struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb;
};
Image read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    Image image;
    std::string magic;
    int maximum;
    in >> magic >> image.w >> image.h >> maximum;
    if (!in || magic != "P6" || maximum != 255)
        throw std::runtime_error("Invalid capture " + path.string());
    in.get();
    image.rgb.resize(size_t(image.w) * image.h * 3);
    in.read(reinterpret_cast<char *>(image.rgb.data()), std::streamsize(image.rgb.size()));
    if (!in)
        throw std::runtime_error("Truncated capture");
    return image;
}
struct Difference { size_t pixels = 0; double mean = 0; int maximum = 0; };
Difference difference(const Image &a, const Image &b) {
    if (a.w != b.w || a.h != b.h) throw std::runtime_error("Capture dimensions differ");
    Difference result;
    for (size_t i = 0; i < a.rgb.size(); i += 3) {
        int delta = 0;
        for (size_t c = 0; c < 3; ++c) {
            int d = std::abs(int(a.rgb[i + c]) - b.rgb[i + c]);
            result.mean += d;
            delta = std::max(delta, d);
        }
        result.maximum = std::max(result.maximum, delta);
        result.pixels += delta != 0;
    }
    result.mean /= a.rgb.size();
    return result;
}
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
void valleyEdgeChecks(Renderer &r, const fs::path &out) {
    using namespace gfx;
    for (int side : {-1, 1}) {
        beginFrame(r.width, r.height);
        viewport(0, 0, r.width, r.height);
        clearColor(0, 0, 0, 1); clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
        disable(DEPTH_TEST); disable(LIGHTING); disable(FOG); disable(TEXTURE_2D);
        enable(BLEND); blendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        backdropHaze({}, {});
        matrixMode(PROJECTION); loadIdentity(); ortho(0, r.width, 0, r.height, -1, 1);
        matrixMode(MODELVIEW); loadIdentity();
        const float edge[]{195, 200, 1, -1000}, noEdge[]{0, 0, 0, 0};
        materialfv(FRONT_AND_BACK, VALLEY_EDGE, edge);
        // Deliberately use a coarse 20-metre quad. The true fade must still
        // occupy just its last 5 metres, irrespective of vertex interpolation.
        begin(QUADS);
        for (const auto &p : std::array<std::array<double, 2>, 4>{{
                 {0, 0}, {double(r.width), 0}, {double(r.width), double(r.height)}, {0, double(r.height)}}}) {
            const double t = p[0] / r.width;
            color4f(1, 1, 1, float(1 - t));
            texCoord2d(0, side * (180 + 20 * t) / 13);
            vertex2f(float(p[0]), float(p[1]));
        }
        end();
        materialfv(FRONT_AND_BACK, VALLEY_EDGE, noEdge);
        const auto path = out / (side < 0 ? "edge-negative.ppm" : "edge-positive.ppm");
        r.capture(path.string()); r.finish();
        const auto image = read(path);
        auto pixel = [&](double z) {
            const int x = std::clamp(int((z - 180) / 20 * image.w), 0, image.w - 1);
            return int(image.rgb[(image.h / 2 * image.w + x) * 3]);
        };
        require(pixel(190) == 255 && pixel(194) == 255 && pixel(195) >= 254,
                "Terrain fades before the final five metres");
        require(std::abs(pixel(197.5) - 128) <= 3 && pixel(199) < 30 && pixel(200) == 0,
                "Terrain edge must fade smoothly over exactly five metres");
    }
}
void backdropChecks(Renderer &r, const fs::path &out) {
    std::ofstream report(out / "backdrop-report.csv");
    report << "case,changed_pixels,mean_channel_delta,max_delta,foreground_changes\n";
    auto draw = [&](int hill, const Frame &f, environment::Conditions conditions,
                    int camera, bool panorama, bool enabled, const std::string &name) {
        r.setBackdropMist(enabled);
        r.setEnvironment(conditions, true);
        r.resetCamera();
        r.begin(hills()[hill], f, f.elapsed, camera, panorama);
        if (!r.sceneReadyFor(hills()[hill])) {
            r.waitForScene(); r.resetCamera();
            r.begin(hills()[hill], f, f.elapsed, camera, panorama);
        }
        r.overlay();
        r.rect(25, 20, 210, 42, {.04f, .07f, .10f, 1});
        r.text("The Perfect V", 40, 30, 24);
        r.jumperPreview(JumperAppearance::Pro, 215, 55, 12);
        const auto path = out / (name + (enabled ? "-on.ppm" : "-off.ppm"));
        r.capture(path.string()); r.finish();
        return read(path);
    };
    auto check = [&](int hill, const Frame &f, environment::Conditions conditions,
                     int camera, bool panorama, const std::string &name) {
        const auto off = draw(hill, f, conditions, camera, panorama, false, name);
        const auto on = draw(hill, f, conditions, camera, panorama, true, name);
        const auto d = difference(off, on);
        size_t foreground = 0;
        // A small opaque near-snow patch at bottom centre must remain clear.
        for (int y = on.h * 9 / 10; y < on.h; ++y)
            for (int x = on.w * 48 / 100; x < on.w * 52 / 100; ++x)
                for (int c = 0; c < 3; ++c)
                    foreground += off.rgb[(y * on.w + x) * 3 + c] != on.rgb[(y * on.w + x) * 3 + c];
        report << name << ',' << d.pixels << ',' << d.mean << ',' << d.maximum << ',' << foreground << '\n';
        report.flush();
        if (!panorama) require(foreground == 0, "Backdrop haze changed near snow in the jumping corridor");
        require(d.mean < 35, "Backdrop mist overwhelms the scene");
        if (conditions.time == environment::TimeOfDay::Day && conditions.weather == environment::Weather::Sunny)
            require(d.pixels > 500, "Backdrop transition veil is missing");
        // The opaque UI badge, including its 3D preview, must remain identical.
        const int uiLeft = int(std::ceil(r.offsetX + 25 * r.uiScale)) + 2;
        const int uiRight = int(std::floor(r.offsetX + 235 * r.uiScale)) - 2;
        const int uiTop = int(std::ceil(r.offsetY + 20 * r.uiScale)) + 2;
        const int uiBottom = int(std::floor(r.offsetY + 62 * r.uiScale)) - 2;
        for (int y = uiTop; y < uiBottom; ++y)
            for (int x = uiLeft; x < uiRight; ++x)
                for (int c = 0; c < 3; ++c)
                    require(off.rgb[(y * on.w + x) * 3 + c] == on.rgb[(y * on.w + x) * 3 + c],
                            "Backdrop material leaked into sky/UI");
    };
    std::vector<int> venues{7};
    for (int i = 0; i < int(HILL_COUNT); ++i) if (i != 7) venues.push_back(i);
    for (int hill : venues) {
        Simulation sim(hill, 0); sim.resetOriginal(hill, 12345, 98765);
        for (int tick = 0; tick < 20000 && sim.phase != Phase::Flight &&
             sim.phase != Phase::Finished && sim.phase != Phase::Disqualified; ++tick)
            sim.step(ORIGINAL_TICK, autopilot(sim));
        require(sim.phase == Phase::Flight, "Backdrop probe jump never reached flight");
        for (int tick = 0; tick < 40; ++tick) sim.step(ORIGINAL_TICK, autopilot(sim));
        const auto f = sim.frame();
        const auto name = "hill-" + std::to_string(hill);
        check(hill, f, {}, 0, false, name + "-flight");
        check(hill, f, {}, 0, true, name + "-panorama");
        if (hill == 7) {
            check(hill, f, {}, 1, false, name + "-chase");
            check(hill, f, {}, 2, false, name + "-broadcast");
            for (int time = 0; time < 4; ++time)
                for (int weather = 0; weather < 4; ++weather) {
                    environment::Conditions conditions{environment::TimeOfDay(time), environment::Weather(weather), 76543};
                    check(hill, f, conditions, 0, false,
                          name + "-time" + std::to_string(time) + "-weather" + std::to_string(weather));
                }
        }
    }
    std::cout << r.device << ": backdrop mist passed all 32 venues, cameras and 16 time/weather cases\n";
}
void lookAt(Vec3 eye, Vec3 center) {
    Vec3 f = (center - eye).normalized(), s = cross(f, {0, 1, 0}).normalized(), u = cross(s, f);
    float matrix[]{float(s.x), float(u.x), float(-f.x), 0,
                   float(s.y), float(u.y), float(-f.y), 0,
                   float(s.z), float(u.z), float(-f.z), 0, 0, 0, 0, 1};
    gfx::multMatrixf(matrix);
    gfx::translated(-eye.x, -eye.y, -eye.z);
}
void patch(Renderer &r, double distance, double cameraX, float sunlight, bool material) {
    using namespace gfx;
    beginFrame(r.width, r.height);
    viewport(0, 0, r.width, r.height);
    clearColor(.12f, .19f, .26f, 1);
    clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
    matrixMode(PROJECTION); loadIdentity();
    const double top = .25 * std::tan(48 * PI / 360), aspect = double(r.width) / r.height;
    frustum(-top * aspect, top * aspect, -top, top, .25, 2000);
    matrixMode(MODELVIEW); loadIdentity();
    lookAt({cameraX, distance * .45, distance}, {0, 0, 0});
    enable(DEPTH_TEST); depthMask(true); enable(LIGHTING); enable(LIGHT0);
    disable(LIGHT1); disable(FOG); disable(TEXTURE_2D); disable(BLEND);
    float ambient[]{.47f, .47f, .47f, 1}, sun[]{.32f, .32f, .32f, 1};
    float position[]{-.5f, 1, .625f, 0}, black[]{0, 0, 0, 0};
    lightModelfv(LIGHT_MODEL_AMBIENT, ambient);
    lightfv(LIGHT0, AMBIENT, black); lightfv(LIGHT0, DIFFUSE, sun);
    lightfv(LIGHT0, POSITION, position); lightfv(LIGHT0, SPECULAR, black);
    materialfv(FRONT_AND_BACK, EMISSION, black);
    materialfv(FRONT_AND_BACK, SPECULAR, black);
    snowSunlight(sunlight);
    // Both halves have identical appearance; only the left is snow-tagged.
    for (int half = 0; half < 2; ++half) {
        materialf(FRONT_AND_BACK, SNOW_GLITTER, material && half == 0 ? 1 : 0);
        normal3d(0, 1, 0); color4f(.85f, .89f, .93f, 1);
        begin(QUADS);
        for (Vec3 p : {Vec3{half ? 0.0 : -500.0, 0, -500},
                       Vec3{half ? 0.0 : -500.0, 0, 500},
                       Vec3{half ? 500.0 : 0.0, 0, 500},
                       Vec3{half ? 500.0 : 0.0, 0, -500}}) {
            texCoord2d(p.x / 13, p.z / 13); vertex3d(p.x, p.y, p.z);
        }
        end();
    }
    materialf(FRONT_AND_BACK, SNOW_GLITTER, 0);
}
int run(const fs::path &out, const std::string &assets, bool timingOnly, bool backdropOnly) {
    fs::create_directories(out);
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    auto *window = SDL_CreateWindow("Snow glitter visual tests", 1280, 720, SDL_WINDOW_HIDDEN);
    if (!window) throw std::runtime_error(SDL_GetError());
    {
        Renderer r(window, assets, 4, false, true);
        r.setSynchronousRendering(true);
        if (backdropOnly) {
            SDL_GetWindowSizeInPixels(window, &r.width, &r.height);
            valleyEdgeChecks(r, out);
            backdropChecks(r, out);
        } else {
        std::ofstream report;
        if (!timingOnly) report.open(out / "report.csv");
        if (report) report << "case,changed_pixels,mean_channel_delta,max_channel_delta\n";
        auto compare = [&](const std::string &name, const Image &a, const Image &b) {
            const auto d = difference(a, b);
            report << name << ',' << d.pixels << ',' << d.mean << ',' << d.maximum << '\n';
            report.flush();
            return d;
        };
        auto patchCapture = [&](const std::string &name, double distance, double x, float sun, bool material) {
            SDL_GetWindowSizeInPixels(window, &r.width, &r.height);
            patch(r, distance, x, sun, material);
            r.capture((out / (name + ".ppm")).string()); r.finish();
            return read(out / (name + ".ppm"));
        };
        if (!timingOnly) {
            for (int samples : {0, 2, 4, 8}) {
                if (!gfx::supportsSampleCount(samples)) continue;
                gfx::configureGraphics(samples, false);
                for (int width : {960, 1920}) {
                    SDL_SetWindowSize(window, width, width * 9 / 16);
                    const std::string label = "patch-" + std::to_string(width) + "-msaa" + std::to_string(samples);
                    auto off = patchCapture(label + "-off", 9, 0, 1, false);
                    auto on = patchCapture(label + "-on", 9, 0, 1, true);
                    const auto d = compare(label, off, on);
                    require(d.pixels > 20 && d.maximum >= 8, "Close sunlit snow has no visible glints");
                    require(d.mean < 1.0 && d.pixels < size_t(width * width * 9 / 16 / 5),
                            "Glitter is not sparse/subtle");
                    for (int y = 0; y < on.h; ++y)
                        for (int x = on.w / 2 + 3; x < on.w; ++x)
                            for (int c = 0; c < 3; ++c)
                                require(off.rgb[(y * on.w + x) * 3 + c] == on.rgb[(y * on.w + x) * 3 + c],
                                        "Snow glints leaked onto the other material");
                    auto repeat = patchCapture(label + "-repeat", 9, 0, 1, true);
                    require(compare(label + "-stationary", on, repeat).pixels == 0, "Stationary snow flickers");
                    auto shiftOff = patchCapture(label + "-shift-off", 9, .025, 1, false);
                    auto shift = patchCapture(label + "-shift", 9, .025, 1, true);
                    require(compare(label + "-moving", shiftOff, shift).pixels > 20, "Moving view lost all glints");
                    auto dark = patchCapture(label + "-dark", 9, 0, 0, true);
                    require(compare(label + "-no-sun", off, dark).pixels == 0, "Glints ignore sunlight");
                    auto farOff = patchCapture(label + "-far-off", 900, 0, 1, false);
                    auto far = patchCapture(label + "-far", 900, 0, 1, true);
                    require(compare(label + "-far", farOff, far).pixels == 0, "Unresolved distant crystals shimmer");
                }
            }
        }
        SDL_SetWindowSize(window, 1280, 720);
        gfx::configureGraphics(4, false);
        Simulation sim(7, 0); sim.resetOriginal(7, 12345, 98765);
        for (int tick = 0; tick < 20000 && sim.phase != Phase::Flight &&
             sim.phase != Phase::Finished && sim.phase != Phase::Disqualified; ++tick)
            sim.step(ORIGINAL_TICK, autopilot(sim));
        require(sim.phase == Phase::Flight, "Probe jump never reached flight");
        for (int i = 0; i < 40; ++i) sim.step(ORIGINAL_TICK, autopilot(sim));
        const Frame f = sim.frame();
        Frame displayed = f;
        auto scene = [&](bool enabled, environment::Conditions conditions, int camera) {
            r.setSnowGlitter(enabled); r.setEnvironment(conditions, true); r.resetCamera();
            r.begin(hills()[7], displayed, f.elapsed, camera, false);
            if (!r.sceneReady()) {
                r.waitForScene(); r.resetCamera(); r.begin(hills()[7], displayed, f.elapsed, camera, false);
            }
            // Include UI to ensure scene material state cannot spill into text.
            r.overlay(); r.text("The Perfect V", 40, 30, 24);
        };
        if (!timingOnly) {
            size_t sunnyPixels = 0;
            for (int time = 0; time < 4; ++time) {
                for (int weather = 0; weather < 4; ++weather) {
                    const environment::Conditions conditions{environment::TimeOfDay(time), environment::Weather(weather), 76543};
                    const std::string name = "scene-time" + std::to_string(time) + "-weather" + std::to_string(weather);
                    scene(false, conditions, 0); r.capture((out / (name + "-off.ppm")).string()); r.finish();
                    scene(true, conditions, 0); r.capture((out / (name + "-on.ppm")).string()); r.finish();
                    const auto d = compare(name, read(out / (name + "-off.ppm")), read(out / (name + "-on.ppm")));
                    if (time == 3 || weather != 0)
                        require(d.pixels == 0, "Night/cloud/fog scene has solar glints");
                    else sunnyPixels += d.pixels;
                }
            }
            require(sunnyPixels > 20, "Actual sunny venues have no glints");
            for (int camera = 0; camera < 3; ++camera) {
                scene(true, {}, camera); r.capture((out / ("camera-" + std::to_string(camera) + ".ppm")).string()); r.finish();
            }
            // A slow camera traverse for frame-by-frame inspection. Fixed replay
            // time freezes animation: only the viewpoint/surface position changes.
            Image previous;
            for (int frame = 0; frame < 24; ++frame) {
                displayed = f;
                displayed.x += frame * .035;
                scene(true, {}, 0);
                const auto path = out / ("motion-" + std::to_string(100 + frame) + ".ppm");
                r.capture(path.string()); r.finish();
                const auto current = read(path);
                if (frame)
                    require(compare("motion-" + std::to_string(frame), previous, current).mean < 8,
                            "Small camera movement caused a large frame flash");
                previous = current;
            }
        }
        displayed = f;
        // Same warm scene and forced completion for both settings. Software GPU
        // times are diagnostic, never a hardware FPS promise.
        // Compile pipelines and warm the cached venue before recording times.
        for (bool enabled : {false, true}) {
            scene(enabled, {}, 0);
            gfx::renderOffscreen();
            r.finish();
        }
        std::ofstream timings(out / "timings.csv");
        timings << "enabled,median_ms,p95_ms\n";
        for (bool enabled : {false, true, true, false}) {
            std::vector<double> samples;
            for (int i = 0; i < 24; ++i) {
                auto start = std::chrono::steady_clock::now();
                scene(enabled, {}, 0); gfx::renderOffscreen(); r.finish();
                if (i >= 4) samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            }
            std::sort(samples.begin(), samples.end());
            timings << enabled << ',' << samples[samples.size() / 2] << ',' << samples[size_t(samples.size() * .95)] << '\n';
        }
        std::cout << r.device << (timingOnly ? ": isolated offscreen timings completed\n" :
            ": snow material/resolutions/MSAA/stationary/moving/far/16 weather cases passed\n");
        }
    }
    SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 3 && !(argc == 4 && (std::string(argv[3]) == "--timing-only" || std::string(argv[3]) == "--backdrop-mist")))
            throw std::runtime_error("Usage: snow-glitter-probe output-directory assets-directory [--timing-only|--backdrop-mist]");
        return run(argv[1], argv[2], argc == 4 && std::string(argv[3]) == "--timing-only",
                   argc == 4 && std::string(argv[3]) == "--backdrop-mist");
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
