#include "audio.h"
#include "competition.h"
#include "original_replay.h"
#include "physics.h"
#include "renderer.h"
#include "jump_feedback.h"
#include "presentation_timing.h"
#include "storage.h"
#include "visual_weather.h"
#include "draw_commands.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace sjp;
namespace {
uint32_t originalClockSeed() {
    const auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    auto hundredths =
        (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000) / 10;
    return originalWindSeedFromDosTime(uint8_t(local.tm_hour), uint8_t(local.tm_min), uint8_t(local.tm_sec),
                                       uint8_t(hundredths));
}
std::string number(double n, int digits = 1) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(digits) << n;
    return o.str();
}
// Coordinates share the overlay's 1440 x 900 reference canvas.
struct ReplayLayout {
    static constexpr float left = 38, right = 1402, top = 35, bottom = 865;
    static constexpr float controlsHeight = 58, controlsTop = bottom - controlsHeight;
    static constexpr float seekX = 270, seekWidth = 930;
};
enum class Screen {
    Menu,
    Hills,
    Help,
    Settings,
    Graphics,
    Records,
    Playing,
    Paused,
    Result,
    Standings,
    Replay,
    CupSetup,
    JumpReady,
    ReplayFiles,
    SkiJumper
};
enum class Action {
    None,
    Practice,
    Cup,
    Hills,
    Help,
    Settings,
    Graphics,
    Fullscreen,
    Antialiasing,
    Vsync,
    Records,
    Quit,
    Back,
    SelectHill,
    HillPage,
    Resume,
    Again,
    Next,
    Replay,
    ReplayPlay,
    ReplaySlow,
    ReplayFast,
    ReplaySeek,
    Camera,
    Sound,
    SensitivityDown,
    SensitivityUp,
    SensitivityReset,
    Wind,
    TimeOfDay,
    Weather,
    EnvironmentReset,
    Ghost,
    CupSetupMode,
    CupSetupTab,
    CupSetupPage,
    CupPlayerName,
    CupPlayerCountry,
    CupPlayerType,
    CupPlayerLevel,
    CupAddPlayer,
    CupRemovePlayer,
    CupHillToggle,
    CupHillsAll,
    CupSeasons,
    CupCpuView,
    CupStart,
    CupResume,
    CupTeamName,
    CupTeamCountry,
    CupTeamCount,
    StartTurn,
    ReplayBrowse,
    ReplayBrowsePage,
    ReplayFile,
    ReplayParent,
    ReplayExport,
    ReplayExportNative,
    SkiJumper,
    SelectJumper
};
struct Button {
    float x, y, w, h;
    Action action;
    int value = 0;
};
class App {
    SDL_Window *window = nullptr;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Audio> audio;
    Screen screen = Screen::Menu, replayReturn = Screen::Menu, helpReturn = Screen::Menu;
    Simulation sim{2, 2};
    Records records;
    Replay lastReplay;
    bool hasReplay = false, ghost = false;
    std::string dataDir, captureDir;
    bool running = true, fullscreen = false, competition = false, smoke = false, hidden = false,
         storageError = false;
    bool mouseLeft = false, mouseRight = false, newRecord = false, replayPlaying = true,
         keyboardTakeoff = false;
    float mouseX = -100, mouseY = -100;
    double time = 0, accumulator = 0, replayTime = 0, replaySpeed = 1;
    int mouseDy = 0;
    double mouseDyFraction = 0;
    Input pending;
    std::vector<Button> buttons;
    static constexpr int HILLS_PER_PAGE = 8;
    int hillPage = 0;
    int selectedHill = 2, camera = 0, round = 0, cupEvent = 0, windMode = 0;
    Competition cup;
    CupSetup worldSetup = defaultCupSetup(), teamSetup = defaultTeamCupSetup();
    bool setupTeam = false, hasCupSave = false;
    int setupTab = 0, setupPage = 0, standingsPage = 0, editingName = -1;
    std::string editText, message, replayExportError, completedName, completedCountry;
    bool editingCountry = false;
    int completedRound = 0, completedEvent = 0;
    std::filesystem::path replayDirectory;
    std::vector<std::filesystem::path> replayFiles;
    int replayPage = 0;
    OriginalReplayData originalReplayData;
    bool canExportOriginal = false;
    std::string replayArgument;
    OriginalWind sessionWeather;
    std::array<float, 2> sessionAuxiliary{};
    int weatherHill = -1;
    environment::Conditions activeEnvironment;
    environment::Conditions replayEnvironment;
    visual::JumpFeedback completedFeedback;
    uint32_t visualJumpSerial = 0;
    bool synchronousRendering = false, noMsaa = false, gpuValidation = false;
    bool smokeMouseCapture = false;
    size_t mouseCaptureTransitions = 0;

  public:
    App(int argc, char **argv) {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--smoke-test")
                smoke = true;
            else if (a == "--smoke-mouse-capture")
                smoke = smokeMouseCapture = true;
            else if (a == "--hidden")
                hidden = true;
            else if (a == "--graphics-compat")
                synchronousRendering = true;
            else if (a == "--vulkan") {
                // Accepted explicitly by the separate Vulkan launcher.
            } else if (a == "--gpu-validation")
                gpuValidation = true;
            else if (a == "--sync-rendering")
                synchronousRendering = true;
            else if (a == "--no-msaa")
                noMsaa = true;
            else if (a == "--capture-dir" && i + 1 < argc)
                captureDir = argv[++i];
            else if (a == "--data-dir" && i + 1 < argc)
                dataDir = argv[++i];
            else if (a == "--replay" && i + 1 < argc)
                replayArgument = argv[++i];
            else if (a == "--help") {
                std::cout << "The Perfect V\n  --smoke-test     automated complete jump and render "
                             "validation\n  --hidden         hidden window (use with --smoke-test)\n  "
                             "--capture-dir P  write menu, flight and result images (PPM)\n  --data-dir P    "
                             " use a separate records and replay directory\n  --replay FILE     open an "
                             "original .RPL or native .pvr replay\n"
                             "  --smoke-mouse-capture  fullscreen test of mouse capture and cup clicks\n"
                             "  --graphics-compat  wait for each submitted Vulkan frame\n"
                             "  --sync-rendering   wait for each submitted frame\n"
                             "  --vulkan          use the native Vulkan renderer (default)\n"
                             "  --gpu-validation  enable SDL GPU/Vulkan validation\n"
                             "  --no-msaa          disable multisample antialiasing\n";
                running = false;
                return;
            } else
                throw std::runtime_error("Unknown argument: " + a);
        }
        if (smokeMouseCapture && hidden)
            throw std::runtime_error("Mouse capture testing requires a visible window");
        struct StartupCleanup {
            App &app;
            bool complete = false;
            ~StartupCleanup() {
                if (complete)
                    return;
                app.audio.reset();
                app.renderer.reset();
                if (app.window)
                    SDL_DestroyWindow(app.window);
                app.window = nullptr;
                SDL_Quit();
            }
        } startup{*this};
        // Keep original mouse counts independent of DPI/render size and the
        // desktop acceleration curve. Sensitivity is applied once by physics.
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0");
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE, "1");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
            throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("The Perfect V", smoke ? 1920 : 1440, smoke ? 1080 : 900,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                      (hidden ? SDL_WINDOW_HIDDEN : 0));
        if (!window)
            throw std::runtime_error(std::string("Window creation failed: ") + SDL_GetError());
        SDL_SetWindowMinimumSize(window, 960, 600);
        std::string assets;
        const char *base = SDL_GetBasePath();
        std::filesystem::path executableDir = base ? base : ".";
        for (auto candidate : {executableDir / "assets", executableDir / "../assets",
                               executableDir / "../share/the-perfect-v/assets",
                               executableDir / "../share/the-perfect-v", std::filesystem::path(SJP_ASSET_DIR)}) {
            if (std::filesystem::exists(candidate / "fonts/Interim-Regular.ttf")) {
                assets = candidate.string();
                break;
            }
        }
        if (assets.empty())
            throw std::runtime_error("Game fonts are missing. Keep the assets directory with the game.");
        if (dataDir.empty()) {
            char *pref = SDL_GetPrefPath("", "the-perfect-v");
            if (pref) {
                dataDir = (std::filesystem::path(pref) / "saves").string();
                SDL_free(pref);
                const auto shareDirectory = std::filesystem::path(dataDir).parent_path().parent_path();
                // Prefer the most recent namespace; never merge an older checkpoint
                // back into a save set where the player may have deleted it.
                auto legacyDirectory = shareDirectory / "ThePerfectV" / "ThePerfectV";
                if (!std::filesystem::exists(legacyDirectory))
                    legacyDirectory = shareDirectory / "SkiJumpPro" / "SkiJumpPro";
                std::string error;
                if (!importLegacySaves(legacyDirectory.string(), dataDir, &error))
                    throw std::runtime_error("Could not import previous saves: " + error);
            } else
                dataDir = (executableDir / "saves").string();
        }
        std::error_code ec;
        std::filesystem::create_directories(dataDir, ec);
        storageError = bool(ec);
        if (!captureDir.empty()) {
            std::filesystem::create_directories(captureDir, ec);
            if (ec)
                throw std::runtime_error("Could not create capture directory");
        }
        const auto originalRecords = dataDir + "/records-dsj2.dat";
        const auto oldProfileRecords = dataDir + "/records-original.dat";
        records = loadRecords(std::filesystem::exists(originalRecords)     ? originalRecords
                              : std::filesystem::exists(oldProfileRecords) ? oldProfileRecords
                                                                           : dataDir + "/records.dat");
        camera = records.camera;
        if (!smoke && records.graphics.fullscreen) {
            fullscreen = SDL_SetWindowFullscreen(window, true);
            if (!fullscreen)
                records.graphics.fullscreen = false;
        }
        renderer = std::make_unique<Renderer>(window, assets, noMsaa ? 0 : records.graphics.msaaSamples,
                                              !smoke && records.graphics.vsync, gpuValidation);
        renderer->setSynchronousRendering(synchronousRendering);
        audio = std::make_unique<Audio>(assets);
        Competition saved;
        if (loadCompetition(dataDir + "/cup.sjc", saved)) {
            hasCupSave = saved.active();
            if (saved.setup().mode == CupMode::Team)
                teamSetup = saved.setup();
            else
                worldSetup = saved.setup();
        }
        for (CupSetup *setup : {&worldSetup, &teamSetup})
            for (size_t i = 0; i < setup->participants.size(); ++i) {
                const auto &p = setup->participants[i];
                if (p.computer && (p.name.rfind("CPU ", 0) == 0 || p.name.rfind(p.country + " ", 0) == 0))
                    assignCpuIdentity(*setup, i, athleteIdentitySeed());
            }
        replayDirectory = dataDir;
        SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
        hasReplay = loadReplay(dataDir + "/latest-dsj2.pvr", lastReplay);
        if (hasReplay && !lastReplay.originalRpl.empty())
            canExportOriginal =
                decodeOriginalReplay(lastReplay.originalRpl, originalReplayData, &replayExportError);
        if (!replayArgument.empty())
            openReplayFile(replayArgument);
        const int sdlVersion = SDL_GetVersion();
        std::cout << "The Perfect V | " << renderer->device << "\nSaves: " << dataDir
                  << "\nGraphics: Vulkan, SDL " << SDL_VERSIONNUM_MAJOR(sdlVersion) << '.'
                  << SDL_VERSIONNUM_MINOR(sdlVersion) << '.' << SDL_VERSIONNUM_MICRO(sdlVersion)
                  << ", MSAA=" << sjp::gfx::sampleCount() << ", synchronous=" << synchronousRendering
                  << ", frames in flight=2"
                  << ", vsync=" << gfx::vsyncEnabled() << '\n'
                  << std::flush;
        startup.complete = true;
    }
    ~App() {
        if (renderer) {
            records.camera = camera;
            saveRecords(dataDir + "/records-dsj2.dat", records);
        }
        audio.reset();
        renderer.reset();
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
    int run() {
        if (!running)
            return 0;
        if (smoke) {
            if (smokeMouseCapture) {
                // Keep the real desktop pointer within the test window during
                // pause/unpause. Otherwise focus-follows-mouse can correctly
                // pause the automated control test when capture is released.
                if (!SDL_SetWindowFullscreen(window, true))
                    throw std::runtime_error(std::string("Test fullscreen failed: ") + SDL_GetError());
                fullscreen = true;
                SDL_RaiseWindow(window);
                render();
                renderer->finish();
                const Uint32 focusDeadline = SDL_GetTicks() + 10000;
                do {
                    SDL_Delay(20);
                    events();
                } while (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
                         SDL_GetTicks() < focusDeadline);
                if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
                    throw std::runtime_error("Mouse capture test requires window focus");
            }
            return smokeTest();
        }
        uint64_t previous = SDL_GetPerformanceCounter();
        while (running) {
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                // A null event pointer waits without consuming the event. The
                // normal handler still processes restore, close and file drops.
                SDL_WaitEventTimeout(nullptr, 100);
                events();
                previous = SDL_GetPerformanceCounter();
                continue;
            }
            uint64_t now = SDL_GetPerformanceCounter();
            double dt = std::min(.08, double(now - previous) / SDL_GetPerformanceFrequency());
            previous = now;
            time += dt;
            events();
            if (screen == Screen::Playing && renderer->sceneReadyFor(hills()[selectedHill])) {
                accumulator += dt;
                while (accumulator >= ORIGINAL_TICK) {
                    Input input = pending;
                    input.buttonMask = keyboardTakeoff ? -1 : int(mouseLeft) + 2 * int(mouseRight);
                    pending = {};
                    input.mouseDy = mouseDy;
                    input.mouseSensitivity = records.sensitivity;
                    mouseDy = 0;
                    const bool *keys = SDL_GetKeyboardState(nullptr);
                    input.pitchDelta +=
                        (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W] ? 1 : 0) * .65 * ORIGINAL_TICK -
                        (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S] ? 1 : 0) * .65 * ORIGINAL_TICK;
                    Phase before = sim.phase;
                    sim.step(ORIGINAL_TICK, input);
                    accumulator -= ORIGINAL_TICK;
                    if (sim.phase != before) {
                        if (sim.phase == Phase::Flight && records.sound)
                            audio->cue(2);
                    }
                    if (sim.phase == Phase::Finished || sim.phase == Phase::Disqualified) {
                        completeJump();
                        break;
                    }
                }
            } else if (screen != Screen::Paused)
                accumulator = 0;
            if (screen == Screen::Replay && replayPlaying && renderer->sceneReadyFor(hills()[lastReplay.hill]))
                replayTime = std::min(replayTime + dt * replaySpeed, replayDuration());
            updateAudio(dt);
            render();
            renderer->finish();
        }
        return 0;
    }

  private:
    void updateAudio(double dt) {
        if (screen == Screen::Replay) {
            const auto mode = replayPlaying && replayTime < replayDuration()
                                  ? CrowdPlayback::Active : CrowdPlayback::Paused;
            audio->update(replayFrame(replayTime), dt, mode, records.sound, float(replaySpeed));
        } else {
            const auto mode = screen == Screen::Playing ? CrowdPlayback::Active
                              : screen == Screen::Paused ? CrowdPlayback::Paused
                              : screen == Screen::Result ? CrowdPlayback::Result : CrowdPlayback::Off;
            audio->update(sim.renderFrame(accumulator), dt, mode, records.sound);
        }
    }
    double chosenWind() { return windMode == 1 ? 0 : windMode == 2 ? 3 : -2.5; }
    void captureMouse() {
        const bool capture = screen == Screen::Playing && (!smoke || smokeMouseCapture);
        // Relative mode already hides the cursor. Only change it when needed:
        // repeated hide/show requests also reach the desktop's cursor path.
        if (SDL_GetWindowRelativeMouseMode(window) != capture) {
            if (!SDL_SetWindowRelativeMouseMode(window, capture)) {
                if (smokeMouseCapture)
                    throw std::runtime_error(std::string("Mouse capture failed: ") + SDL_GetError());
                std::cerr << "Mouse capture failed: " << SDL_GetError() << '\n';
            } else
                ++mouseCaptureTransitions;
        }
        mouseLeft = mouseRight = false;
        mouseDy = 0;
        mouseDyFraction = 0;
    }
    int hillCount() const { return int(hills().size()); }
    int hillPages() const { return (hillCount() + HILLS_PER_PAGE - 1) / HILLS_PER_PAGE; }
    bool finalCupEvent() const { return competition && cup.finalEvent(); }
    CupSetup &editableSetup() { return setupTeam ? teamSetup : worldSetup; }
    int activeJumper() const { return competition ? cup.currentJumper() : -1; }
    bool activeComputer() const {
        int jumper = activeJumper();
        return jumper >= 0 && cup.entries()[size_t(jumper)].participant.computer;
    }
    std::string jumperName() const {
        int jumper = activeJumper();
        return jumper >= 0 ? cup.entries()[size_t(jumper)].participant.name : "YOU";
    }
    std::string jumperCountry() const {
        int jumper = activeJumper();
        return jumper >= 0 ? cup.entries()[size_t(jumper)].participant.country : "";
    }
    void checkpointCup() {
        if (!competition)
            return;
        if (!saveCompetition(dataDir + "/cup.sjc", cup))
            storageError = true;
        hasCupSave = cup.active();
    }
    void syncCup() {
        selectedHill = cup.hill();
        round = cup.round();
        cupEvent = cup.event();
        sessionWeather = cup.weather();
        sessionAuxiliary = cup.auxiliary();
        weatherHill = selectedHill;
    }
    void changeScreen(Screen s) {
        screen = s;
        if (s != Screen::Playing && s != Screen::Paused && s != Screen::Result && s != Screen::Replay)
            audio->stop();
        if (s == Screen::Hills || s == Screen::Records)
            hillPage = selectedHill / HILLS_PER_PAGE;
        captureMouse();
        buttons.clear();
    }
    void newJump(Screen destination = Screen::Playing) {
        if (weatherHill != selectedHill)
            initializeWeather();
        sim.resetWithWeather(selectedHill, sessionWeather,
                             competition ? cup.preparedJumpSeed() : originalClockSeed());
        sim.pose.auxiliary = sessionAuxiliary;
        if (competition)
            sim.configureAi(cup.entries()[size_t(cup.currentJumper())].skills, activeComputer(), false);
        if (windMode != 0 && !competition)
            sim.setWind(std::abs(chosenWind()), chosenWind() >= 0 ? PI : 0);
        keyboardTakeoff = false;
        pending = {};
        accumulator = 0;
        newRecord = false;
        activeEnvironment = competition ? cup.visualEnvironment()
            : environment::resolve(records.environment,
                environment::hash(sim.initialRandomSeed() ^ ++visualJumpSerial * 0x9e3779b9U));
        renderer->setEnvironment(activeEnvironment, true);
        renderer->resetCamera();
        audio->beginJump(sim.hill());
        changeScreen(destination);
        if (records.sound)
            audio->cue(1);
    }
    void beginCup() {
        std::string error;
        if (!cup.start(editableSetup(), originalClockSeed(), originalClockSeed(), originalClockSeed(),
                       &error, records.environment)) {
            message = error;
            return;
        }
        competition = true;
        standingsPage = 0;
        nextCupJumper();
    }
    void initializeWeather() {
        OriginalRandom weatherRandom(originalClockSeed());
        sessionWeather.initialize(weatherRandom);
        weatherHill = selectedHill;
    }
    void nextCupJumper() {
        syncCup();
        while (!cup.eventComplete() && cup.currentJumper() >= 0 && activeComputer() && cup.setup().skipCpu) {
            if (!cup.hasPreparedJump())
                cup.prepareJump(originalClockSeed());
            Simulation cpu;
            cpu.resetWithWeather(selectedHill, sessionWeather, cup.preparedJumpSeed());
            cpu.pose.auxiliary = sessionAuxiliary;
            cpu.configureAi(cup.entries()[size_t(cup.currentJumper())].skills, true, true);
            for (int n = 0; n < 20000 && cpu.phase != Phase::Finished && cpu.phase != Phase::Disqualified;
                 ++n)
                cpu.step(ORIGINAL_TICK, {});
            if (cpu.phase != Phase::Finished && cpu.phase != Phase::Disqualified)
                throw std::runtime_error("CPU jump did not finish");
            cup.retainJumpState(cpu.weatherState(), cpu.pose.auxiliary);
            if (!cup.submitResult(cpu.result))
                throw std::runtime_error("CPU result was rejected");
            syncCup();
            checkpointCup();
        }
        if (cup.eventComplete()) {
            standingsPage = 0;
            checkpointCup();
            changeScreen(Screen::Standings);
            return;
        }
        if (!cup.hasPreparedJump())
            cup.prepareJump(originalClockSeed());
        checkpointCup();
        // Preparing a cup jump must leave the pointer available for Start.
        // Capture begins only when the player starts the turn.
        newJump(Screen::JumpReady);
    }
    void completeJump() {
        completedFeedback = visual::jumpFeedback(sim);
        completedName = jumperName();
        completedCountry = jumperCountry();
        completedRound = round;
        completedEvent = cupEvent;
        sessionWeather = sim.weatherState();
        sessionAuxiliary = sim.pose.auxiliary;
        bool human = !competition || !activeComputer();
        if (updateHillRecord(records, selectedHill, sim.result, human)) {
            newRecord = true;
            if (!saveRecords(dataDir + "/records-dsj2.dat", records))
                storageError = true;
        }
        if (sim.frames.size() > 2) {
            lastReplay = sim.replay();
            lastReplay.jumper = records.jumper;
            lastReplay.hasEnvironment = true;
            lastReplay.environment = activeEnvironment;
            hasReplay = true;
            canExportOriginal = buildOriginalReplay(sim, originalReplayData, &replayExportError);
            if (canExportOriginal) {
                originalReplayData.athleteName = completedName.substr(0, 25);
                canExportOriginal =
                    encodeOriginalReplay(originalReplayData, lastReplay.originalRpl, &replayExportError);
            }
            if (!saveReplay(dataDir + "/latest-dsj2.pvr", lastReplay))
                storageError = true;
        }
        if (competition) {
            cup.retainJumpState(sim.weatherState(), sim.pose.auxiliary);
            if (!cup.submitResult(sim.result))
                throw std::runtime_error("Cup result was rejected");
            checkpointCup();
        }
        changeScreen(Screen::Result);
    }
    void nextRound() {
        if (!competition)
            return;
        nextCupJumper();
    }
    void finishNameEdit(bool accept) {
        if (editingName == -1)
            return;
        auto &setup = editableSetup();
        if (accept && !editText.empty()) {
            if (editingName >= 0 && editingName < int(setup.participants.size())) {
                auto &p = setup.participants[size_t(editingName)];
                if (editingCountry) {
                    const auto *nation = nationality(editText);
                    if (!nation) {
                        message = "Unknown country code";
                        return;
                    }
                    p.country = nation->code;
                    if (p.computer)
                        assignCpuIdentity(setup, size_t(editingName), athleteIdentitySeed(), true);
                } else
                    p.name = editText;
            } else if (editingName <= -2) {
                size_t index = size_t(-editingName - 2);
                if (index < setup.teams.size()) {
                    auto &t = setup.teams[index];
                    if (editingCountry) {
                        const auto *nation = nationality(editText);
                        if (!nation) {
                            message = "Unknown country code";
                            return;
                        }
                        if (nationality(t.name) == nationality(t.country))
                            t.name = nation->name;
                        t.country = nation->code;
                        for (size_t i = 0; i < setup.participants.size(); ++i) {
                            auto &p = setup.participants[i];
                            if (p.team == int(index)) {
                                p.country = t.country;
                                if (p.computer)
                                    assignCpuIdentity(setup, i, athleteIdentitySeed(), true);
                            }
                        }
                    } else
                        t.name = editText;
                }
            }
        }
        message.clear();
        editingName = -1;
        SDL_StopTextInput(window);
    }
    void scanReplays() {
        replayFiles.clear();
        std::error_code error;
        for (std::filesystem::directory_iterator it(replayDirectory, error), end; !error && it != end;
             it.increment(error)) {
            const auto &entry = *it;
            std::string ext = entry.path().extension().string();
            for (char &c : ext)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            if (entry.is_directory(error) || ext == ".pvr" || ext == ".rpl")
                replayFiles.push_back(entry.path());
        }
        std::sort(replayFiles.begin(), replayFiles.end(), [](const auto &a, const auto &b) {
            std::error_code ea, eb;
            bool ad = std::filesystem::is_directory(a, ea), bd = std::filesystem::is_directory(b, eb);
            return ad != bd ? ad : a.filename().string() < b.filename().string();
        });
        replayPage = 0;
        if (error)
            message = "Cannot read this folder: " + error.message();
    }
    bool openReplayFile(const std::string &path) {
        Replay loaded;
        std::string ext = std::filesystem::path(path).extension().string(), error;
        for (char &c : ext)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        OriginalReplayData data;
        bool original = ext == ".rpl";
        if (!original && ext != ".pvr") {
            message = "Unsupported replay format. Choose a .PVR or .RPL file.";
            return false;
        }
        if (original) {
            if (!readOriginalReplay(path, data, &error) ||
                !reconstructOriginalReplay(data, loaded, nullptr, &error)) {
                message = "Replay could not be opened: " + error;
                return false;
            }
            if (!encodeOriginalReplay(data, loaded.originalRpl, &error))
                return false;
            // Original recordings have no character metadata; use the current selection on import.
            loaded.jumper = records.jumper;
        } else if (!loadReplay(path, loaded)) {
            message = "Replay file is invalid or uses an unsupported format.";
            return false;
        }
        canExportOriginal = original || (!loaded.originalRpl.empty() &&
                                         decodeOriginalReplay(loaded.originalRpl, data, &error));
        if (canExportOriginal)
            originalReplayData = data;
        replayExportError = "This replay does not contain an original input recording.";
        lastReplay = std::move(loaded);
        hasReplay = true;
        message.clear();
        beginReplay();
        return true;
    }
    void exportNativeReplay() {
        if (!hasReplay) {
            message = "No replay to export.";
            return;
        }
        Replay exported = lastReplay;
        exported.hasEnvironment = true;
        exported.environment = replayEnvironment;
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count();
        const std::string stem = dataDir + "/replay-" + hills()[exported.hill].id + "-" +
                                 std::to_string(timestamp);
        std::string path = stem + ".pvr";
        std::error_code error;
        for (unsigned suffix = 1; std::filesystem::exists(path, error); ++suffix)
            path = stem + "-" + std::to_string(suffix) + ".pvr";
        if (error || !saveReplay(path, exported)) {
            message = "Replay could not be saved. Check the save directory.";
            return;
        }
        message = "Saved replay: " + path;
    }
    void beginReplay() {
        if (!hasReplay)
            return;
        if (screen != Screen::Replay)
            replayReturn = screen;
        replayTime = 0;
        replaySpeed = 1;
        replayPlaying = true;
        replayEnvironment = lastReplay.hasEnvironment ? lastReplay.environment
            : environment::resolve({}, canExportOriginal ? originalReplayData.seed
                                                        : visual::replayWeatherSeed(lastReplay));
        renderer->setEnvironment(replayEnvironment, true);
        renderer->resetCamera();
        audio->beginReplay(hills()[lastReplay.hill], lastReplay.frames);
        changeScreen(Screen::Replay);
    }
    void seekReplay(double position) {
        replayTime = clamp(position, 0.0, replayDuration());
        audio->seekCrowd();
        renderer->resetCamera();
    }
    double replayDuration() const {
        return lastReplay.frames.empty()
                   ? 0
                   : lastReplay.frames.back().elapsed - lastReplay.frames.front().elapsed;
    }
    Frame replayFrame(double t) const {
        if (lastReplay.frames.empty())
            return sim.frame();
        double sample = t + lastReplay.frames.front().elapsed;
        auto it = std::lower_bound(lastReplay.frames.begin(), lastReplay.frames.end(), sample,
                                   [](const Frame &a, double v) { return a.elapsed < v; });
        if (it == lastReplay.frames.end())
            return lastReplay.frames.back();
        if (it == lastReplay.frames.begin())
            return *it;
        const auto &a = *(it - 1);
        const auto &b = *it;
        double k = clamp((sample - a.elapsed) / (b.elapsed - a.elapsed), 0.0, 1.0);
        return interpolateFrame(a, b, k);
    }
    void savePreferences() {
        records.camera = camera;
        if (!saveRecords(dataDir + "/records-dsj2.dat", records))
            storageError = true;
    }
    void applyGraphics() {
        gfx::configureGraphics(noMsaa ? 0 : records.graphics.msaaSamples, records.graphics.vsync);
        if (!noMsaa)
            records.graphics.msaaSamples = gfx::sampleCount();
        records.graphics.vsync = gfx::vsyncEnabled();
        savePreferences();
    }
    void action(Action a, int value = 0) {
        if (editingName != -1) {
            finishNameEdit(true);
            if (editingName != -1)
                return;
        }
        message.clear();
        buttons.clear();
        bool playerAction = a == Action::CupPlayerName || a == Action::CupPlayerCountry ||
                            a == Action::CupPlayerType || a == Action::CupPlayerLevel;
        bool teamAction = a == Action::CupTeamName || a == Action::CupTeamCountry;
        if ((playerAction && (value < 0 || value >= int(editableSetup().participants.size()))) ||
            (teamAction && (value < 0 || value >= int(editableSetup().teams.size()))))
            return;
        switch (a) {
        case Action::Practice:
            competition = false;
            weatherHill = -1;
            newJump();
            break;
        case Action::Cup:
            setupTab = setupPage = 0;
            changeScreen(Screen::CupSetup);
            break;
        case Action::SkiJumper:
            changeScreen(Screen::SkiJumper);
            break;
        case Action::SelectJumper:
            if (validJumperAppearance(JumperAppearance(value))) {
                records.jumper = JumperAppearance(value);
                if (!saveRecords(dataDir + "/records-dsj2.dat", records))
                    storageError = true;
            }
            break;
        case Action::Hills:
            changeScreen(Screen::Hills);
            break;
        case Action::Help:
            helpReturn = screen;
            changeScreen(Screen::Help);
            break;
        case Action::Settings:
            changeScreen(Screen::Settings);
            break;
        case Action::Graphics:
            changeScreen(Screen::Graphics);
            break;
        case Action::Fullscreen:
            if (SDL_SetWindowFullscreen(window, !fullscreen)) {
                fullscreen = !fullscreen;
                records.graphics.fullscreen = fullscreen;
                savePreferences();
            } else {
                message = "Could not change fullscreen mode.";
            }
            break;
        case Action::Antialiasing: {
            constexpr std::array<int, 4> levels{{0, 2, 4, 8}};
            const auto current = std::find(levels.begin(), levels.end(), gfx::sampleCount());
            for (size_t step = 1; step <= levels.size(); ++step) {
                const int next = levels[(size_t(current - levels.begin()) + step) % levels.size()];
                if (gfx::supportsSampleCount(next)) {
                    records.graphics.msaaSamples = next;
                    break;
                }
            }
            noMsaa = false;
            applyGraphics();
            break;
        }
        case Action::Vsync:
            records.graphics.vsync = !gfx::vsyncEnabled();
            applyGraphics();
            break;
        case Action::Records:
            changeScreen(Screen::Records);
            break;
        case Action::Quit:
            running = false;
            break;
        case Action::Back:
            if (screen == Screen::Graphics) {
                changeScreen(Screen::Settings);
            } else if (screen == Screen::Help) {
                changeScreen(helpReturn);
            } else if (screen == Screen::Replay) {
                audio->endReplay();
                renderer->resetCamera();
                changeScreen(replayReturn);
            } else {
                renderer->resetCamera();
                competition = false;
                changeScreen(Screen::Menu);
            }
            break;
        case Action::SelectHill:
            if (value < 0 || value >= hillCount())
                break;
            selectedHill = value;
            hillPage = selectedHill / HILLS_PER_PAGE;
            sim.reset(value, 2);
            renderer->resetCamera();
            break;
        case Action::HillPage:
            hillPage = clamp(hillPage + value, 0, hillPages() - 1);
            break;
        case Action::Resume:
            changeScreen(Screen::Playing);
            break;
        case Action::Again:
            competition = false;
            newJump();
            break;
        case Action::Next:
            if (screen == Screen::Standings) {
                if (cup.finalEvent()) {
                    cup.advanceEvent(originalClockSeed(), originalClockSeed());
                    checkpointCup();
                    action(Action::Back);
                } else if (cup.advanceEvent(originalClockSeed(), originalClockSeed()))
                    nextCupJumper();
            } else
                nextRound();
            break;
        case Action::CupSetupMode:
            setupTeam = value != 0;
            setupPage = 0;
            break;
        case Action::CupSetupTab:
            setupTab = value;
            setupPage = 0;
            break;
        case Action::CupSetupPage:
            if (screen == Screen::Standings)
                standingsPage += value;
            else
                setupPage = std::max(0, setupPage + value);
            break;
        case Action::CupPlayerName:
        case Action::CupPlayerCountry:
        case Action::CupTeamName:
        case Action::CupTeamCountry: {
            bool team = a == Action::CupTeamName || a == Action::CupTeamCountry;
            if (setupTeam && a == Action::CupPlayerCountry) {
                value = editableSetup().participants[size_t(value)].team;
                team = true;
            }
            editingCountry = a == Action::CupTeamCountry || a == Action::CupPlayerCountry;
            editingName = team ? -2 - value : value;
            editText = team ? (editingCountry ? editableSetup().teams[size_t(value)].country
                                              : editableSetup().teams[size_t(value)].name)
                            : (editingCountry ? editableSetup().participants[size_t(value)].country
                                              : editableSetup().participants[size_t(value)].name);
            SDL_StartTextInput(window);
            break;
        }
        case Action::CupPlayerType: {
            auto &setup = editableSetup();
            auto &p = setup.participants[size_t(value)];
            p.computer = !p.computer;
            if (p.computer)
                assignCpuIdentity(setup, size_t(value), athleteIdentitySeed());
            break;
        }
        case Action::CupPlayerLevel:
            editableSetup().participants[size_t(value)].level =
                (editableSetup().participants[size_t(value)].level + 1) % 6;
            break;
        case Action::CupAddPlayer:
            if (!setupTeam && worldSetup.participants.size() < 16) {
                CupParticipant p;
                p.name = "PLAYER " + std::to_string(worldSetup.participants.size() + 1);
                worldSetup.participants.push_back(p);
            }
            break;
        case Action::CupRemovePlayer:
            if (!setupTeam && worldSetup.participants.size() > 1)
                worldSetup.participants.pop_back();
            break;
        case Action::CupTeamCount: {
            int oldCount = int(teamSetup.teams.size());
            int count = clamp(oldCount + value, 1, 4);
            CupSetup defaults = defaultTeamCupSetup();
            teamSetup.teams.resize(size_t(count));
            teamSetup.participants.resize(size_t(count * 4));
            for (int t = oldCount; t < count; ++t) {
                teamSetup.teams[size_t(t)] = defaults.teams[size_t(t)];
                for (int m = 0; m < 4; ++m)
                    teamSetup.participants[size_t(t * 4 + m)] = defaults.participants[size_t(t * 4 + m)];
            }
            break;
        }
        case Action::CupHillToggle: {
            auto &list = editableSetup().hills;
            auto it = std::find(list.begin(), list.end(), value);
            if (it == list.end())
                list.push_back(value);
            else
                list.erase(it);
            std::sort(list.begin(), list.end());
            break;
        }
        case Action::CupHillsAll:
            editableSetup().hills.clear();
            if (value)
                for (int h = 0; h < hillCount(); ++h)
                    editableSetup().hills.push_back(h);
            break;
        case Action::CupSeasons:
            editableSetup().doubleSeason = !editableSetup().doubleSeason;
            break;
        case Action::CupCpuView:
            editableSetup().skipCpu = !editableSetup().skipCpu;
            break;
        case Action::CupStart:
            beginCup();
            break;
        case Action::CupResume: {
            std::string error;
            if (loadCompetition(dataDir + "/cup.sjc", cup, &error) && cup.active()) {
                competition = true;
                nextCupJumper();
            } else
                message = "Cup could not be resumed: " + error;
            break;
        }
        case Action::StartTurn:
            changeScreen(Screen::Playing);
            break;
        case Action::ReplayBrowse:
            scanReplays();
            changeScreen(Screen::ReplayFiles);
            break;
        case Action::ReplayBrowsePage:
            replayPage = clamp(replayPage + value, 0, std::max(0, (int(replayFiles.size()) - 1) / 8));
            break;
        case Action::ReplayParent:
            if (replayDirectory.has_parent_path())
                replayDirectory = replayDirectory.parent_path();
            scanReplays();
            break;
        case Action::ReplayFile:
            if (value >= 0 && value < int(replayFiles.size())) {
                auto path = replayFiles[size_t(value)];
                if (std::filesystem::is_directory(path)) {
                    replayDirectory = path;
                    scanReplays();
                } else
                    openReplayFile(path.string());
            }
            break;
        case Action::ReplayExport:
            if (!canExportOriginal)
                message = replayExportError;
            else {
                std::string path = dataDir + "/export-" + std::to_string(originalClockSeed()) + ".RPL", error;
                message = saveOriginalReplay(path, originalReplayData, &error)
                              ? "Saved original replay: " + path
                              : error;
            }
            break;
        case Action::ReplayExportNative:
            exportNativeReplay();
            break;
        case Action::Replay:
            beginReplay();
            break;
        case Action::ReplayPlay:
            if (replayTime >= replayDuration()) {
                seekReplay(0);
                replayPlaying = true;
            } else
                replayPlaying = !replayPlaying;
            break;
        case Action::ReplaySlow:
            replaySpeed = std::max(.25, replaySpeed / 2);
            break;
        case Action::ReplayFast:
            replaySpeed = std::min(2.0, replaySpeed * 2);
            break;
        case Action::ReplaySeek:
            seekReplay((mouseX - ReplayLayout::seekX) / ReplayLayout::seekWidth * replayDuration());
            break;
        case Action::Camera:
            camera = (camera + 1) % 3;
            renderer->resetCamera();
            records.camera = camera;
            break;
        case Action::Sound:
            records.sound = !records.sound;
            break;
        case Action::SensitivityDown:
            records.sensitivity = std::max(.3, records.sensitivity - .2);
            break;
        case Action::SensitivityUp:
            records.sensitivity = std::min(3.0, records.sensitivity + .2);
            break;
        case Action::SensitivityReset:
            records.sensitivity = 1;
            break;
        case Action::Wind:
            windMode = (windMode + 1) % 4;
            break;
        case Action::TimeOfDay:
            if (value >= 0 && value <= 4)
                records.environment.time = environment::TimeOfDay(value);
            break;
        case Action::Weather:
            if (value >= 0 && value <= 4)
                records.environment.weather = environment::Weather(value);
            break;
        case Action::EnvironmentReset:
            records.environment = {};
            break;
        case Action::Ghost:
            ghost = !ghost;
            break;
        default:
            break;
        }
    }
    void events() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_DROP_FILE) {
                std::string path(e.drop.data);
                if (screen == Screen::Playing)
                    changeScreen(Screen::Paused);
                openReplayFile(path);
                continue;
            }
            if (e.type == SDL_EVENT_TEXT_INPUT && editingName != -1) {
                std::string value = editText + e.text.text;
                if (value.size() <= (editingCountry ? 3u : 25u))
                    editText = value;
                continue;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && editingName != -1) {
                if (e.key.key == SDLK_RETURN)
                    finishNameEdit(true);
                else if (e.key.key == SDLK_ESCAPE)
                    finishNameEdit(false);
                else if (e.key.key == SDLK_BACKSPACE && !editText.empty()) {
                    size_t start = editText.size() - 1;
                    while (start > 0 && (static_cast<unsigned char>(editText[start]) & 0xc0) == 0x80)
                        --start;
                    editText.erase(start);
                }
                continue;
            }
            if (e.type == SDL_EVENT_QUIT)
                running = false;
            if ((e.type == SDL_EVENT_WINDOW_FOCUS_LOST || e.type == SDL_EVENT_WINDOW_MINIMIZED) &&
                screen == Screen::Playing) {
                if (smokeMouseCapture)
                    std::cout << "Mouse capture test: window lost focus during play\n" << std::flush;
                changeScreen(Screen::Paused);
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (screen == Screen::Playing && sim.phase == Phase::Flight) {
                    // SDL3 reports floats. Carry sub-count movement while keeping
                    // the original physics controller's integer mouse counts.
                    const double counts = clamp(double(mouseDy) + mouseDyFraction + e.motion.yrel,
                                                -1000000.0, 1000000.0);
                    mouseDy = int(counts);
                    mouseDyFraction = counts - mouseDy;
                } else {
                    int ww = 0, hh = 0;
                    if (!SDL_GetWindowSize(window, &ww, &hh) || ww <= 0 || hh <= 0)
                        continue;
                    float px = float(e.motion.x) * renderer->width / ww,
                          py = float(e.motion.y) * renderer->height / hh;
                    mouseX = (px - renderer->offsetX) / renderer->uiScale;
                    mouseY = (py - renderer->offsetY) / renderer->uiScale;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (screen == Screen::Playing) {
                    int b = e.button.button == SDL_BUTTON_LEFT    ? 1
                            : e.button.button == SDL_BUTTON_RIGHT ? 2
                                                                  : 0;
                    if (b == 1)
                        mouseLeft = true;
                    if (b == 2)
                        mouseRight = true;
                    if (sim.phase == Phase::Ready && b == 1)
                        pending.start = true;
                    if (b) {
                        keyboardTakeoff = false;
                        // Raw levels are evaluated by the original 70 Hz controller.
                        if (sim.originalStage == 3)
                            pending.takeoff = true;
                    }
                } else if (e.button.button == SDL_BUTTON_LEFT) {
                    for (const auto &b : buttons)
                        if (mouseX >= b.x && mouseX <= b.x + b.w && mouseY >= b.y && mouseY <= b.y + b.h) {
                            Action a = b.action;
                            int value = b.value;
                            action(a, value);
                            break;
                        }
                }
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL && (screen == Screen::Hills || screen == Screen::Records)) {
                int direction = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
                if (direction)
                    action(Action::HillPage, direction > 0 ? -1 : 1);
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (e.button.button == SDL_BUTTON_LEFT)
                    mouseLeft = false;
                if (e.button.button == SDL_BUTTON_RIGHT)
                    mouseRight = false;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                auto key = e.key.key;
                if (key == SDLK_F11) {
                    action(Action::Fullscreen);
                }
                if (key == SDLK_M)
                    action(Action::Sound);
                if (key == SDLK_C)
                    action(Action::Camera);
                if (key == SDLK_ESCAPE) {
                    if (screen == Screen::Playing)
                        changeScreen(Screen::Paused);
                    else if (screen == Screen::Paused)
                        action(Action::Resume);
                    else if (screen != Screen::Menu)
                        action(Action::Back);
                } else if (screen == Screen::Playing) {
                    if (key == SDLK_SPACE) {
                        if (sim.phase == Phase::Ready)
                            pending.start = true;
                        else if (sim.phase == Phase::Inrun ||
                                 (sim.phase == Phase::Flight && !sim.jumped && sim.phaseTime < .16)) {
                            pending.takeoff = true;
                            keyboardTakeoff = true;
                        } else if (sim.phase == Phase::Flight)
                            pending.land = Landing::TwoFoot;
                    }
                    if (key == SDLK_T)
                        pending.land = Landing::Telemark;
                    if (key == SDLK_B)
                        pending.land = Landing::TwoFoot;
                    if (key == SDLK_G)
                        action(Action::Ghost);
                } else if (screen == Screen::Replay) {
                    if (key == SDLK_SPACE)
                        action(Action::ReplayPlay);
                    if (key == SDLK_LEFT) {
                        seekReplay(replayTime - 2);
                    }
                    if (key == SDLK_RIGHT) {
                        seekReplay(replayTime + 2);
                    }
                    if (key == SDLK_1)
                        replaySpeed = .25;
                    if (key == SDLK_2)
                        replaySpeed = .5;
                    if (key == SDLK_3)
                        replaySpeed = 1;
                    if (key == SDLK_4)
                        replaySpeed = 2;
                } else if (screen == Screen::Result) {
                    if (key == SDLK_RETURN || key == SDLK_SPACE)
                        action(competition ? Action::Next : Action::Again);
                    if (key == SDLK_R)
                        action(Action::Replay);
                } else if (screen == Screen::Standings && key == SDLK_RETURN)
                    action(Action::Next);
                else if (screen == Screen::JumpReady && (key == SDLK_RETURN || key == SDLK_SPACE))
                    action(Action::StartTurn);
                else if (screen == Screen::CupSetup && key == SDLK_RETURN)
                    action(Action::CupStart);
                else if (screen == Screen::Paused && key == SDLK_RETURN)
                    action(Action::Resume);
                else if (screen == Screen::Menu) {
                    if (key == SDLK_RETURN || key == SDLK_SPACE)
                        action(Action::Practice);
                    if (key == SDLK_H)
                        action(Action::Help);
                    if (key == SDLK_R && hasReplay)
                        action(Action::Replay);
                } else if (screen == Screen::Hills || screen == Screen::Records) {
                    if (key == SDLK_RIGHT || key == SDLK_DOWN)
                        action(Action::SelectHill, (selectedHill + 1) % hillCount());
                    if (key == SDLK_LEFT || key == SDLK_UP)
                        action(Action::SelectHill, (selectedHill + hillCount() - 1) % hillCount());
                    if (key == SDLK_PAGEDOWN)
                        action(Action::HillPage, 1);
                    if (key == SDLK_PAGEUP)
                        action(Action::HillPage, -1);
                    if (key == SDLK_RETURN)
                        action(Action::Practice);
                }
            }
        }
    }
    void button(const std::string &label, float x, float y, float w, Action a, int value = 0,
                bool primary = false, float h = 50, float size = 15) {
        auto &r = *renderer;
        bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
        Color bg = primary ? colors::teal : Color{.042f, .097f, .125f, .86f};
        if (hover) {
            bg.r = std::min(1.f, bg.r + .06f);
            bg.g = std::min(1.f, bg.g + .06f);
            bg.b = std::min(1.f, bg.b + .06f);
        }
        r.rect(x, y, w, h, bg, 7);
        if (!primary)
            r.line(x + 8, y + 1, x + w - 8, y + 1, {.65f, .78f, .81f, .13f});
        const float labelSize = std::min(size, (w - (w < 100 ? 24 : 40)) / std::max(1.f, r.textWidth(label, 1, true)));
        const float textX = w < 100 ? x + (w - r.textWidth(label, labelSize, true)) / 2 : x + 20;
        r.text(label, textX, y + (h - labelSize) / 2 - 2, labelSize, primary ? colors::ink : colors::white,
               true);
        buttons.push_back({x, y, w, h, a, value});
    }
    void smallButton(const std::string &label, float x, float y, float w, Action a, int value = 0,
                     bool selected = false) {
        button(label, x, y, w, a, value, selected, 35, 13);
    }
    void flagButton(const std::string &country, float x, float y, float w, Action action, int value) {
        const bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + 35;
        renderer->rect(x, y, w, 35, {.042f, .097f, .125f, hover ? 1.f : .86f}, 6);
        renderer->countryFlag(country, x + (w - 30) / 2, y + 7, 30, 21);
        buttons.push_back({x, y, w, 35, action, value});
    }
    void textLink(const std::string &label, float x, float y, float w, Action a, float size = 15) {
        bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + 36;
        if (hover)
            renderer->rect(x, y, w, 36, {.042f, .097f, .125f, .86f}, 6);
        renderer->text(label, x + 20, y + 8, size, hover ? colors::white : colors::muted);
        buttons.push_back({x, y, w, 36, a});
    }
    void dimViewport(Color color) {
        auto &r = *renderer;
        r.rect(-r.offsetX / r.uiScale, -r.offsetY / r.uiScale, r.width / r.uiScale, r.height / r.uiScale,
               color);
    }
    void brand(float x, float y) {
        auto &r = *renderer;
        r.line(x, y + 22, x + 15, y, colors::teal, 2);
        r.line(x + 15, y, x + 30, y + 22, colors::teal, 2);
        r.line(x + 19, y + 8, x + 27, y - 3, colors::white, 2);
        r.line(x + 27, y - 3, x + 43, y + 22, colors::white, 2);
        r.text("THE PERFECT V", x + 58, y - 1, 20, colors::white, true);
    }
    void top(const std::string &title, const std::string &subtitle = {}) {
        auto &r = *renderer;
        dimViewport({.023f, .057f, .079f, .9f});
        brand(64, 46);
        smallButton("Back", 1250, 40, 126, Action::Back);
        r.text(title, 64, 126, 36, colors::white, true);
        if (!subtitle.empty())
            r.text(subtitle, 66, 183, 14, colors::muted);
    }
    void menu() {
        auto &r = *renderer;
        const auto &h = hills()[selectedHill];
        const float left = -r.offsetX / r.uiScale, top = -r.offsetY / r.uiScale;
        r.gradient(left, top, 970 - left, r.height / r.uiScale, {.023f, .057f, .079f, .98f},
                   {.023f, .057f, .079f, 0});
        constexpr float x = 76, y = 66;
        r.line(x, y + 23, x + 15, y, colors::teal, 2);
        r.line(x + 15, y, x + 30, y + 23, colors::teal, 2);
        r.line(x + 19, y + 8, x + 27, y - 3, colors::white, 2);
        r.line(x + 27, y - 3, x + 43, y + 23, colors::white, 2);
        r.text("THE PERFECT V", x + 59, y - 2, 23, colors::white, true);
        constexpr float quitY = 809;
        const float groupHeight = hasCupSave ? 428.f : 382.f;
        // Balance the visible group against the logo and Quit text, excluding
        // the extra padding in the links' click targets.
        const float groupY = (y + 23 + quitY + 8 - groupHeight) / 2;
        const bool hover = mouseX >= x && mouseX <= x + 316 && mouseY >= groupY && mouseY <= groupY + 46;
        r.rect(x, groupY, 316, 46, {.025f, .071f, .094f, hover ? .9f : .65f}, 6);
        r.countryFlag(h.country, x + 20, groupY + 12, 28, 20);
        r.text(h.name + " / K" + std::to_string(h.k), x + 61, groupY + 13, 16, colors::white);
        r.line(x + 288, groupY + 18, x + 294, groupY + 24, colors::muted, 1.4);
        r.line(x + 294, groupY + 24, x + 300, groupY + 18, colors::muted, 1.4);
        buttons.push_back({x, groupY, 316, 46, Action::Hills});
        button("Practice", x, groupY + 74, 316, Action::Practice, 0, true, 54, 18);
        button("Cup", x, groupY + 142, 316, Action::Cup, 0, false, 54, 18);
        button("Ski Jumper", x, groupY + 210, 316, Action::SkiJumper, 0, false, 54, 18);
        button("Replays", x, groupY + 278, 316, Action::ReplayBrowse, 0, false, 54, 18);
        textLink("Records", x, groupY + 359, 130, Action::Records);
        textLink("Settings", x + 146, groupY + 359, 130, Action::Settings);
        if (hasCupSave)
            textLink("Resume cup", x, groupY + 405, 170, Action::CupResume);
        textLink("Quit", x, quitY, 95, Action::Quit, 14);
    }
    void skiJumperUI() {
        top("Ski Jumper");
        auto &r = *renderer;
        for (size_t i = 0; i < JUMPER_APPEARANCES.size(); ++i) {
            const auto appearance = JUMPER_APPEARANCES[i];
            const bool selected = records.jumper == appearance;
            const float x = 64 + i * 334;
            r.glassPanel(x, 214, 310, 566, 12, .94f);
            if (selected)
                r.rect(x + 22, 214, 58, 3, colors::teal, 1);
            r.text(jumperAppearanceName(appearance), x + 22, 244, 27, colors::white, true);
            r.jumperPreview(appearance, x + 132, 618, 145);
            button(selected ? "Selected" : "Select", x + 22, 720, 266,
                   Action::SelectJumper, int(appearance), selected, 40, 14);
        }
    }
    void hillPagination(float y) {
        auto &r = *renderer;
        r.text(std::to_string(hillPage + 1) + " / " + std::to_string(hillPages()), 1040, y + 10, 13,
               colors::muted);
        if (hillPage > 0)
            smallButton("Previous", 1174, y, 98, Action::HillPage, -1);
        if (hillPage + 1 < hillPages())
            smallButton("Next", 1283, y, 91, Action::HillPage, 1);
    }
    void hillSelect() {
        top("Hills");
        auto &r = *renderer;
        int first = hillPage * HILLS_PER_PAGE;
        for (int slot = 0; slot < HILLS_PER_PAGE && first + slot < hillCount(); slot++) {
            int i = first + slot;
            float x = 64 + (slot % 4) * 332, y = 246 + (slot / 4) * 236;
            const auto &h = hills()[i];
            bool selected = i == selectedHill;
            r.glassPanel(x, y, 314, 216, 12, .92f);
            if (selected) {
                r.rect(x + 1, y + 1, 312, 214, {.16f, .53f, .48f, .14f}, 11);
                r.rect(x, y + 20, 3, 47, colors::teal, 1);
            }
            r.countryFlag(h.country, x + 252, y + 36, 40, 27);
            r.text(h.name, x + 20, y + 36, 23, colors::white, true);
            r.text("K" + std::to_string(h.k), x + 20, y + 91, 34, selected ? colors::teal : colors::white,
                   true);
            if (records.best[i] > 0)
                r.text("Record " + number(records.best[i]) + " m", x + 21, y + 187, 12, colors::muted);
            buttons.push_back({x, y, 314, 216, Action::SelectHill, i});
        }
        hillPagination(722);
        button("World cup", 955, 811, 157, Action::Cup);
        button("Practice", 1128, 811, 246, Action::Practice, 0, true);
    }
    void help() {
        top("Controls");
        auto &r = *renderer;
        r.text("ACTION", 86, 248, 12, colors::muted, true);
        r.text("MOUSE", 360, 248, 12, colors::muted, true);
        r.text("KEYBOARD", 1070, 248, 12, colors::muted, true);
        struct Control {
            const char *action, *mouse, *key;
        };
        const Control controls[] = {{"Start", "Left button; release during inrun", "Space"},
                                    {"Takeoff", "Hold both buttons near the edge, then release", "Space"},
                                    {"Flight position", "Move up / down", "Up / Down or W / S"},
                                    {"Telemark", "One button, then the other before contact", "T"},
                                    {"Two-foot landing", "Both buttons together", "B or Space"}};
        for (int i = 0; i < 5; ++i) {
            const float y = 282 + i * 64;
            r.glassPanel(64, y, 1310, 54, 7, .86f);
            r.text(controls[i].action, 86, y + 16, 17, colors::white, true);
            r.text(controls[i].mouse, 360, y + 17, 16, colors::muted);
            r.text(controls[i].key, 1070, y + 17, 16, colors::white);
        }
        const Control shortcuts[] = {{"Pause", "", "Esc"}, {"Camera", "", "C"},       {"Ghost", "", "G"},
                                     {"Sound", "", "M"},   {"Fullscreen", "", "F11"}, {"Replay", "", "R"}};
        for (int i = 0; i < 6; ++i) {
            const float x = 86 + (i % 3) * 436, y = 658 + (i / 3) * 59;
            r.text(shortcuts[i].action, x, y, 15, colors::muted);
            r.text(shortcuts[i].key, x + 170, y, 15, colors::white, true);
        }
    }
    void settings() {
        top("Settings");
        auto &r = *renderer;
        const char *cams[] = {"Side follow", "Chase", "Wide"};
        const char *winds[] = {"Variable", "Calm", "Headwind", "Tailwind"};
        struct Setting {
            std::string title, value;
            Action action;
        };
        const Setting rows[] = {{"Camera", cams[camera], Action::Camera},
                                {"Sound", records.sound ? "On" : "Off", Action::Sound},
                                {"Practice wind", winds[windMode], Action::Wind},
                                {"Last jump ghost", ghost ? "On" : "Off", Action::Ghost}};
        for (int i = 0; i < 4; ++i) {
            const float y = 253 + i * 87;
            r.glassPanel(64, y, 644, 74, 7, .9f);
            r.text(rows[i].title, 86, y + 25, 18, colors::white, true);
            button(rows[i].value, 456, y + 12, 230, rows[i].action);
        }
        r.glassPanel(738, 253, 636, 185, 7, .9f);
        r.text("Time of day", 758, 271, 19, colors::white, true);
        for (int i = 0; i <= 4; ++i)
            button(environment::name(environment::TimeOfDay(i)), 758 + (i % 3) * 205, 307 + (i / 3) * 59, 186,
                   Action::TimeOfDay, i, int(records.environment.time) == i);
        r.glassPanel(738, 456, 636, 185, 7, .9f);
        r.text("Weather", 758, 474, 19, colors::white, true);
        for (int i = 0; i <= 4; ++i)
            button(environment::name(environment::Weather(i)), 758 + (i % 3) * 205, 510 + (i / 3) * 59, 186,
                   Action::Weather, i, int(records.environment.weather) == i);
        r.glassPanel(64, 619, 644, 123, 7, .9f);
        r.text("Mouse sensitivity", 86, 636, 19, colors::white, true);
        smallButton("Reset", 86, 690, 130, Action::SensitivityReset);
        smallButton("-", 326, 690, 56, Action::SensitivityDown);
        r.text(number(records.sensitivity) + "x", 430, 697, 18, colors::teal, true);
        smallButton("+", 630, 690, 56, Action::SensitivityUp);
        smallButton("Day & sun", 1202, 662, 151, Action::EnvironmentReset);
        button("Controls", 64, 803, 210, Action::Help);
        button("Graphics", 290, 803, 210, Action::Graphics);
    }
    void graphicsSettings() {
        top("Graphics");
        struct Setting {
            const char *title;
            std::string value;
            Action action;
        };
        const std::array<Setting, 3> rows{{
            {"Fullscreen", fullscreen ? "On" : "Off", Action::Fullscreen},
            {"Antialiasing", gfx::sampleCount() ? std::to_string(gfx::sampleCount()) + "x MSAA" : "Off", Action::Antialiasing},
            {"VSync", gfx::vsyncEnabled() ? "On" : "Off", Action::Vsync}}};
        for (size_t i = 0; i < rows.size(); ++i) {
            const float y = 253 + i * 87;
            renderer->glassPanel(64, y, 870, 74, 7, .9f);
            renderer->text(rows[i].title, 86, y + 25, 18, colors::white, true);
            button(rows[i].value, 652, y + 12, 260, rows[i].action);
        }
    }
    void recordScreen() {
        top("Records");
        auto &r = *renderer;
        r.text("HILL", 89, 242, 11, colors::muted, true);
        r.text("K POINT", 870, 242, 11, colors::muted, true);
        r.textRight("HILL RECORD", 1334, 242, 11, colors::muted, true);
        int first = hillPage * HILLS_PER_PAGE;
        for (int slot = 0; slot < HILLS_PER_PAGE && first + slot < hillCount(); slot++) {
            int i = first + slot;
            float y = 272 + slot * 54;
            bool selected = i == selectedHill;
            r.glassPanel(64, y, 1310, 47, 8, .9f);
            if (selected) {
                r.rect(65, y + 1, 1308, 45, {.16f, .53f, .48f, .13f}, 7);
                r.rect(64, y + 10, 3, 27, colors::teal, 1);
            }
            r.countryFlag(hills()[i].country, 89, y + 12, 30, 21);
            r.text(hills()[i].name, 136, y + 13, 17, colors::white, true);
            r.text("K" + std::to_string(hills()[i].k), 870, y + 16, 13, colors::muted);
            r.textRight(records.best[i] ? number(records.best[i]) + " m" : "--", 1334, y + 10, 21,
                        records.best[i] ? colors::teal : colors::muted, true);
            buttons.push_back({64, y, 1310, 47, Action::SelectHill, i});
        }
        hillPagination(722);
        button("Practice", 1077, 808, 296, Action::Practice, 0, true);
        if (hasReplay)
            button("Last replay", 64, 808, 238, Action::Replay);
    }
    void hud() {
        auto &r = *renderer;
        const auto &h = sim.hill();
        r.glassPanel(32, 28, 320, 48, 7, .86f);
        r.text(h.name + " / K" + std::to_string(h.k), 50, 41, 18, colors::white, true);
        if (competition) {
            r.glassPanel(470, 28, 490, 67, 7, .86f);
            const std::string name = jumperName();
            const float size = std::min(18.f, 406.f / std::max(1.f, r.textWidth(name, 1, true)));
            r.countryFlag(jumperCountry(), 488, 39, 28, 20);
            r.text(name, 532, 39, size, colors::white, true);
            r.text("Event " + std::to_string(cupEvent + 1) + " / " + std::to_string(cup.eventCount()) +
                       "   Round " + std::to_string(round + 1),
                   488, 70, 11, colors::muted);
        }
        r.glassPanel(1208, 28, 200, 48, 7, .86f);
        r.windArrow(1237, 52, sim.windSpeed, sim.displayWindAngle());
        r.textRight(number(sim.windSpeed) + " m/s", 1390, 39, 22, colors::white, true);
        if (sim.phase == Phase::Ready) {
            r.glassPanel(32, 88, 80, 34, 7, .86f);
            r.text(number(sim.countdown, 0) + " s", 50, 95, 16,
                   sim.countdown < 5 ? colors::orange : colors::teal, true);
            return;
        }
        r.glassPanel(32, 801, 174, 57, 7, .86f);
        r.text(number(std::hypot(sim.vx, sim.vy) * 3.6, 0), 48, 811, 30, colors::white, true);
        r.textRight("km/h", 188, 829, 12, colors::muted);
        if (sim.phase == Phase::Flight || sim.phase == Phase::Outrun) {
            r.glassPanel(220, 801, 184, 57, 7, .86f);
            const double distance = sim.phase == Phase::Flight ? h.distance(sim.x) : sim.result.distance;
            r.text(number(distance), 236, 811, 30, colors::white, true);
            r.textRight("m", 386, 829, 12, colors::muted);
        }
    }
    void paused() {
        auto &r = *renderer;
        dimViewport({.023f, .057f, .079f, .78f});
        r.glassPanel(472, 285, 496, 320, 16, .94f);
        r.text("Paused", 508, 317, 38, colors::white, true);
        button("Resume", 510, 398, 420, Action::Resume, 0, true);
        button("Leave jump", 510, 465, 420, Action::Back);
    }
    void result() {
        auto &r = *renderer;
        const auto &s = sim.result;
        dimViewport({.023f, .057f, .079f, .7f});
        r.glassPanel(287, 121, 866, 662, 16, .95f);
        r.rect(326, 121, 78, 3, colors::teal, 1);
        r.pill(s.disqualified ? "DISQUALIFIED"
               : s.crashed    ? "FALL"
               : newRecord    ? "NEW HILL RECORD"
                              : "RESULT",
               326, 154, s.crashed || s.disqualified ? colors::orange : colors::teal);
        r.textRight(sim.hill().name + " / K" + std::to_string(sim.hill().k), 1114, 161, 13, colors::muted);
        r.text(number(s.distance), 321, 282, 82, s.crashed ? colors::white : colors::teal, true);
        r.text("m", 611, 344, 18, colors::muted);
        r.textRight(number(s.total), 1114, 301, 56, colors::white, true);
        r.textRight("TOTAL POINTS", 1112, 369, 11, colors::muted, true);
        r.line(326, 410, 1114, 410, {.3f, .42f, .48f, .55f});
        r.text("DISTANCE", 326, 434, 11, colors::muted, true);
        r.text(number(s.distancePoints) + " pts", 326, 459, 23, colors::white, true);
        r.textRight("STYLE", 1114, 434, 11, colors::muted, true);
        r.textRight(number(s.stylePoints) + " pts", 1114, 459, 23, colors::white, true);
        auto min = std::min_element(s.judges.begin(), s.judges.end());
        auto max = std::max_element(s.judges.begin(), s.judges.end());
        if (min == max && s.judges.size() > 1)
            max = s.judges.end() - 1;
        r.text("JUDGES", 326, 518, 11, colors::muted, true);
        for (int i = 0; i < 5; i++) {
            float x = 424 + i * 106;
            bool noMarks = s.beforeTakeoff || s.disqualified;
            bool drop = !noMarks && (&s.judges[i] == &*min || &s.judges[i] == &*max);
            r.rect(x, 509, 88, 47, drop ? Color{.25f, .36f, .41f, .23f} : Color{.27f, .88f, .78f, .2f}, 7);
            r.rect(x + 1, 510, 86, 45, {.07f, .14f, .18f, .92f}, 6);
            std::string v = noMarks ? "--" : number(s.judges[i]);
            r.text(v, x + 15, 521, 18, drop ? colors::muted : colors::white, true);
            if (drop)
                r.line(x + 11, 535, x + 73, 535, colors::muted, 1);
        }
        const std::array<const char *, 3> labels{"TAKEOFF", "FLIGHT", "LANDING"};
        const std::array<visual::FeedbackItem, 3> feedback{
            completedFeedback.takeoff, completedFeedback.flight, completedFeedback.landing};
        for (size_t i = 0; i < feedback.size(); ++i) {
            const float x = 326 + i * 277;
            const Color color = feedback[i].tone == visual::FeedbackTone::Good ? colors::teal
                                : feedback[i].tone == visual::FeedbackTone::Improve ? colors::orange
                                                                                   : colors::muted;
            r.text(labels[i], x, 579, 11, colors::muted, true);
            r.rect(x, 607, 4, 4, color, 2);
            r.text(feedback[i].text, x + 14, 597, 17, colors::white, true);
        }
        if (competition) {
            r.countryFlag(completedCountry, 326, 633, 24, 17);
            r.text(completedName + "  /  EVENT " + std::to_string(completedEvent + 1) + " OF " +
                       std::to_string(cup.eventCount()) + "  /  ROUND " + std::to_string(completedRound + 1) +
                       " OF 2",
                   366, 633, 12, colors::teal, true);
        }
        button(competition ? "Continue" : "Jump again", 326, 691, 265,
               competition ? Action::Next : Action::Again, 0, true);
        if (hasReplay && !s.disqualified)
            button("Replay", 608, 691, 217, Action::Replay);
        button("Menu", 842, 691, 271, Action::Back);
    }
    void cupSetupUI() {
        auto &r = *renderer;
        auto &setup = editableSetup();
        top("Cup");
        smallButton("World cup", 65, 209, 150, Action::CupSetupMode, 0, !setupTeam);
        smallButton("Team cup", 230, 209, 150, Action::CupSetupMode, 1, setupTeam);
        smallButton("Participants", 398, 209, 176, Action::CupSetupTab, 0, setupTab == 0);
        smallButton("Hills", 590, 209, 110, Action::CupSetupTab, 1, setupTab == 1);
        smallButton(setup.doubleSeason ? "Two seasons" : "One season", 726, 209, 170, Action::CupSeasons);
        smallButton(setup.skipCpu ? "CPU: skip" : "CPU: watch", 912, 209, 156, Action::CupCpuView);
        int pages = 1;
        if (setupTab == 0) {
            int count = int(setup.participants.size());
            pages = (count + 7) / 8;
            setupPage = clamp(setupPage, 0, pages - 1);
            if (setupTeam) {
                for (int t = 0; t < int(setup.teams.size()); ++t) {
                    smallButton(setup.teams[size_t(t)].name, 65 + t * 266, 299, 177, Action::CupTeamName, t);
                    flagButton(setup.teams[size_t(t)].country, 250 + t * 266, 299, 63, Action::CupTeamCountry,
                               t);
                }
                smallButton("- team", 1150, 299, 96, Action::CupTeamCount, -1);
                smallButton("+ team", 1257, 299, 111, Action::CupTeamCount, 1);
            } else {
                smallButton("+ player", 1100, 299, 128, Action::CupAddPlayer);
                smallButton("- player", 1240, 299, 128, Action::CupRemovePlayer);
            }
            static const char *levels[] = {"Poorest", "Poor", "Average", "Good", "Superb", "Random"};
            for (int row = 0, i = setupPage * 8; i < std::min(count, (setupPage + 1) * 8); ++row, ++i) {
                const auto &p = setup.participants[size_t(i)];
                float y = 354 + row * 47;
                r.glassPanel(65, y, 1303, 43, 8, .86f);
                r.text(std::to_string(i + 1), 83, y + 12, 14, colors::muted);
                smallButton(p.name, 132, y + 3, 330, Action::CupPlayerName, i);
                flagButton(p.country, 479, y + 3, 82, Action::CupPlayerCountry, i);
                smallButton(p.computer ? "CPU" : "Human", 582, y + 3, 127, Action::CupPlayerType, i);
                if (p.computer)
                    smallButton(levels[p.level], 728, y + 3, 155, Action::CupPlayerLevel, i);
                if (setupTeam)
                    r.text(setup.teams[size_t(p.team)].name + " / MEMBER " + std::to_string(p.member + 1),
                           908, y + 13, 13, colors::muted);
            }
        } else {
            pages = hillPages();
            setupPage = clamp(setupPage, 0, pages - 1);
            smallButton("All", 1128, 265, 110, Action::CupHillsAll, 1);
            smallButton("Clear", 1255, 265, 113, Action::CupHillsAll, 0);
            for (int row = 0, h = setupPage * 8; h < std::min(hillCount(), (setupPage + 1) * 8); ++row, ++h) {
                bool chosen = std::find(setup.hills.begin(), setup.hills.end(), h) != setup.hills.end();
                button(hills()[h].name + " / K" + std::to_string(hills()[h].k), 65, 321 + row * 53, 1303,
                       Action::CupHillToggle, h, chosen);
            }
        }
        if (pages > 1) {
            if (setupPage > 0)
                smallButton("Previous", 65, 769, 110, Action::CupSetupPage, -1);
            r.text(std::to_string(setupPage + 1) + " / " + std::to_string(pages), 194, 781, 13,
                   colors::muted);
            if (setupPage + 1 < pages)
                smallButton("Next", 330, 769, 110, Action::CupSetupPage, 1);
        }
        r.text(std::to_string(setup.participants.size()) + " jumpers / " +
                   std::to_string(setup.hills.size()) + " hills",
               65, 828, 15, colors::teal, true);
        button("Start cup", 1128, 802, 240, Action::CupStart, 0, true);
    }
    void jumpReadyUI() {
        auto &r = *renderer;
        r.glassPanel(300, 211, 840, 470, 16, .94f);
        r.pill(cup.setup().mode == CupMode::Team ? "TEAM CUP" : "WORLD CUP", 343, 246);
        r.text("Next jumper", 343, 304, 24, colors::muted);
        r.countryFlag(jumperCountry(), 343, 359, 44, 30);
        const std::string name = jumperName();
        const float nameSize = std::min(43.f, 684.f / std::max(1.f, r.textWidth(name, 1, true)));
        r.text(name, 405, 354, nameSize, colors::white, true);
        r.text(sim.hill().name + " / EVENT " + std::to_string(cup.event() + 1) + " OF " +
                   std::to_string(cup.eventCount()) + " / ROUND " + std::to_string(cup.round() + 1),
               343, 423, 16, colors::teal);
        r.text(environment::description(activeEnvironment), 813, 266, 13, colors::muted);
        if (cup.setup().mode == CupMode::Team) {
            const auto &p = cup.entries()[size_t(activeJumper())].participant;
            r.text(cup.setup().teams[size_t(p.team)].name + " / MEMBER " + std::to_string(p.member + 1), 343,
                   457, 16, colors::muted);
        }
        button(activeComputer() ? "Watch" : "Ready", 343, 576, 310, Action::StartTurn, 0, true);
        button("Save & menu", 690, 576, 360, Action::Back);
    }
    void standings() {
        bool final = cup.finalEvent() && cup.eventComplete();
        top(final ? "Cup results" : "Standings",
            hills()[cup.hill()].name + " / EVENT " + std::to_string(cup.event() + 1) + " OF " +
                std::to_string(cup.eventCount()) + " / " + environment::description(cup.visualEnvironment()));
        auto &r = *renderer;
        bool team = cup.setup().mode == CupMode::Team;
        auto order = cup.standings(final), rank = cup.ranks(final);
        int pages = (int(order.size()) + 7) / 8;
        standingsPage = clamp(standingsPage, 0, pages - 1);
        r.text("POS", 88, 247, 11, colors::muted, true);
        r.text(team ? "TEAM" : "JUMPER", 174, 247, 11, colors::muted, true);
        if (!team) {
            r.text("ROUND 1", 658, 247, 11, colors::muted, true);
            r.text("ROUND 2", 832, 247, 11, colors::muted, true);
        }
        r.text("EVENT", 1034, 247, 11, colors::muted, true);
        r.textRight("CUP POINTS", 1337, 247, 11, colors::muted, true);
        for (int row = 0, pos = standingsPage * 8; pos < std::min(int(order.size()), (standingsPage + 1) * 8);
             ++row, ++pos) {
            int index = order[size_t(pos)];
            float y = 281 + row * 58;
            bool human = !team && !cup.entries()[size_t(index)].participant.computer;
            r.glassPanel(64, y, 1310, 51, 8, .9f);
            if (human) {
                r.rect(65, y + 1, 1308, 49, {.16f, .53f, .48f, .13f}, 7);
                r.rect(64, y + 12, 3, 27, colors::teal, 1);
            }
            r.text(std::to_string(rank[size_t(index)]), 91, y + 13, 18, colors::teal, true);
            std::string name, country;
            double total;
            int points;
            if (team) {
                const auto &t = cup.setup().teams[size_t(index)];
                const auto &score = cup.teamStandings()[size_t(index)];
                name = t.name;
                country = t.country;
                total = score.eventTotal;
                points = score.cupPoints;
            } else {
                const auto &e = cup.entries()[size_t(index)];
                name = e.participant.name;
                country = e.participant.country;
                total = e.eventTotal;
                points = e.cupPoints;
                r.text(number(e.results[0].total), 658, y + 13, 18, colors::white);
                r.text(number(e.results[1].total), 832, y + 13, 18, colors::white);
            }
            r.countryFlag(country, 174, y + 14, 30, 21);
            const float nameSize = std::min(18.f, 415.f / std::max(1.f, r.textWidth(name, 1, true)));
            r.text(name, 222, y + 13, nameSize, colors::white, true);
            r.text(number(total), 1034, y + 13, 18, colors::white, true);
            r.textRight(std::to_string(points), 1334, y + 13, 19, colors::white, true);
        }
        if (pages > 1) {
            smallButton("Previous", 66, 817, 119, Action::CupSetupPage, -1);
            smallButton("Next", 202, 817, 119, Action::CupSetupPage, 1);
        }
        button(final ? "Finish cup" : "Next hill", 1128, 808, 246, Action::Next, 0, true);
        smallButton("Save & menu", 922, 816, 184, Action::Back);
    }
    void replayFilesUI() {
        auto &r = *renderer;
        top("Replays");
        r.text(replayDirectory.string().substr(0, 135), 66, 245, 14, colors::teal);
        smallButton("Parent folder", 65, 282, 220, Action::ReplayParent);
        if (hasReplay)
            smallButton("Last replay", 1160, 282, 215, Action::Replay);
        for (int row = 0, i = replayPage * 8; i < std::min(int(replayFiles.size()), (replayPage + 1) * 8);
             ++row, ++i) {
            const auto &path = replayFiles[size_t(i)];
            button((std::filesystem::is_directory(path) ? "/ " : "") + path.filename().string(), 65,
                   345 + row * 53, 1303, Action::ReplayFile, i);
        }
        if (replayFiles.empty())
            r.text("No replays", 66, 363, 18, colors::muted);
        const int pages = std::max(1, (int(replayFiles.size()) + 7) / 8);
        if (pages > 1) {
            if (replayPage > 0)
                smallButton("Previous", 65, 808, 110, Action::ReplayBrowsePage, -1);
            r.text(std::to_string(replayPage + 1) + " / " + std::to_string(pages), 194, 820, 13,
                   colors::muted);
            if (replayPage + 1 < pages)
                smallButton("Next", 330, 808, 110, Action::ReplayBrowsePage, 1);
        }
    }
    void replayUI() {
        auto &r = *renderer;
        constexpr float left = ReplayLayout::left, right = ReplayLayout::right,
                        top = ReplayLayout::top, padding = 20, buttonWidth = 142;
        const std::string title = hills()[lastReplay.hill].name + " / " + number(lastReplay.result.distance) + " m";
        const std::string metadata = canExportOriginal
            ? originalReplayData.athleteName + " / " + originalReplayData.date + " / " + originalReplayData.author
            : std::string{};
        const std::string weather = environment::description(replayEnvironment);
        const float titleWidth = std::clamp(r.textWidth(title, 15, true), 210.f, 440.f),
                    detailWidth = std::clamp(std::max(r.textWidth(metadata, 10), r.textWidth(weather, 11)),
                                             200.f, 520.f),
                    detailX = left + padding + titleWidth + 28;
        r.glassPanel(left, top, detailX + detailWidth + padding - left, 88, 12, .86f);
        r.pill("REPLAY", left + padding, top + 14);
        r.text(title, left + padding, top + 52,
               std::min(15.f, titleWidth / std::max(1.f, r.textWidth(title, 1, true))), colors::white, true);
        if (canExportOriginal)
            r.text(metadata, detailX, top + 22,
                   std::min(10.f, detailWidth / std::max(1.f, r.textWidth(metadata, 1))), colors::muted);
        r.text(weather, detailX, top + (canExportOriginal ? 54 : 37), 11, colors::muted);

        smallButton("Back", right - buttonWidth, top, buttonWidth, Action::Back);
        smallButton("Camera " + std::to_string(camera + 1), right - buttonWidth, top + 45,
                    buttonWidth, Action::Camera);
        smallButton("Export .PVR", right - buttonWidth, top + 90, buttonWidth, Action::ReplayExportNative);
        if (canExportOriginal)
            smallButton("Export .RPL", right - buttonWidth, top + 135, buttonWidth, Action::ReplayExport);

        constexpr float panelHeight = ReplayLayout::controlsHeight, panelY = ReplayLayout::controlsTop,
                        controlsY = panelY + (panelHeight - 35) / 2,
                        trackY = panelY + (panelHeight - 7) / 2;
        r.glassPanel(left, panelY, right - left, panelHeight, 12, .9f);
        smallButton(replayPlaying && replayTime < replayDuration() ? "Pause" : "Play",
                    left + 18, controlsY, 74, Action::ReplayPlay);
        const std::string timing = number(replayTime) + " / " + number(replayDuration()) + " s";
        r.text(timing, left + 108, panelY + 21,
               std::min(11.f, 108.f / std::max(1.f, r.textWidth(timing, 1))), colors::muted);
        const float playedWidth = float(ReplayLayout::seekWidth * replayTime / std::max(.01, replayDuration()));
        r.rect(ReplayLayout::seekX, trackY, ReplayLayout::seekWidth, 7, {.02f, .06f, .09f, 1}, 4);
        r.rect(ReplayLayout::seekX, trackY + 1, playedWidth, 5, colors::teal, 3);
        r.rect(ReplayLayout::seekX + playedWidth - 4, trackY - 4, 8, 15, colors::white, 4);
        buttons.push_back({ReplayLayout::seekX, controlsY, ReplayLayout::seekWidth, 35, Action::ReplaySeek});
        smallButton("-", right - 184, controlsY, 33, Action::ReplaySlow);
        const std::string speed = number(replaySpeed, 2) + "x";
        r.text(speed, right - 102 - r.textWidth(speed, 13, true) / 2, panelY + 19, 13, colors::teal, true);
        smallButton("+", right - 53, controlsY, 33, Action::ReplayFast);

        // Draw after all replay controls; the hint never participates in hit testing.
        for (const auto &b : buttons) {
            if ((b.action != Action::ReplayExportNative && b.action != Action::ReplayExport) ||
                mouseX < b.x || mouseX > b.x + b.w || mouseY < b.y || mouseY > b.y + b.h)
                continue;
            const bool native = b.action == Action::ReplayExportNative;
            const std::string heading = native ? "The Perfect V replay (.PVR)" : "Deluxe Ski Jump 2 replay (.RPL)";
            const std::array<const char *, 2> lines = native
                ? std::array<const char *, 2>{"Preserves the jumper, time of day and weather.",
                                             "Open it again in The Perfect V."}
                : std::array<const char *, 2>{"For use in the original Deluxe Ski Jump 2.",
                                             "Time of day and weather are not included."};
            const float width = std::max({r.textWidth(heading, 14, true), r.textWidth(lines[0], 13),
                                          r.textWidth(lines[1], 13)}) + 32,
                        x = b.x - width - 14;
            r.glassPanel(x, b.y, width, 94, 10, .98f);
            r.text(heading, x + 16, b.y + 14, 14, colors::white, true);
            r.text(lines[0], x + 16, b.y + 42, 13, colors::muted);
            r.text(lines[1], x + 16, b.y + 63, 13, colors::muted);
            break;
        }
    }
    void render() {
        buttons.clear();
        bool panorama = screen == Screen::Menu || screen == Screen::Hills || screen == Screen::Help ||
                        screen == Screen::Graphics ||
                        screen == Screen::Settings || screen == Screen::Records ||
                        screen == Screen::Standings || screen == Screen::CupSetup ||
                        screen == Screen::ReplayFiles || screen == Screen::SkiJumper;
        Frame f = screen == Screen::Replay                                ? replayFrame(replayTime)
                  : screen == Screen::Playing || screen == Screen::Paused ? sim.renderFrame(accumulator)
                                                                          : sim.frame();
        int hi = screen == Screen::Replay ? lastReplay.hill : selectedHill;
        if (panorama) {
            Simulation gate(hi, 2);
            f = gate.frame();
        }
        Frame g;
        bool showGhost = ghost && hasReplay && lastReplay.hill == selectedHill && screen == Screen::Playing &&
                         !sim.frames.empty();
        if (showGhost)
            g = replayFrame(f.elapsed - sim.frames.front().elapsed);
        auto conditions = activeEnvironment;
        if (screen == Screen::Replay)
            conditions = replayEnvironment;
        else if (competition)
            conditions = cup.visualEnvironment();
        else if (panorama)
            conditions = environment::resolve(records.environment, 0x7abc9013U);
        renderer->setEnvironment(conditions);
        renderer->setJumperAppearance(screen == Screen::Replay ? lastReplay.jumper : records.jumper);
        renderer->setHillRecord(records.best[size_t(hi)]);
        renderer->begin(hills()[hi], f, time, camera, panorama, showGhost, showGhost ? &g : nullptr);
        if (smoke && !renderer->sceneReady()) {
            renderer->waitForScene();
            renderer->begin(hills()[hi], f, time, camera, panorama, showGhost, showGhost ? &g : nullptr);
        }
        renderer->overlay();
        // Menus remain visible and interactive while their new background
        // venue loads. The renderer keeps drawing the previous complete scene.
        if (!renderer->sceneReady() && !panorama) {
            renderer->glassPanel(485, 373, 470, 130);
            renderer->text("Preparing hill", 525, 400, 27, colors::white, true);
            renderer->text(hills()[hi].name, 525, 446, 16, colors::muted);
            return;
        }
        switch (screen) {
        case Screen::Menu:
            menu();
            break;
        case Screen::Hills:
            hillSelect();
            break;
        case Screen::Help:
            help();
            break;
        case Screen::Settings:
            settings();
            break;
        case Screen::Graphics:
            graphicsSettings();
            break;
        case Screen::Records:
            recordScreen();
            break;
        case Screen::Playing:
            hud();
            break;
        case Screen::Paused:
            hud();
            paused();
            break;
        case Screen::Result:
            result();
            break;
        case Screen::Standings:
            standings();
            break;
        case Screen::CupSetup:
            cupSetupUI();
            break;
        case Screen::SkiJumper:
            skiJumperUI();
            break;
        case Screen::JumpReady:
            jumpReadyUI();
            break;
        case Screen::ReplayFiles:
            replayFilesUI();
            break;
        case Screen::Replay:
            replayUI();
            break;
        }
        if (!message.empty()) {
            const float messageY = screen == Screen::Replay ? ReplayLayout::controlsTop - 52 : 852;
            renderer->rect(38, messageY, screen == Screen::Replay ? ReplayLayout::right - ReplayLayout::left : 1360,
                           40, colors::panel, 5);
            renderer->text(message.substr(0, 160), 50, messageY + 12, 12, colors::white);
        }
        if (editingName != -1) {
            renderer->rect(330, 290, 780, 215, colors::panel, 12);
            renderer->text(editingCountry ? "Country code" : "Name", 363, 321, 22, colors::white, true);
            renderer->text(editText + "_", 363, 379, 27, colors::teal, true);
            renderer->text("Enter: Save    Esc: Cancel", 363, 456, 14, colors::muted);
        }
        if (storageError) {
            renderer->rect(38, 115, 510, 38, {.4f, .16f, .12f, .9f}, 5);
            renderer->text("Progress could not be saved. Check the save directory.", 50, 127, 12,
                           colors::white);
        }
    }
    int smokeTest() {
        size_t cupButtonClicks = 0;
        auto requireCapture = [&](bool expected) {
            if (smokeMouseCapture && bool(SDL_GetWindowRelativeMouseMode(window)) != expected)
                throw std::runtime_error("Unexpected relative mouse mode on cup screen");
        };
        auto clickButton = [&](Action requested, int value = -1) {
            render();
            renderer->finish();
            const auto button = std::find_if(buttons.begin(), buttons.end(),
                                             [&](const Button &b) {
                                                 return b.action == requested && (value < 0 || b.value == value);
                                             });
            if (button == buttons.end())
                throw std::runtime_error("Smoke test button is missing");
            int ww = 0, hh = 0;
            if (!SDL_GetWindowSize(window, &ww, &hh) || ww <= 0 || hh <= 0)
                throw std::runtime_error("Smoke test window has no usable size");
            const int x = int(((button->x + button->w / 2) * renderer->uiScale + renderer->offsetX) *
                              ww / renderer->width);
            const int y = int(((button->y + button->h / 2) * renderer->uiScale + renderer->offsetY) *
                              hh / renderer->height);
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.windowID = SDL_GetWindowID(window);
            e.motion.x = x;
            e.motion.y = y;
            if (SDL_PushEvent(&e) != 1)
                throw std::runtime_error("Could not queue smoke test pointer motion");
            e = {};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.windowID = SDL_GetWindowID(window);
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = x;
            e.button.y = y;
            if (SDL_PushEvent(&e) != 1)
                throw std::runtime_error("Could not queue smoke test button press");
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            if (SDL_PushEvent(&e) != 1)
                throw std::runtime_error("Could not queue smoke test button release");
            events();
        };
        auto cupNext = [&] {
            requireCapture(false);
            const auto before = mouseCaptureTransitions;
            if (smokeMouseCapture) {
                clickButton(Action::Next);
                ++cupButtonClicks;
            } else
                action(Action::Next);
            requireCapture(false);
            if (mouseCaptureTransitions != before)
                throw std::runtime_error("Next unnecessarily changed relative mouse mode");
        };
        auto startTurn = [&] {
            requireCapture(false);
            const auto before = mouseCaptureTransitions;
            if (smokeMouseCapture)
                clickButton(Action::StartTurn);
            else
                action(Action::StartTurn);
            requireCapture(true);
            if (screen != Screen::Playing ||
                (smokeMouseCapture && mouseCaptureTransitions != before + 1))
                throw std::runtime_error("Starting a cup turn did not capture the pointer once");
        };
        worldSetup = defaultCupSetup();
        teamSetup = defaultTeamCupSetup();
        // Control test uses a reproducible wind override. The cup below uses
        // the original randomized, continuously carried weather.
        windMode = 2;
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/menu.ppm");
        renderer->finish();
        clickButton(Action::SkiJumper);
        if (screen != Screen::SkiJumper)
            throw std::runtime_error("Home Ski Jumper button failed");
        const Simulation appearanceBaseline = sim;
        for (int choice : {1, 2, 3, 0, 3}) {
            clickButton(Action::SelectJumper, choice);
            if (int(records.jumper) != choice ||
                int(loadRecords(dataDir + "/records-dsj2.dat").jumper) != choice ||
                sim.x != appearanceBaseline.x || sim.y != appearanceBaseline.y ||
                sim.vx != appearanceBaseline.vx || sim.vy != appearanceBaseline.vy ||
                sim.pose.angles != appearanceBaseline.pose.angles ||
                sim.originalCounter != appearanceBaseline.originalCounter ||
                sim.initialRandomSeed() != appearanceBaseline.initialRandomSeed())
                throw std::runtime_error("Jumper selection lost persistence or changed simulation");
        }
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/ski-jumper.ppm");
        renderer->finish();
        clickButton(Action::Back);
        clickButton(Action::Hills);
        if (screen != Screen::Hills)
            throw std::runtime_error("Home hill selector failed");
        // Exercise ordinary asynchronous rendering: the smoke path normally
        // waits for loading and would hide a one-frame menu disappearance.
        const int previousHill = selectedHill;
        smoke = false;
        for (int venue : {hillCount() - 1, hillCount() - 2}) {
            action(Action::SelectHill, venue);
            render();
            const bool hasSelection = std::any_of(buttons.begin(), buttons.end(), [](const Button &b) {
                return b.action == Action::SelectHill;
            });
            const bool hasPractice = std::any_of(buttons.begin(), buttons.end(), [](const Button &b) {
                return b.action == Action::Practice;
            });
            if (!hasSelection || !hasPractice || gfx::frameStats().submittedIndices == 0)
                throw std::runtime_error("Loading a hill removed the menu or the previous scenery");
            renderer->finish();
        }
        smoke = true;
        action(Action::SelectHill, previousHill);
        clickButton(Action::Back);
        clickButton(Action::Records);
        if (screen != Screen::Records)
            throw std::runtime_error("Home Records link failed");
        clickButton(Action::Back);
        clickButton(Action::Settings);
        if (screen != Screen::Settings)
            throw std::runtime_error("Home Settings link failed");
        const auto savedGraphics = records.graphics;
        const bool savedNoMsaa = noMsaa;
        const auto graphicsBaseline = sim.frame();
        clickButton(Action::Graphics);
        if (screen != Screen::Graphics)
            throw std::runtime_error("Settings Graphics link failed");
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/graphics.ppm");
        renderer->finish();
        const int previousSamples = gfx::sampleCount();
        int availableLevels = 0;
        for (int count : {0, 2, 4, 8})
            availableLevels += gfx::supportsSampleCount(count);
        for (int choice = 0; choice < availableLevels; ++choice) {
            clickButton(Action::Antialiasing);
            render();
            renderer->finish();
            const auto restoredGraphics = loadRecords(dataDir + "/records-dsj2.dat").graphics;
            if (restoredGraphics.msaaSamples != gfx::sampleCount() ||
                !gfx::supportsSampleCount(gfx::sampleCount()) ||
                restoredGraphics.vsync != gfx::vsyncEnabled())
                throw std::runtime_error("MSAA must use supported levels and persist graphics preferences");
        }
        if (gfx::sampleCount() != previousSamples)
            throw std::runtime_error("Antialiasing levels did not cycle back to the initial setting");
        for (int toggle = 0; toggle < 2; ++toggle) {
            clickButton(Action::Vsync);
            render();
            renderer->finish();
            const auto restoredGraphics = loadRecords(dataDir + "/records-dsj2.dat").graphics;
            if (restoredGraphics.vsync != gfx::vsyncEnabled() ||
                gfx::sampleCount() != previousSamples)
                throw std::runtime_error("VSync must persist the supported mode without changing MSAA");
        }
        if (sim.frame().x != graphicsBaseline.x || sim.frame().y != graphicsBaseline.y ||
            sim.frame().pose != graphicsBaseline.pose || sim.frame().elapsed != graphicsBaseline.elapsed)
            throw std::runtime_error("Graphics settings changed the simulation");
        records.graphics = savedGraphics;
        noMsaa = savedNoMsaa;
        gfx::configureGraphics(noMsaa ? 0 : records.graphics.msaaSamples, false);
        savePreferences();
        clickButton(Action::Back);
        if (screen != Screen::Settings)
            throw std::runtime_error("Graphics Back did not return to Settings");
        clickButton(Action::Help);
        if (screen != Screen::Help)
            throw std::runtime_error("Settings Controls link failed");
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/controls.ppm");
        renderer->finish();
        clickButton(Action::Back);
        if (screen != Screen::Settings)
            throw std::runtime_error("Controls did not return to Settings");
        clickButton(Action::Back);
        clickButton(Action::Cup);
        if (screen != Screen::CupSetup)
            throw std::runtime_error("Home Cup button failed");
        clickButton(Action::Back);
        clickButton(Action::ReplayBrowse);
        if (screen != Screen::ReplayFiles)
            throw std::runtime_error("Home Replays button failed");
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/replay-files.ppm");
        renderer->finish();
        clickButton(Action::Back);
        // Exercise the actual menu hit testing, keyboard start, pause and mouse takeoff.
        clickButton(Action::Practice);
        if (screen != Screen::Playing)
            throw std::runtime_error("Practice button input failed");
        const double practiceRecordBefore = records.best[size_t(selectedHill)];
        auto key = [&](SDL_Keycode code) {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = code;
            SDL_PushEvent(&e);
            events();
        };
        auto mouse = [&](Uint8 b, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = b;
            SDL_PushEvent(&e);
            events();
        };
        key(SDLK_ESCAPE);
        if (screen != Screen::Paused)
            throw std::runtime_error("Pause input failed");
        key(SDLK_ESCAPE);
        if (screen != Screen::Playing)
            throw std::runtime_error("Resume input failed");
        key(SDLK_SPACE);
        if (!pending.start)
            throw std::runtime_error("Keyboard start input failed");
        sim.step(1. / 120, pending);
        pending = {};
        bool flightCaptured = false, mouseTakeoff = false, mouseLanding = false, satsReleased = false;
        double landingClickTime = -1;
        for (int n = 0; n < 6000 && screen == Screen::Playing; n++) {
            time += 1. / 120;
            Input in = autopilot(sim);
            if (sim.originalStage == 3 && in.takeoff && !mouseTakeoff) {
                mouse(SDL_BUTTON_LEFT, true);
                mouse(SDL_BUTTON_RIGHT, true);
                if (!pending.takeoff)
                    throw std::runtime_error("Mouse takeoff input failed");
                in.takeoff = pending.takeoff;
                pending = {};
                mouseTakeoff = true;
            }
            if (mouseTakeoff && sim.originalStage >= 6 && !satsReleased) {
                // A held sats button cannot also begin a landing.
                in.land = Landing::None;
                in.buttonMask = 3;
                sim.step(ORIGINAL_TICK, in);
                if (sim.originalStage != 6 || sim.landing != Landing::None)
                    throw std::runtime_error("Takeoff release guard failed");
                mouse(SDL_BUTTON_LEFT, false);
                mouse(SDL_BUTTON_RIGHT, false);
                satsReleased = true;
            }
            if (sim.originalStage == 7 && in.land != Landing::None && landingClickTime < 0) {
                mouse(SDL_BUTTON_RIGHT, true);
                landingClickTime = time;
            }
            if (landingClickTime >= 0 && time - landingClickTime > .3 && !mouseLanding) {
                mouse(SDL_BUTTON_LEFT, true);
                mouseLanding = true;
            }
            in.land = Landing::None;
            in.buttonMask = int(mouseLeft) + 2 * int(mouseRight);
            if (sim.originalStage == 9 && mouseLanding) {
                if (sim.telemarkWidth < .25 || sim.telemarkWidth > .4 || sim.landing != Landing::Telemark)
                    throw std::runtime_error("Measured mouse telemark width failed");
                mouse(SDL_BUTTON_LEFT, false);
                mouse(SDL_BUTTON_RIGHT, false);
                in.buttonMask = 0;
            }
            sim.step(1. / 120, in);
            updateAudio(1. / 120);
            if (n % 4 == 0) {
                render();
                if (sim.phase == Phase::Flight && sim.phaseTime > 1.4 && !flightCaptured) {
                    flightCaptured = true;
                    if (!captureDir.empty())
                        renderer->capture(captureDir + "/flight.ppm");
                }
                renderer->finish();
            }
            if (sim.phase == Phase::Finished || sim.phase == Phase::Disqualified)
                completeJump();
        }
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/result.ppm");
        renderer->finish();
        std::cout << "Smoke test: distance=" << sim.result.distance << " m, score=" << sim.result.total
                  << ", landed=" << !sim.crashed << ", frames=" << sim.frames.size() << '\n';
        if (screen != Screen::Result || !flightCaptured || !mouseTakeoff || !mouseLanding || sim.crashed ||
            sim.result.distance < 40) {
            std::cerr << "Control test failed: screen=" << int(screen) << ", phase=" << int(sim.phase)
                      << ", flight=" << flightCaptured << ", takeoff=" << mouseTakeoff
                      << ", landing=" << mouseLanding << '\n';
            return 1;
        }
        if (records.best[size_t(selectedHill)] != std::max(practiceRecordBefore, sim.result.distance) ||
            loadRecords(dataDir + "/records-dsj2.dat").best != records.best)
            throw std::runtime_error("Standing practice jump did not persist its all-time human record");
        const auto replayRecords = records.best;
        const auto selectedAppearance = records.jumper;
        if (lastReplay.jumper != selectedAppearance)
            throw std::runtime_error("Completed jump lost its recorded character");
        // A later selection must not change the archived character or its re-export.
        records.jumper = JumperAppearance::Pro;
        beginReplay();
        seekReplay(replayDuration() * .6);
        updateAudio(0);
        std::string previousExport;
        for (int copy = 0; copy < 2; ++copy) {
            clickButton(Action::ReplayExportNative);
            const std::string prefix = "Saved replay: ";
            if (message.compare(0, prefix.size(), prefix) != 0)
                throw std::runtime_error("Native replay export failed: " + message);
            const auto exportedPath = message.substr(prefix.size());
            Replay exported;
            if (std::filesystem::path(exportedPath).extension() != ".pvr" ||
                exportedPath == previousExport || !loadReplay(exportedPath, exported) ||
                !exported.hasEnvironment || !(exported.environment == replayEnvironment) ||
                exported.hill != lastReplay.hill || exported.frames.size() != lastReplay.frames.size() ||
                exported.originalRpl != lastReplay.originalRpl || exported.jumper != selectedAppearance)
                throw std::runtime_error("Native replay export lost atmosphere, character, input recording or uniqueness");
            for (size_t i = 0; i < exported.frames.size(); ++i)
                if (exported.frames[i].windSpeed != lastReplay.frames[i].windSpeed ||
                    exported.frames[i].windAngle != lastReplay.frames[i].windAngle)
                    throw std::runtime_error("Native replay export changed recorded wind");
            previousExport = exportedPath;
        }
        message.clear();
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/replay.ppm");
        renderer->finish();
        if (records.best != replayRecords || records.jumper != JumperAppearance::Pro)
            throw std::runtime_error("Replay changed hill records or the preferred character");
        records.jumper = selectedAppearance;
        action(Action::Back);
        auto dropReplay = [&](const std::string &path) {
            SDL_Event drop{};
            drop.type = SDL_EVENT_DROP_FILE;
            drop.drop.windowID = SDL_GetWindowID(window);
            drop.drop.data = path.c_str();
            if (!SDL_PushEvent(&drop))
                throw std::runtime_error("Could not queue replay file drop");
            events();
            if (screen != Screen::Replay)
                throw std::runtime_error("Dropped native replay did not open");
        };
        for (Screen origin : {Screen::Result, Screen::Playing, Screen::ReplayFiles, Screen::Menu}) {
            changeScreen(origin);
            audio->beginJump(sim.hill(), &sim.frames);
            const Screen expected = origin == Screen::Playing ? Screen::Paused : origin;
            dropReplay(previousExport);
            dropReplay(previousExport);
            clickButton(Action::Back);
            if (screen != expected)
                throw std::runtime_error("Replacing a replay lost the Back destination");
            dropReplay(previousExport);
            dropReplay(previousExport);
            SDL_Event escape{};
            escape.type = SDL_EVENT_KEY_DOWN;
            escape.key.key = SDLK_ESCAPE;
            if (!SDL_PushEvent(&escape))
                throw std::runtime_error("Could not queue replay Escape");
            events();
            if (screen != expected)
                throw std::runtime_error("Replacing a replay lost the Escape destination");
        }
        Simulation successfulJump = sim;
        sim.reset(0, 0);
        sim.setWind(0, 0);
        changeScreen(Screen::Playing);
        renderer->resetCamera();
        bool fallCaptured = false, fallAirborne = false;
        for (int n = 0; n < 7000 && sim.phase != Phase::Finished; ++n) {
            Input unprepared;
            unprepared.start = sim.phase == Phase::Ready;
            unprepared.buttonMask = 0;
            sim.step(ORIGINAL_TICK, unprepared);
            if (sim.phase == Phase::Outrun && sim.y > sim.hill().ground(sim.x) + .001)
                fallAirborne = true;
            if (n % 4 == 0) {
                render();
                if (!fallCaptured && sim.originalStage == 12 && sim.phaseTime > 1.5 && !captureDir.empty())
                    renderer->capture(captureDir + "/fall.ppm");
                renderer->finish();
                if (sim.originalStage == 12 && sim.phaseTime > 1.5)
                    fallCaptured = true;
            }
        }
        if (sim.phase != Phase::Finished || !sim.crashed || !fallCaptured || !fallAirborne ||
            std::hypot(sim.vx, sim.vy) > .010001)
            throw std::runtime_error("Original fall and outrun test failed");
        sim = std::move(successfulJump);
        windMode = 0;
        // Render every venue and every screen, then exercise the complete two-round cup.
        for (int i = 0; i < hillCount(); i++) {
            action(Action::SelectHill, i);
            changeScreen(Screen::Menu);
            render();
            renderer->finish();
        }
        for (Screen s : {Screen::Hills, Screen::Help, Screen::Settings, Screen::Records, Screen::Paused}) {
            changeScreen(s);
            if (s == Screen::Settings) {
                action(Action::SensitivityReset);
                if (records.sensitivity != 1)
                    throw std::runtime_error("Original mouse calibration reset failed");
            }
            render();
            if (s == Screen::Settings && !captureDir.empty())
                renderer->capture(captureDir + "/settings.ppm");
            renderer->finish();
        }
        changeScreen(Screen::Settings);
        for (int dayTime = 0; dayTime <= 4; ++dayTime) {
            clickButton(Action::TimeOfDay, dayTime);
            for (int weather = 0; weather <= 4; ++weather) {
                clickButton(Action::Weather, weather);
                render();
                if (!(renderer->environmentConditions() == environment::resolve(records.environment, 0x7abc9013U)) ||
                    int(records.environment.time) != dayTime || int(records.environment.weather) != weather)
                    throw std::runtime_error("Time/weather controls or preview failed");
                renderer->finish();
            }
        }
        clickButton(Action::EnvironmentReset);
        if (records.environment.time != environment::TimeOfDay::Day ||
            records.environment.weather != environment::Weather::Sunny)
            throw std::runtime_error("Day and sun defaults failed");
        // Verify every page has selectable entries and keyboard navigation reaches the final hill.
        for (Screen s : {Screen::Hills, Screen::Records}) {
            changeScreen(s);
            action(Action::HillPage, -hillPages());
            for (int page = 0; page < hillPages(); page++) {
                if (hillPage != page)
                    throw std::runtime_error("Hill pagination input failed");
                render();
                int available = 0;
                for (const auto &b : buttons)
                    if (b.action == Action::SelectHill) {
                        if (b.value < page * HILLS_PER_PAGE ||
                            b.value >= std::min((page + 1) * HILLS_PER_PAGE, hillCount()))
                            throw std::runtime_error("Hill page contains the wrong profile");
                        available++;
                    }
                if (available != std::min(HILLS_PER_PAGE, hillCount() - page * HILLS_PER_PAGE))
                    throw std::runtime_error("Hill page is missing selectable profiles");
                if (!captureDir.empty())
                    renderer->capture(captureDir + (s == Screen::Hills ? "/hills-page-" : "/records-page-") +
                                      std::to_string(page + 1) + ".ppm");
                renderer->finish();
                if (page + 1 < hillPages())
                    key(SDLK_PAGEDOWN);
            }
            action(Action::SelectHill, hillCount() - 1);
            key(SDLK_RIGHT);
            if (selectedHill != 0 || hillPage != 0)
                throw std::runtime_error("Hill selection does not wrap to the first profile");
            key(SDLK_LEFT);
            if (selectedHill != hillCount() - 1 || hillPage != hillPages() - 1)
                throw std::runtime_error("Hill selection does not reach the final profile");
        }
        selectedHill = 2;
        setupTeam = false;
        changeScreen(Screen::CupSetup);
        clickButton(Action::CupPlayerType, 1);
        clickButton(Action::CupPlayerType, 1);
        if (!worldSetup.participants[1].computer || !nationality(worldSetup.participants[1].country) ||
            worldSetup.participants[1].name.find(' ') == std::string::npos)
            throw std::runtime_error("Converting a participant to CPU did not generate an identity");
        clickButton(Action::CupPlayerCountry, 1);
        editText = "ZZZ";
        finishNameEdit(true);
        if (editingName == -1 || message.empty())
            throw std::runtime_error("Unknown nationality was silently accepted");
        editText = "deu";
        finishNameEdit(true);
        if (editingName != -1 || worldSetup.participants[1].country != "GER")
            throw std::runtime_error("Flag selector did not canonicalize an ISO country code");
        while (worldSetup.participants.size() < 16)
            action(Action::CupAddPlayer);
        for (int page = 0; page < 2; ++page) {
            setupPage = page;
            render();
            int names = 0;
            for (const auto &b : buttons)
                if (b.action == Action::CupPlayerName)
                    ++names;
            if (names != 8)
                throw std::runtime_error("Cup setup is missing participants on its second page");
            if (!captureDir.empty())
                renderer->capture(captureDir + "/world-setup-page-" + std::to_string(page + 1) + ".ppm");
            renderer->finish();
        }
        worldSetup = defaultCupSetup();
        setupPage = 0;
        records.environment = {environment::TimeOfDay::Random, environment::Weather::Random};
        const auto beforeCup = mouseCaptureTransitions;
        beginCup();
        requireCapture(false);
        if (mouseCaptureTransitions != beforeCup)
            throw std::runtime_error("Preparing the cup unnecessarily captured the pointer");
        int cupFalls = 0;
        for (int event = 0; event < hillCount(); event++) {
            const auto fixedEnvironment = cup.visualEnvironment();
            for (int jump = 0; jump < 2; jump++) {
                if (screen != Screen::JumpReady)
                    throw std::runtime_error("Cup hotseat readiness missing");
                if (!(activeEnvironment == fixedEnvironment))
                    throw std::runtime_error("Time/weather changed within one cup event");
                startTurn();
                for (int n = 0; n < 6000 && screen == Screen::Playing; n++) {
                    sim.step(1. / 120, autopilot(sim));
                    updateAudio(1. / 120);
                    if (sim.phase == Phase::Finished)
                        completeJump();
                }
                if (screen != Screen::Result || sim.phase != Phase::Finished ||
                    !std::isfinite(sim.result.distance) || !std::isfinite(sim.result.total) ||
                    sim.result.distance < 0 || !hasReplay || lastReplay.hill != selectedHill ||
                    lastReplay.frames.size() != sim.frames.size() || lastReplay.frames.size() < 2) {
                    std::cerr << "Cup simulation failed at event " << event + 1 << ", jump " << jump + 1
                              << '\n';
                    return 1;
                }
                if (!lastReplay.hasEnvironment || !(lastReplay.environment == fixedEnvironment))
                    throw std::runtime_error("Cup replay lost its fixed event atmosphere");
                Replay restored;
                if (storageError || !loadReplay(dataDir + "/latest-dsj2.pvr", restored) ||
                    restored.hill != selectedHill || restored.frames.size() != sim.frames.size() ||
                    restored.result.crashed != sim.result.crashed || restored.jumper != records.jumper) {
                    std::cerr << "Cup replay persistence failed at event " << event + 1 << '\n';
                    return 1;
                }
                if (sim.crashed)
                    cupFalls++;
                cupNext();
            }
            render();
            if (!(renderer->environmentConditions() == fixedEnvironment))
                throw std::runtime_error("Cup standings changed the event atmosphere");
            if (!captureDir.empty() && (event == 0 || event + 1 == hillCount()))
                renderer->capture(captureDir + (event == 0 ? "/cup-event.ppm" : "/cup-final.ppm"));
            renderer->finish();
            if (event + 1 < hillCount())
                cupNext();
            if (smokeMouseCapture && (event + 1) % 8 == 0)
                std::cout << "Mouse capture cup: " << event + 1 << '/' << hillCount()
                          << " hills checked\n" << std::flush;
        }
        if (screen != Screen::Standings || !finalCupEvent())
            throw std::runtime_error("The complete cup did not reach final standings");
        cupNext();
        if (screen != Screen::Menu)
            throw std::runtime_error("Finishing the cup did not return to the main menu");
        records.environment = {};
        // Exercise configuration, checkpoint reload, human handoff and Team Cup frontend.
        action(Action::Cup);
        action(Action::CupSetupMode, 1);
        teamSetup.teams[0].name = "TEAM";
        teamSetup.participants[0].name = "PRESERVED";
        action(Action::CupTeamCount, -1);
        if (teamSetup.teams[0].name != "TEAM" || teamSetup.participants[0].name != "PRESERVED")
            throw std::runtime_error("Changing team count overwrote an existing team");
        teamSetup.teams[0].name = "NORWAY";
        action(Action::CupTeamCount, -1);
        clickButton(Action::CupPlayerCountry, 4);
        if (editingName != -3)
            throw std::runtime_error("Member flag did not select the team's nationality");
        editText = "JPN";
        finishNameEdit(true);
        for (int i = 4; i < 8; ++i)
            if (teamSetup.participants[size_t(i)].country != "JPN" ||
                teamSetup.participants[size_t(i)].name.find(' ') == std::string::npos)
                throw std::runtime_error("Team nationality change did not update its CPU members");
        teamSetup.hills = {0, 1};
        action(Action::CupPlayerName, 0);
        editText = "HOTSEAT ONE";
        finishNameEdit(true);
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/team-setup.ppm");
        renderer->finish();
        action(Action::CupSetupTab, 1);
        render();
        renderer->finish();
        action(Action::CupStart);
        int teamJumps = 0;
        while (!cup.complete()) {
            if (screen == Screen::JumpReady) {
                Competition restored;
                if (!loadCompetition(dataDir + "/cup.sjc", restored) ||
                    restored.currentJumper() != cup.currentJumper() ||
                    restored.preparedJumpSeed() != cup.preparedJumpSeed())
                    throw std::runtime_error("Hotseat cup checkpoint failed");
                if (teamJumps == 0) {
                    action(Action::Back);
                    const auto beforeResume = mouseCaptureTransitions;
                    clickButton(Action::CupResume);
                    requireCapture(false);
                    if (mouseCaptureTransitions != beforeResume)
                        throw std::runtime_error("Resuming the cup unnecessarily captured the pointer");
                    render();
                    if (!captureDir.empty())
                        renderer->capture(captureDir + "/hotseat.ppm");
                    renderer->finish();
                }
                startTurn();
                for (int n = 0; n < 15000 && screen == Screen::Playing; ++n) {
                    sim.step(ORIGINAL_TICK, autopilot(sim));
                    if (sim.phase == Phase::Finished || sim.phase == Phase::Disqualified)
                        completeJump();
                }
                if (screen != Screen::Result)
                    throw std::runtime_error("Team Cup jump failed");
                ++teamJumps;
                cupNext();
            } else if (screen == Screen::Standings) {
                render();
                if (!captureDir.empty())
                    renderer->capture(captureDir + "/team-standings.ppm");
                renderer->finish();
                cupNext();
            } else
                throw std::runtime_error("Unexpected Team Cup screen");
        }
        render();
        renderer->finish();
        action(Action::Back);
        worldSetup = defaultCupSetup();
        worldSetup.participants.resize(2);
        worldSetup.hills = {0};
        worldSetup.participants[0].computer = true;
        worldSetup.participants[0].level = 0;
        worldSetup.participants[1].computer = false;
        worldSetup.participants[1].name = "HOTSEAT TWO";
        worldSetup.skipCpu = false;
        setupTeam = false;
        beginCup();
        int visibleJumps = 0;
        while (!cup.complete()) {
            if (screen == Screen::JumpReady) {
                const bool cpuJumper = activeComputer();
                const auto beforeRecords = records.best;
                startTurn();
                for (int n = 0; n < 20000 && screen == Screen::Playing; ++n) {
                    sim.step(ORIGINAL_TICK, autopilot(sim));
                    if (sim.phase == Phase::Finished || sim.phase == Phase::Disqualified)
                        completeJump();
                }
                if (screen != Screen::Result)
                    throw std::runtime_error("Visible CPU/human cup jump failed");
                if (cpuJumper && records.best != beforeRecords)
                    throw std::runtime_error("Visible CPU jump set a hill record");
                ++visibleJumps;
                Competition restored;
                if (!loadCompetition(dataDir + "/cup.sjc", restored) ||
                    restored.currentJumper() != cup.currentJumper() ||
                    restored.entries()[0].completed != cup.entries()[0].completed)
                    throw std::runtime_error("Completed result checkpoint failed");
                if (visibleJumps == 1) {
                    action(Action::Back);
                    clickButton(Action::CupResume);
                } else
                    cupNext();
            } else if (screen == Screen::Standings)
                cupNext();
            else
                throw std::runtime_error("Unexpected visible cup screen");
        }
        if (visibleJumps != 4)
            throw std::runtime_error("Visible CPU/human cup lost or duplicated a jump");
        // The standalone source distribution does not include the original demo.
        // Exercise its frontend import only when the optional local fixture exists.
        const std::string originalReplayFixture = std::string(SJP_ASSET_DIR) + "/../reference/EXAMPLE.RPL";
        if (std::filesystem::is_regular_file(originalReplayFixture)) {
            // Import the source replay through the same frontend as file browsing/drag-drop.
            if (!openReplayFile(originalReplayFixture) || !canExportOriginal || lastReplay.hill != 0)
                throw std::runtime_error("Original RPL frontend import failed");
            render();
            if (!captureDir.empty())
                renderer->capture(captureDir + "/original-replay.ppm");
            renderer->finish();
            if (!saveReplay(dataDir + "/imported.pvr", lastReplay) ||
                !openReplayFile(dataDir + "/imported.pvr") || !canExportOriginal)
                throw std::runtime_error("Original RPL attachment persistence failed");
            action(Action::ReplayExport);
        } else {
            std::cout << "Smoke test: optional original replay fixture absent; import validation skipped\n";
        }
        action(Action::Back);
        action(Action::ReplayBrowse);
        render();
        if (!captureDir.empty())
            renderer->capture(captureDir + "/replay-library.ppm");
        renderer->finish();
        sjp::gfx::finish();
        std::cout << "Smoke test: all " << hillCount() << " venues, screens, replay and " << hillCount() * 2
                  << "-jump cup passed (" << cupFalls << " falls). Vulkan submissions completed\n";
        if (smokeMouseCapture)
            std::cout << "Mouse capture test: " << mouseCaptureTransitions << " transitions, "
                      << cupButtonClicks << " Next button clicks; no capture on cup preparation\n";
        return 0;
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
#if defined(__APPLE__) && defined(__x86_64__)
        // MoltenVK 1.4.2 queries an indirect argument encoder during startup;
        // Intel's paravirtual Metal driver aborts in that query. Conventional
        // descriptor binding passes the full renderer test. Set the process
        // environment before SDL starts threads; honor an explicit user override.
        if (SDL_setenv_unsafe("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0", 0) != 0)
            throw std::runtime_error("Could not configure MoltenVK descriptor binding");
#endif
        App app(argc, argv);
        return app.run();
    } catch (const std::exception &e) {
        std::cerr << "The Perfect V: " << e.what() << '\n';
#if defined(_WIN32) || defined(__APPLE__)
        bool automated = false;
        for (int i = 1; i < argc; ++i)
            automated = automated || std::string(argv[i]) == "--smoke-test";
        if (!automated)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "The Perfect V", e.what(), nullptr);
#endif
        return 1;
    }
}
