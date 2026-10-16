#include "competition.h"
#include "renderer.h"
#include "storage.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sys/resource.h>
#include <thread>
#include <vector>

// Explicitly invoked diagnostics only: never registered as an automatic GPU test.
// Personal saves are read, never written. The wrapper records kernel/GPU events.
namespace {
struct Options {
    std::string cup, replay, assets, expectedRenderer, expectedCompiler;
    int seconds = 0, samples = 4, fps = 60;
    bool synchronize = false, culling = true;
};
Options options(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (++i == argc)
                throw std::runtime_error("Missing value for " + a);
            return argv[i];
        };
        if (a == "--cup")
            o.cup = value();
        else if (a == "--replay")
            o.replay = value();
        else if (a == "--assets")
            o.assets = value();
        else if (a == "--expect-renderer")
            o.expectedRenderer = value();
        else if (a == "--expect-compiler")
            o.expectedCompiler = value();
        else if (a == "--seconds")
            o.seconds = std::stoi(value());
        else if (a == "--samples")
            o.samples = std::stoi(value());
        else if (a == "--fps")
            o.fps = std::stoi(value());
        else if (a == "--sync")
            o.synchronize = true;
        else if (a == "--no-culling")
            o.culling = false;
        else
            throw std::runtime_error("Unknown argument: " + a);
    }
    if (o.cup.empty() || o.replay.empty() || o.assets.empty() || o.expectedRenderer.empty() ||
        o.seconds < 0 || o.seconds > 600 || (o.samples != 0 && o.samples != 4) || o.fps < 1 || o.fps > 60)
        throw std::runtime_error("Specify --cup, --replay, --assets, --expect-renderer; "
                                 "seconds 0..600, samples 0/4, fps 1..60");
    return o;
}
struct Context {
    SDL_Window *window = nullptr;
    explicit Context(int) {
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("The Perfect V Vulkan diagnostics", 1440, 900,
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!window) {
            const std::string error = SDL_GetError();
            SDL_Quit();
            throw std::runtime_error(error);
        }
        std::cout << "video=" << SDL_GetCurrentVideoDriver() << " backend=Vulkan\n";
    }
    ~Context() {
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

sjp::Frame sample(const std::vector<sjp::Frame> &frames, double fraction) {
    const double position = std::clamp(fraction, 0.0, 1.0) * (frames.size() - 1);
    const size_t i = size_t(position);
    return sjp::interpolateFrame(frames[i], frames[std::min(i + 1, frames.size() - 1)], position - i);
}
int run(const Options &o) {
    sjp::Competition cup;
    std::string error;
    if (!sjp::loadCompetition(o.cup, cup, &error))
        throw std::runtime_error(error);
    sjp::Replay replay;
    if (!sjp::loadReplay(o.replay, replay) || replay.frames.empty())
        throw std::runtime_error("Invalid replay");
    std::cout << "cup_event=" << cup.event() + 1 << '/' << cup.eventCount() << " round=" << cup.round() + 1
              << " hill=" << cup.hill() << " seed=" << cup.preparedJumpSeed()
              << " replay_frames=" << replay.frames.size() << '\n';
    std::vector<std::vector<sjp::Frame>> jumps(sjp::HILL_COUNT);
    for (int hi = 0; hi < int(sjp::HILL_COUNT); ++hi) {
        sjp::Simulation s(hi, 0);
        s.resetOriginal(hi, 12345, 98765);
        s.setWind(0, 0);
        for (int tick = 0;
             tick < 20000 && s.phase != sjp::Phase::Finished && s.phase != sjp::Phase::Disqualified; ++tick)
            s.step(sjp::ORIGINAL_TICK, sjp::autopilot(s));
        if (s.phase != sjp::Phase::Finished || s.frames.size() < 2)
            throw std::runtime_error("Probe trajectory did not finish for hill " + std::to_string(hi));
        jumps[hi] = std::move(s.frames);
    }
    Context context(o.samples);
    sjp::Renderer renderer(context.window, o.assets, o.samples == 0);
    renderer.setSynchronousRendering(o.synchronize);
    sjp::gfx::setFrustumCulling(o.culling);
    if (renderer.device.find(o.expectedRenderer) == std::string::npos ||
        (!o.expectedCompiler.empty() && renderer.device.find(o.expectedCompiler) == std::string::npos))
        throw std::runtime_error("Unexpected renderer/compiler: " + renderer.device);
    int samples = 0;
    sjp::gfx::getIntegerv(sjp::gfx::SAMPLES, &samples);
    if (samples != o.samples)
        throw std::runtime_error("Requested MSAA was not obtained");
    std::cout << "renderer=" << renderer.device << " samples=" << samples << " sync=" << o.synchronize
              << " cap_fps=" << o.fps << " seconds=" << o.seconds << " culling=" << o.culling << '\n';
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    size_t draws = 0;
    auto draw = [&](int hi, const sjp::Frame &f, int camera, bool panorama) {
        renderer.resetCamera();
        renderer.begin(sjp::hills()[hi], f, f.elapsed, camera, panorama);
        if (!renderer.sceneReady()) {
            renderer.waitForScene();
            renderer.begin(sjp::hills()[hi], f, f.elapsed, camera, panorama);
        }
        renderer.overlay();
        renderer.text(sjp::hills()[hi].name, 20, 20, 20);
        renderer.finish();
        if (o.synchronize)
            sjp::gfx::finish();
        ++draws;
        next = std::max(next + std::chrono::microseconds(1000000 / o.fps), Clock::now());
        std::this_thread::sleep_until(next);
    };
    for (int hi = 0; hi < int(sjp::HILL_COUNT); ++hi) {
        renderer.setJumpWeather(hi == cup.hill() ? cup.preparedJumpSeed() : 12345 + hi);
        draw(hi, jumps[hi].front(), 0, true);
        for (int camera = 0; camera < 3; ++camera) {
            draw(hi, sample(jumps[hi], .4), camera, false);
            draw(hi, sample(jumps[hi], .8), camera, false);
        }
        std::cout << "hill=" << hi + 1 << "/32 short_check=OK\n";
    }
    for (double fraction : {0.0, .5, 1.0})
        draw(replay.hill, sample(replay.frames, fraction), 0, false);
    const auto started = Clock::now();
    double lastLog = -3;
    int priorHill = -1;
    while (o.seconds) {
        const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        if (elapsed >= o.seconds)
            break;
        // First 160 seconds visit all 32 hills; remaining time replays the affected venue.
        const int hi = elapsed < 160 ? std::min(31, int(elapsed / 5)) : replay.hill;
        if (hi != priorHill) {
            renderer.setJumpWeather(hi == cup.hill() ? cup.preparedJumpSeed() : 12345 + hi);
            priorHill = hi;
        }
        const auto &frames = elapsed < 160 ? jumps[hi] : replay.frames;
        const double cycle =
            elapsed < 160 ? std::fmod(elapsed, 5.0) / 5.0 : std::fmod(elapsed - 160, 17.0) / 17.0;
        const int camera = elapsed < 160 ? 0 : int((elapsed - 160) / 34) % 3;
        const auto f = sample(frames, cycle);
        draw(hi, f, camera, false);
        if (elapsed - lastLog >= 2) {
            struct rusage usage{};
            getrusage(RUSAGE_SELF, &usage);
            const auto stats = sjp::gfx::frameStats();
            std::cout << "elapsed=" << elapsed << " draws=" << draws << " hill=" << hi << " phase=" << f.phase
                      << " camera=" << camera << " max_rss_kib=" << usage.ru_maxrss
                      << " cached_vertices=" << stats.cachedVertices << " cached_indices=" << stats.cachedIndices
                      << " submitted_indices=" << stats.submittedIndices << " culled_indices=" << stats.culledIndices
                      << " dynamic_vertices=" << stats.dynamicVertices << " draw_calls=" << stats.drawCalls << '\n';
            lastLog = elapsed;
        }
    }
    sjp::gfx::finish();
    std::cout << "PASS draws=" << draws << " all_hills=32 seconds=" << o.seconds << '\n';
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    try {
        return run(options(argc, argv));
    } catch (const std::exception &e) {
        std::cerr << "Render probe: " << e.what() << '\n';
        return 1;
    }
}
