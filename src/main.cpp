#include "audio.h"
#include "competition.h"
#include "original_replay.h"
#include "physics.h"
#include "renderer.h"
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
    localtime_r(&seconds, &local);
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
enum class Screen {
    Menu,
    Hills,
    Help,
    Settings,
    Records,
    Playing,
    Paused,
    Result,
    Standings,
    Replay,
    CupSetup,
    JumpReady,
    ReplayFiles
};
enum class Action {
    None,
    Practice,
    Cup,
    Hills,
    Help,
    Settings,
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
    ReplayExportNative
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
    Screen screen = Screen::Menu, replayReturn = Screen::Menu;
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
    std::string editText, message, replayExportError, completedName;
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
                             "original .RPL or native .sjr replay\n"
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
        window = SDL_CreateWindow("The Perfect V", 1440, 900,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                      (hidden ? SDL_WINDOW_HIDDEN : 0));
        if (!window)
            throw std::runtime_error(std::string("Window creation failed: ") + SDL_GetError());
        SDL_SetWindowMinimumSize(window, 960, 600);
        std::string assets;
        const char *base = SDL_GetBasePath();
        std::filesystem::path executableDir = base ? base : ".";
        for (auto candidate :
             {executableDir / "assets", executableDir / "../assets", executableDir / "../share/the-perfect-v",
              std::filesystem::path(SJP_ASSET_DIR)}) {
            if (std::filesystem::exists(candidate / "fonts/Interim-Regular.ttf")) {
                assets = candidate.string();
                break;
            }
        }
        if (assets.empty())
            throw std::runtime_error("Game fonts are missing. Keep the assets directory with the game.");
        renderer = std::make_unique<Renderer>(window, assets, noMsaa, !smoke, gpuValidation);
        renderer->setSynchronousRendering(synchronousRendering);
        audio = std::make_unique<Audio>();
        if (dataDir.empty()) {
            // Retain the legacy namespace so the rename keeps existing saves.
            char *pref = SDL_GetPrefPath("SkiJumpPro", "SkiJumpPro");
            if (pref) {
                dataDir = pref;
                SDL_free(pref);
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
        Competition saved;
        if (loadCompetition(dataDir + "/cup.sjc", saved)) {
            hasCupSave = saved.active();
            if (saved.setup().mode == CupMode::Team)
                teamSetup = saved.setup();
            else
                worldSetup = saved.setup();
        }
        replayDirectory = dataDir;
        SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
        hasReplay = loadReplay(dataDir + "/latest-dsj2.sjr", lastReplay);
        if (hasReplay && !lastReplay.originalRpl.empty())
            canExportOriginal =
                decodeOriginalReplay(lastReplay.originalRpl, originalReplayData, &replayExportError);
        if (!replayArgument.empty())
            openReplayFile(replayArgument);
        const int sdlVersion = SDL_GetVersion();
        std::cout << "The Perfect V | " << renderer->device << "\nSaves: " << dataDir
                  << "\nGraphics: Vulkan, SDL " << SDL_VERSIONNUM_MAJOR(sdlVersion) << '.'
                  << SDL_VERSIONNUM_MINOR(sdlVersion) << '.' << SDL_VERSIONNUM_MICRO(sdlVersion)
                  << ", MSAA=" << sjp::gfx::sampleCount()
                  << ", synchronous=" << synchronousRendering << ", frames in flight=1"
                  << ", frame cap=" << (smoke ? 0 : 60) << '\n' << std::flush;
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
        visual::FrameSchedule frameSchedule(std::chrono::steady_clock::now());
        while (running) {
            uint64_t now = SDL_GetPerformanceCounter();
            double dt = std::min(.08, double(now - previous) / SDL_GetPerformanceFrequency());
            previous = now;
            time += dt;
            events();
            if (screen == Screen::Playing) {
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
                        if (sim.phase == Phase::Outrun && records.sound)
                            audio->cue(sim.crashed ? 4 : 3);
                    }
                    if (sim.phase == Phase::Finished || sim.phase == Phase::Disqualified) {
                        completeJump();
                        break;
                    }
                }
            } else if (screen != Screen::Paused)
                accumulator = 0;
            if (screen == Screen::Replay && replayPlaying)
                replayTime = std::min(replayTime + dt * replaySpeed, replayDuration());
            audio->set(screen == Screen::Playing ? std::sqrt(sim.vx * sim.vx + sim.vy * sim.vy) : 0,
                       records.sound);
            render();
            renderer->finish();
            // Bound submission rate even when VSync is unavailable or ignored.
            std::this_thread::sleep_until(frameSchedule.nextDeadline(std::chrono::steady_clock::now()));
        }
        return 0;
    }

  private:
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
        completedName = jumperName();
        completedRound = round;
        completedEvent = cupEvent;
        sessionWeather = sim.weatherState();
        sessionAuxiliary = sim.pose.auxiliary;
        bool human = !competition || !activeComputer();
        if (competition && human && !sim.result.crashed && !sim.result.disqualified &&
            sim.result.distance > records.best[selectedHill]) {
            records.best[selectedHill] = sim.result.distance;
            newRecord = true;
            if (!saveRecords(dataDir + "/records-dsj2.dat", records))
                storageError = true;
        }
        if (sim.frames.size() > 2) {
            lastReplay = sim.replay();
            lastReplay.hasEnvironment = true;
            lastReplay.environment = activeEnvironment;
            hasReplay = true;
            canExportOriginal = buildOriginalReplay(sim, originalReplayData, &replayExportError);
            if (canExportOriginal) {
                originalReplayData.athleteName = completedName.substr(0, 25);
                canExportOriginal =
                    encodeOriginalReplay(originalReplayData, lastReplay.originalRpl, &replayExportError);
            }
            if (!saveReplay(dataDir + "/latest-dsj2.sjr", lastReplay))
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
                (editingCountry ? p.country : p.name) = editText;
            } else if (editingName <= -2) {
                size_t index = size_t(-editingName - 2);
                if (index < setup.teams.size()) {
                    auto &t = setup.teams[index];
                    (editingCountry ? t.country : t.name) = editText;
                    for (auto &p : setup.participants)
                        if (p.team == int(index))
                            p.country = t.country;
                }
            }
        }
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
            if (entry.is_directory(error) || ext == ".sjr" || ext == ".rpl")
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
        if (original) {
            if (!readOriginalReplay(path, data, &error) ||
                !reconstructOriginalReplay(data, loaded, nullptr, &error)) {
                message = "Replay could not be opened: " + error;
                return false;
            }
            if (!encodeOriginalReplay(data, loaded.originalRpl, &error))
                return false;
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
        std::string path = stem + ".sjr";
        std::error_code error;
        for (unsigned suffix = 1; std::filesystem::exists(path, error); ++suffix)
            path = stem + "-" + std::to_string(suffix) + ".sjr";
        if (error || !saveReplay(path, exported)) {
            message = "Replay could not be saved. Check the save directory.";
            return;
        }
        message = "Saved replay: " + path;
    }
    void beginReplay() {
        if (!hasReplay)
            return;
        replayReturn = screen;
        replayTime = 0;
        replaySpeed = 1;
        replayPlaying = true;
        replayEnvironment = lastReplay.hasEnvironment ? lastReplay.environment
            : environment::resolve({}, canExportOriginal ? originalReplayData.seed
                                                        : visual::replayWeatherSeed(lastReplay));
        renderer->setEnvironment(replayEnvironment, true);
        renderer->resetCamera();
        changeScreen(Screen::Replay);
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
    void action(Action a, int value = 0) {
        if (editingName != -1)
            finishNameEdit(true);
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
        case Action::Hills:
            changeScreen(Screen::Hills);
            break;
        case Action::Help:
            changeScreen(Screen::Help);
            break;
        case Action::Settings:
            changeScreen(Screen::Settings);
            break;
        case Action::Records:
            changeScreen(Screen::Records);
            break;
        case Action::Quit:
            running = false;
            break;
        case Action::Back:
            if (screen == Screen::Replay) {
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
            editingCountry = a == Action::CupTeamCountry || a == Action::CupPlayerCountry;
            editingName = team ? -2 - value : value;
            editText = team ? (editingCountry ? editableSetup().teams[size_t(value)].country
                                              : editableSetup().teams[size_t(value)].name)
                            : (editingCountry ? editableSetup().participants[size_t(value)].country
                                              : editableSetup().participants[size_t(value)].name);
            SDL_StartTextInput(window);
            break;
        }
        case Action::CupPlayerType:
            editableSetup().participants[size_t(value)].computer =
                !editableSetup().participants[size_t(value)].computer;
            break;
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
            if (replayTime >= replayDuration())
                replayTime = 0;
            replayPlaying = !replayPlaying;
            break;
        case Action::ReplaySlow:
            replaySpeed = std::max(.25, replaySpeed / 2);
            break;
        case Action::ReplayFast:
            replaySpeed = std::min(2.0, replaySpeed * 2);
            break;
        case Action::ReplaySeek:
            replayTime = clamp((mouseX - 290) / 860 * replayDuration(), 0.0, replayDuration());
            renderer->resetCamera();
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
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
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
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
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
                        replayTime = std::max(0.0, replayTime - 2);
                        renderer->resetCamera();
                    }
                    if (key == SDLK_RIGHT) {
                        replayTime = std::min(replayDuration(), replayTime + 2);
                        renderer->resetCamera();
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
                bool primary = false) {
        auto &r = *renderer;
        float h = 50;
        bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
        Color bg = primary ? colors::teal : Color{.12f, .21f, .26f, .88f};
        if (hover) {
            bg.r = std::min(1.f, bg.r + .08f);
            bg.g = std::min(1.f, bg.g + .08f);
            bg.b = std::min(1.f, bg.b + .08f);
        }
        if (primary) {
            r.rect(x - 2, y + 4, w + 4, h + 1, {.01f, .035f, .045f, .2f}, 11);
            r.rect(x, y, w, h, bg, 9);
            r.gradient(x + 9, y + 2, w - 18, h - 4, {1, 1, 1, .08f}, {1, 1, 1, 0});
            r.line(x + 12, y + 1, x + w - 12, y + 1, {.82f, 1, .95f, .48f});
        } else {
            r.glassPanel(x, y, w, h, 9, hover ? .94f : .8f);
            if (hover)
                r.rect(x + 1, y + 1, w - 2, h - 2, {bg.r, bg.g, bg.b, .35f}, 8);
        }
        r.text(label, x + (w - r.textWidth(label, 15, true)) / 2, y + 15, 15,
               primary ? colors::ink : colors::white, true);
        buttons.push_back({x, y, w, h, a, value});
    }
    void smallButton(const std::string &label, float x, float y, float w, Action a, int value = 0) {
        bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + 35;
        renderer->rect(x, y, w, 35, {.34f, .5f, .55f, hover ? .43f : .22f}, 7);
        renderer->rect(x + 1, y + 1, w - 2, 33, {.06f, .13f, .17f, hover ? .97f : .75f}, 6);
        renderer->text(label, x + 12, y + 8, 13, colors::white);
        buttons.push_back({x, y, w, 35, a, value});
    }
    void brand(float x, float y) {
        auto &r = *renderer;
        r.line(x, y + 22, x + 15, y, colors::teal, 2);
        r.line(x + 15, y, x + 30, y + 22, colors::teal, 2);
        r.line(x + 19, y + 8, x + 27, y - 3, colors::white, 2);
        r.line(x + 27, y - 3, x + 43, y + 22, colors::white, 2);
        r.text("THE PERFECT V", x + 58, y - 1, 20, colors::white, true);
    }
    void top(const std::string &title, const std::string &subtitle) {
        auto &r = *renderer;
        r.rect(0, 0, 1440, 900, {.025f, .065f, .1f, .84f});
        r.gradient(0, 0, 1440, 220, {.04f, .11f, .15f, .25f}, {.04f, .11f, .15f, 0});
        brand(64, 46);
        smallButton("<  Back", 1250, 40, 126, Action::Back);
        r.text(title, 64, 123, 44, colors::white, true);
        r.text(subtitle, 66, 186, 17, colors::muted);
        r.line(65, 218, 1374, 218, {.63f, .78f, .83f, .13f});
        r.line(65, 218, 120, 218, colors::teal, 2);
    }
    void keyHint(const std::string &key, const std::string &label, float x, float y) {
        auto &r = *renderer;
        float w = r.textWidth(key, 11, true) + 16;
        r.rect(x, y + 2, w, 24, {.01f, .035f, .055f, .18f}, 5);
        r.rect(x, y, w, 24, {.35f, .51f, .56f, .5f}, 5);
        r.rect(x + 1, y + 1, w - 2, 21, {.1f, .19f, .24f, .92f}, 4);
        r.text(key, x + 8, y + 5, 11, colors::white, true);
        r.text(label, x + w + 9, y + 3, 13, colors::muted);
    }
    void menu() {
        auto &r = *renderer;
        const auto &h = hills()[selectedHill];
        r.gradient(0, 0, 1000, 900, {.035f, .09f, .13f, .98f}, {.035f, .09f, .13f, 0});
        r.gradient(0, 690, 1440, 210, {.04f, .09f, .13f, .96f}, {.04f, .09f, .13f, .7f});
        brand(68, 45);
        r.glassPanel(1111, 32, 273, 42, 9, .67f);
        r.textRight("ALPINE EDITION  /  01", 1370, 46, 12, colors::white, true);
        r.pill("THE HILL IS CALLING", 72, 160);
        r.text("Find your", 66, 211, 66, colors::white, true);
        r.text("perfect flight.", 66, 291, 66, colors::white, true);
        r.text("A moment of courage. A few seconds of freedom.", 72, 393, 17, colors::white);
        r.text(std::to_string(hillCount()) + " original hills. Real-time wind. Every metre earned.", 72, 424,
               17, colors::muted);
        button("Start practice  >", 72, 488, 244, Action::Practice, 0, true);
        button("Cup setup", 332, 488, 170, Action::Cup);
        if (hasCupSave)
            button("Resume cup", 518, 488, 190, Action::CupResume);
        smallButton("How to jump", 72, 557, 143, Action::Help);
        smallButton("Settings", 228, 557, 107, Action::Settings);
        smallButton("Records", 348, 557, 107, Action::Records);
        if (hasReplay)
            smallButton("Last replay", 72, 604, 143, Action::Replay);
        smallButton("Replay library", 228, 604, 166, Action::ReplayBrowse);
        r.glassPanel(1040, 531, 326, 133, 12, .84f);
        r.rect(1040, 548, 3, 43, colors::teal, 1);
        r.text(h.country, 1063, 548, 11, colors::teal, true);
        r.text(h.name, 1062, 571, 23, colors::white, true);
        r.text("K " + std::to_string(h.k), 1064, 609, 14, colors::muted);
        smallButton("Change hill  >", 1206, 610, 140, Action::Hills);
        r.line(72, 719, 1368, 719, {.5f, .65f, .7f, .2f});
        r.text("CHOOSE YOUR HILL", 72, 742, 11, colors::muted, true);
        smallButton("Browse all " + std::to_string(hillCount()) + "  >", 1198, 730, 170, Action::Hills);
        for (int slot = 0; slot < std::min(6, hillCount()); slot++) {
            int i = (selectedHill + slot) % hillCount();
            float x = 72 + slot * 218;
            const auto &hill = hills()[i];
            bool select = i == selectedHill;
            r.glassPanel(x, 775, 204, 65, 9, select ? .93f : .7f);
            if (select)
                r.rect(x + 1, 776, 202, 63, {.17f, .51f, .46f, .13f}, 8);
            if (select)
                r.rect(x, 775, 3, 65, colors::teal, 1);
            r.text("K" + std::to_string(hill.k), x + 14, 785, 18, select ? colors::teal : colors::white,
                   true);
            r.text(hill.name, x + 14, 815, 12, colors::muted);
            buttons.push_back({x, 775, 204, 65, Action::SelectHill, i});
        }
        r.text("MOUSE + KEYBOARD", 72, 869, 10, colors::muted, true);
        keyHint("ENTER", "Practice", 310, 860);
        keyHint("F11", "Fullscreen", 494, 860);
        smallButton("Quit", 1294, 858, 74, Action::Quit);
    }
    void hillPagination(float y) {
        auto &r = *renderer;
        int first = hillPage * HILLS_PER_PAGE;
        int last = std::min(first + HILLS_PER_PAGE, hillCount());
        r.text("HILLS " + std::to_string(first + 1) + " - " + std::to_string(last) + " OF " +
                   std::to_string(hillCount()),
               66, y + 10, 12, colors::muted, true);
        r.text("PAGE " + std::to_string(hillPage + 1) + " / " + std::to_string(hillPages()), 1040, y + 10, 12,
               colors::muted, true);
        if (hillPage > 0)
            smallButton("<  Previous", 1174, y, 98, Action::HillPage, -1);
        if (hillPage + 1 < hillPages())
            smallButton("Next  >", 1283, y, 91, Action::HillPage, 1);
    }
    void hillSelect() {
        top("Choose your hill.", "Explore all original venues. Select a hill, then start practice or visit "
                                 "every hill in the cup.");
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
            r.pill(h.flying()  ? "SKI FLYING"
                   : h.k <= 90 ? "NORMAL HILL"
                               : "LARGE HILL",
                   x + 20, y + 20, selected ? colors::teal : colors::muted);
            r.text(h.name, x + 20, y + 62, 23, colors::white, true);
            r.text(h.country, x + 21, y + 98, 11, colors::muted, true);
            r.text("K" + std::to_string(h.k), x + 20, y + 128, 34, selected ? colors::teal : colors::white,
                   true);
            r.text(records.best[i] > 0 ? "BEST  " + number(records.best[i]) + " m" : "No personal best yet",
                   x + 21, y + 187, 12, colors::muted);
            buttons.push_back({x, y, 314, 216, Action::SelectHill, i});
        }
        hillPagination(722);
        r.text("SELECTED: " + hills()[selectedHill].name + " / " + hills()[selectedHill].subtitle, 66, 778,
               14, colors::muted);
        keyHint("ARROWS", "Select hill", 66, 832);
        keyHint("PGUP / PGDN", "Pages", 277, 832);
        button("World cup", 955, 811, 157, Action::Cup);
        button("Start practice  >", 1128, 811, 246, Action::Practice, 0, true);
    }
    void help() {
        top("A few seconds. Four small moves.",
            "Authentic mouse controls, with a keyboard option while you learn.");
        auto &r = *renderer;
        struct Lesson {
            std::string n, title, one, two, key;
        };
        const Lesson lessons[] = {
            {"01", "Read the wind", "Headwind helps you fly. At the green light,",
             "left-click to start within 15 seconds.", "Keyboard: SPACE to start"},
            {"02", "Time your takeoff", "Near the board, hold both buttons through",
             "the takeoff. Then release both for landing.", "Keyboard: SPACE near the board"},
            {"03", "Find your flying position", "Move the mouse gently up and down.",
             "Keep a steady position through the flight.", "Keyboard: UP / DOWN or W / S"},
            {"04", "Finish with a telemark", "Before contact, press one button, then the other.",
             "More delay = wider telemark, more style and risk.",
             "Keyboard: T telemark / B two-foot (both buttons)"}};
        for (int i = 0; i < 4; i++) {
            float x = 64 + (i % 2) * 670, y = 251 + (i / 2) * 225;
            r.glassPanel(x, y, 647, 207, 12, .9f);
            r.text(lessons[i].n, x + 24, y + 22, 29, colors::teal, true);
            r.text(lessons[i].title, x + 86, y + 27, 23, colors::white, true);
            r.text(lessons[i].one, x + 26, y + 88, 15, colors::muted);
            r.text(lessons[i].two, x + 26, y + 116, 15, colors::muted);
            r.text(lessons[i].key, x + 26, y + 164, 13, colors::white, true);
        }
        r.text("THE SCORE", 66, 743, 11, colors::teal, true);
        r.text("Distance points + three of five style marks. The highest and lowest marks are dropped.", 66,
               769, 16, colors::white);
        r.text("C changes camera  /  G shows your last jump as a ghost  /  ESC pauses  /  R replays a "
               "finished jump",
               66, 803, 13, colors::muted);
        r.text("New to jumping? Try England K50 or Austria K70 in practice.", 66, 848, 13, colors::muted);
        button("Try it on the hill  >", 1128, 829, 246, Action::Practice, 0, true);
    }
    void settings() {
        top("Make it feel like you.", "Settings are saved automatically when you close the game.");
        auto &r = *renderer;
        const char *cams[] = {"Side follow", "Chase", "Wide"};
        const char *winds[] = {"Original variable wind", "Calm", "Steady headwind", "Steady tailwind"};
        struct Setting {
            std::string title, detail, value;
            Action action;
        };
        const Setting rows[] = {
            {"Camera", "Press C during a jump.", cams[camera], Action::Camera},
            {"Sound", "Press M to toggle sound.", records.sound ? "On" : "Off", Action::Sound},
            {"Wind conditions", "Practice only; cup wind stays original.", winds[windMode], Action::Wind},
            {"Last jump ghost", "Show the last jump on this hill.", ghost ? "On" : "Off", Action::Ghost}};
        for (int i = 0; i < 4; ++i) {
            const float y = 253 + i * 87;
            r.glassPanel(64, y, 644, 74, 10, .9f);
            r.text(rows[i].title, 86, y + 12, 18, colors::white, true);
            r.text(rows[i].detail, 86, y + 46, 12, colors::muted);
            button(rows[i].value + "  >", 456, y + 13, 230, rows[i].action);
        }
        r.glassPanel(738, 253, 636, 185, 10, .9f);
        r.text("Time of day", 758, 271, 19, colors::white, true);
        for (int i = 0; i <= 4; ++i)
            button(environment::name(environment::TimeOfDay(i)), 758 + (i % 3) * 205,
                   307 + (i / 3) * 59, 186, Action::TimeOfDay, i, int(records.environment.time) == i);
        r.glassPanel(738, 456, 636, 214, 10, .9f);
        r.text("Weather", 758, 474, 19, colors::white, true);
        for (int i = 0; i <= 4; ++i)
            button(environment::name(environment::Weather(i)), 758 + (i % 3) * 205,
                   510 + (i / 3) * 59, 186, Action::Weather, i, int(records.environment.weather) == i);
        r.text("Only wind affects the jump.", 758, 641, 13, colors::muted);
        r.glassPanel(64, 619, 644, 152, 10, .9f);
        r.text("Mouse sensitivity", 86, 636, 19, colors::white, true);
        r.text("1.0x matches original mouse counts.", 86, 671, 13, colors::muted);
        button("Original 1.0x", 86, 710, 169, Action::SensitivityReset);
        button("-", 326, 710, 56, Action::SensitivityDown);
        r.text(number(records.sensitivity) + "x", 430, 722, 18, colors::teal, true);
        button("+", 630, 710, 56, Action::SensitivityUp);
        r.text("Practice: fixed for each jump.", 758, 692, 14, colors::white);
        r.text("Cups: fixed for all jumpers and both rounds of each event.", 758, 720, 13, colors::muted);
        smallButton("Day & sun", 1202, 751, 151, Action::EnvironmentReset);
        r.text("Choose conditions before starting practice or a new cup.", 758, 764, 12, colors::muted);
        r.text("F11 switches between windowed and fullscreen. Your progress is stored locally.", 66, 809, 14,
               colors::muted);
    }
    void recordScreen() {
        top("Your longest flights.",
            "Official standing jumps by human cup participants qualify for personal bests.");
        auto &r = *renderer;
        r.text("HILL", 89, 242, 11, colors::muted, true);
        r.text("COUNTRY", 570, 242, 11, colors::muted, true);
        r.text("K POINT", 870, 242, 11, colors::muted, true);
        r.textRight("PERSONAL BEST", 1334, 242, 11, colors::muted, true);
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
            std::string ordinal = (i < 9 ? "0" : "") + std::to_string(i + 1);
            r.text(ordinal, 89, y + 13, 17, colors::teal, true);
            r.text(hills()[i].name, 153, y + 13, 17, colors::white, true);
            r.text(hills()[i].country, 570, y + 16, 12, colors::muted);
            r.text("K" + std::to_string(hills()[i].k), 870, y + 16, 13, colors::muted);
            r.textRight(records.best[i] ? number(records.best[i]) + " m" : "--", 1334, y + 10, 21,
                        records.best[i] ? colors::teal : colors::muted, true);
            buttons.push_back({64, y, 1310, 47, Action::SelectHill, i});
        }
        hillPagination(722);
        r.text("SELECTED: " + hills()[selectedHill].name, 66, 778, 14, colors::muted);
        button("Practice selected hill  >", 1077, 808, 296, Action::Practice, 0, true);
        if (hasReplay)
            button("Watch last replay", 64, 808, 238, Action::Replay);
    }
    void hud() {
        auto &r = *renderer;
        const auto &h = sim.hill();
        // Keep venue and weather at the edges; the jumper has the center.
        r.glassPanel(32, 28, 360, 74, 10, .76f);
        r.text(h.name, 50, 40, 21, colors::white, true);
        r.text("K" + std::to_string(h.k) + (competition ? "" : " / Practice") +
                   " / " + environment::description(activeEnvironment), 50, 76, 11,
               colors::muted);
        if (competition) {
            r.glassPanel(470, 28, 490, 74, 10, .76f);
            const std::string name = jumperName();
            const float size = std::min(20.f, 454.f / std::max(1.f, r.textWidth(name, 1, true)));
            r.text(name, 488, 40, size, colors::white, true);
            r.text((cup.setup().mode == CupMode::Team ? "Team cup  " : "World cup  ") +
                       std::to_string(cupEvent + 1) + " / " + std::to_string(cup.eventCount()) +
                       "  /  Round " + std::to_string(round + 1),
                   488, 76, 11, colors::muted);
        }
        r.glassPanel(1218, 28, 190, 74, 10, .76f);
        r.windArrow(1242, 61, sim.windSpeed, sim.displayWindAngle());
        r.textRight(number(sim.windSpeed) + " m/s", 1390, 39, 24, colors::white, true);
        r.textRight(sim.wind >= 0 ? "Headwind" : "Tailwind", 1390, 76, 11, colors::muted);

        if (sim.phase == Phase::Ready) {
            r.glassPanel(32, 116, 105, 34, 8, .76f);
            r.text("START", 45, 126, 10, colors::muted);
            r.textRight(number(sim.countdown, 0) + " s", 125, 124, 13,
                        sim.countdown < 5 ? colors::orange : colors::teal, true);
            if (!competition || !activeComputer()) {
                r.glassPanel(590, 798, 260, 42, 10, .72f);
                const std::string hint = "SPACE / LEFT CLICK";
                r.text(hint, 720 - r.textWidth(hint, 12, true) / 2, 812, 12, colors::white, true);
            }
            return;
        }

        r.glassPanel(32, 754, 174, 104, 10, .76f);
        r.text("SPEED", 50, 766, 10, colors::muted, true);
        r.text(number(std::sqrt(sim.vx * sim.vx + sim.vy * sim.vy) * 3.6, 0), 48, 790, 40,
               colors::white, true);
        r.textRight("km/h", 188, 829, 11, colors::muted);
        if (sim.phase == Phase::Flight || sim.phase == Phase::Outrun) {
            r.glassPanel(220, 754, 184, 104, 10, .76f);
            r.text("DISTANCE", 238, 766, 10, colors::muted, true);
            const double distance = sim.phase == Phase::Flight ? h.distance(sim.x) : sim.result.distance;
            r.text(number(distance), 236, 795, 32, colors::white, true);
            r.textRight("m", 386, 829, 11, colors::muted);
        }
    }
    void paused() {
        auto &r = *renderer;
        r.rect(0, 0, 1440, 900, {.03f, .08f, .12f, .78f});
        r.glassPanel(472, 285, 496, 320, 16, .94f);
        r.text("Paused", 508, 317, 38, colors::white, true);
        button("Resume", 510, 398, 420, Action::Resume, 0, true);
        button("Leave jump", 510, 465, 420, Action::Back);
        r.text("ESC / ENTER", 511, 560, 12, colors::muted);
    }
    void result() {
        auto &r = *renderer;
        const auto &s = sim.result;
        r.rect(0, 0, 1440, 900, {.025f, .07f, .1f, .7f});
        r.glassPanel(287, 121, 866, 662, 16, .95f);
        r.rect(326, 121, 78, 3, colors::teal, 1);
        r.pill(s.disqualified ? "DISQUALIFIED"
               : s.crashed    ? "FALL"
               : newRecord    ? "NEW PERSONAL BEST"
                              : "JUMP COMPLETE",
               326, 154, s.crashed || s.disqualified ? colors::orange : colors::teal);
        r.textRight(sim.hill().name + " / K" + std::to_string(sim.hill().k), 1114, 161, 13, colors::muted);
        r.text("Jump result", 324, 211, 32, colors::white, true);
        r.text(number(s.distance), 321, 282, 82, s.crashed ? colors::white : colors::teal, true);
        r.text("metres", 611, 344, 18, colors::muted);
        r.textRight(number(s.total), 1114, 301, 56, colors::white, true);
        r.textRight("TOTAL POINTS", 1112, 369, 11, colors::muted, true);
        r.line(326, 410, 1114, 410, {.3f, .42f, .48f, .55f});
        r.text("DISTANCE", 326, 434, 11, colors::muted, true);
        r.text(number(s.distancePoints) + " pts", 326, 459, 23, colors::white, true);
        r.text("STYLE", 603, 434, 11, colors::muted, true);
        r.text(number(s.stylePoints) + " pts", 603, 459, 23, colors::white, true);
        r.text("LANDING", 893, 434, 11, colors::muted, true);
        r.text(s.disqualified                   ? "--"
               : s.crashed                      ? "Fall"
               : s.landing == Landing::Telemark ? "Telemark"
                                                : "Two-foot",
               893, 459, 23, s.crashed ? colors::orange : colors::teal, true);
        auto min = std::min_element(s.judges.begin(), s.judges.end());
        auto max = std::max_element(s.judges.begin(), s.judges.end());
        if (min == max && s.judges.size() > 1)
            max = s.judges.end() - 1;
        r.text("JUDGES", 326, 518, 11, colors::muted, true);
        for (int i = 0; i < 5; i++) {
            float x = 424 + i * 106;
            bool noMarks = s.beforeTakeoff || s.disqualified;
            bool drop = !noMarks && (&s.judges[i] == &*min || &s.judges[i] == &*max);
            r.rect(x, 509, 88, 47, drop ? Color{.25f, .36f, .41f, .23f}
                                     : Color{.27f, .88f, .78f, .2f}, 7);
            r.rect(x + 1, 510, 86, 45, {.07f, .14f, .18f, .92f}, 6);
            std::string v = noMarks ? "--" : number(s.judges[i]);
            r.text(v, x + 15, 521, 18, drop ? colors::muted : colors::white, true);
            if (drop)
                r.line(x + 11, 535, x + 73, 535, colors::muted, 1);
        }
        if (s.beforeTakeoff || s.disqualified)
            r.text(s.feedback, 326, 595, 14, colors::muted);
        if (competition)
            r.text(completedName + "  /  EVENT " + std::to_string(completedEvent + 1) + " OF " +
                       std::to_string(cup.eventCount()) + "  /  ROUND " + std::to_string(completedRound + 1) +
                       " OF 2",
                   326, 633, 12, colors::teal, true);
        button(competition ? "Continue cup  >" : "Jump again  >", 326, 691, 265,
               competition ? Action::Next : Action::Again, 0, true);
        if (hasReplay && !s.disqualified)
            button("Watch replay", 608, 691, 217, Action::Replay);
        button("Main menu", 842, 691, 271, Action::Back);
        keyHint("ENTER", competition ? "Continue" : "Jump again", 326, 811);
        if (hasReplay)
            keyHint("R", "Replay", 581, 811);
    }
    void cupSetupUI() {
        auto &r = *renderer;
        auto &setup = editableSetup();
        top("Build your cup.", "Shared controls / Up to 16 jumpers / Original event and start-order rules");
        smallButton(setupTeam ? "World cup" : "World cup *", 65, 209, 150, Action::CupSetupMode, 0);
        smallButton(setupTeam ? "Team cup *" : "Team cup", 230, 209, 150, Action::CupSetupMode, 1);
        smallButton(setupTab == 0 ? "Participants *" : "Participants", 398, 209, 176, Action::CupSetupTab, 0);
        smallButton(setupTab == 1 ? "Hills *" : "Hills", 590, 209, 110, Action::CupSetupTab, 1);
        smallButton(setup.doubleSeason ? "Two seasons" : "One season", 726, 209, 170, Action::CupSeasons);
        smallButton(setup.skipCpu ? "CPU: skip" : "CPU: watch", 912, 209, 156, Action::CupCpuView);
        smallButton("< Menu", 1235, 209, 140, Action::Back);
        int pages = 1;
        if (setupTab == 0) {
            int count = int(setup.participants.size());
            pages = (count + 7) / 8;
            setupPage = clamp(setupPage, 0, pages - 1);
            r.text("Click a name or country to edit. Each human takes a turn on this machine.", 65, 265, 14,
                   colors::muted);
            if (setupTeam) {
                for (int t = 0; t < int(setup.teams.size()); ++t) {
                    smallButton(setup.teams[size_t(t)].name, 65 + t * 266, 299, 177, Action::CupTeamName, t);
                    smallButton(setup.teams[size_t(t)].country, 250 + t * 266, 299, 63,
                                Action::CupTeamCountry, t);
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
                smallButton(p.country, 479, y + 3, 82, Action::CupPlayerCountry, i);
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
            r.text("Hills run in their original order. Select at least one.", 65, 265, 14, colors::muted);
            smallButton("All 32", 1128, 265, 110, Action::CupHillsAll, 1);
            smallButton("Clear", 1255, 265, 113, Action::CupHillsAll, 0);
            for (int row = 0, h = setupPage * 8; h < std::min(hillCount(), (setupPage + 1) * 8); ++row, ++h) {
                bool chosen = std::find(setup.hills.begin(), setup.hills.end(), h) != setup.hills.end();
                button(std::string(chosen ? "[x] " : "[ ] ") + hills()[h].name + " / K" +
                           std::to_string(hills()[h].k),
                       65, 321 + row * 53, 1303, Action::CupHillToggle, h, chosen);
            }
        }
        smallButton("< Prev", 65, 769, 110, Action::CupSetupPage, -1);
        r.text("Page " + std::to_string(setupPage + 1) + " / " + std::to_string(pages), 194, 781, 13,
               colors::muted);
        smallButton("Next >", 330, 769, 110, Action::CupSetupPage, 1);
        r.text(std::to_string(setup.participants.size()) + " jumpers / " +
                   std::to_string(setup.hills.size()) + " hills / " +
                   std::to_string(setup.hills.size() * (setup.doubleSeason ? 2 : 1)) + " events",
               65, 828, 15, colors::teal, true);
        button("Start cup  >", 1128, 802, 240, Action::CupStart, 0, true);
    }
    void jumpReadyUI() {
        auto &r = *renderer;
        r.glassPanel(300, 211, 840, 470, 16, .94f);
        r.pill(cup.setup().mode == CupMode::Team ? "TEAM CUP" : "WORLD CUP", 343, 246);
        r.text("Next jumper", 343, 304, 24, colors::muted);
        r.text(jumperName(), 343, 354, 43, colors::white, true);
        r.text(sim.hill().name + " / EVENT " + std::to_string(cup.event() + 1) + " OF " +
                   std::to_string(cup.eventCount()) + " / ROUND " + std::to_string(cup.round() + 1),
               343, 423, 16, colors::teal);
        r.text(environment::description(activeEnvironment), 813, 266, 13, colors::muted);
        if (cup.setup().mode == CupMode::Team) {
            const auto &p = cup.entries()[size_t(activeJumper())].participant;
            r.text(cup.setup().teams[size_t(p.team)].name + " / MEMBER " + std::to_string(p.member + 1), 343,
                   457, 16, colors::muted);
        }
        r.text(activeComputer() ? "Watch the original CPU technique."
                                : "Pass the controls to this player. Enter when ready.",
               343, 505, 16, colors::muted);
        button(activeComputer() ? "Watch jump  >" : "Ready  >", 343, 576, 310, Action::StartTurn, 0, true);
        button("Save & menu", 690, 576, 360, Action::Back);
    }
    void standings() {
        bool final = cup.finalEvent() && cup.eventComplete();
        top(final ? "The cup is decided." : "Every jump counts.",
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
            r.text(name, 174, y + 13, 18, colors::white, true);
            r.text(country, 493, y + 16, 12, colors::muted);
            r.text(number(total), 1034, y + 13, 18, colors::white, true);
            r.textRight(std::to_string(points), 1334, y + 13, 19, colors::white, true);
        }
        r.text("Tied scores share a place and cup points.", 66, 780, 14, colors::muted);
        if (pages > 1) {
            smallButton("< Page", 66, 817, 119, Action::CupSetupPage, -1);
            smallButton("Page >", 202, 817, 119, Action::CupSetupPage, 1);
        }
        button(final ? "Finish cup" : "Next hill  >", 1128, 808, 246, Action::Next, 0, true);
        smallButton("Save & menu", 922, 816, 184, Action::Back);
    }
    void replayFilesUI() {
        auto &r = *renderer;
        top("Your replay library.", "Open original DSJ .RPL files or The Perfect V .SJR files. You can also "
                                    "drop a file onto the game.");
        r.text(replayDirectory.string().substr(0, 135), 66, 245, 14, colors::teal);
        smallButton("< Parent folder", 65, 282, 220, Action::ReplayParent);
        smallButton("< Menu", 1235, 282, 140, Action::Back);
        for (int row = 0, i = replayPage * 8; i < std::min(int(replayFiles.size()), (replayPage + 1) * 8);
             ++row, ++i) {
            const auto &path = replayFiles[size_t(i)];
            button((std::filesystem::is_directory(path) ? "Folder: " : "") + path.filename().string(), 65,
                   345 + row * 53, 1303, Action::ReplayFile, i);
        }
        if (replayFiles.empty())
            r.text("No replay files in this folder.", 66, 363, 18, colors::muted);
        smallButton("< Prev", 65, 808, 110, Action::ReplayBrowsePage, -1);
        r.text("Page " + std::to_string(replayPage + 1) + " / " +
                   std::to_string(std::max(1, (int(replayFiles.size()) + 7) / 8)),
               194, 820, 13, colors::muted);
        smallButton("Next >", 330, 808, 110, Action::ReplayBrowsePage, 1);
    }
    void replayUI() {
        auto &r = *renderer;
        r.glassPanel(38, 35, 394, canExportOriginal ? 142 : 114, 12, .86f);
        r.pill("REPLAY", 57, 49);
        r.text(hills()[lastReplay.hill].name + " / " + number(lastReplay.result.distance) + " m", 58, 87, 15,
               colors::white, true);
        if (canExportOriginal)
            r.text(originalReplayData.athleteName + " / " + originalReplayData.date + " / " +
                       originalReplayData.author,
                   58, 119, 10, colors::muted);
        r.text(environment::description(replayEnvironment), 58, canExportOriginal ? 148 : 119, 11,
               colors::muted);
        smallButton("<  Back", 1268, 39, 126, Action::Back);
        smallButton("Export .SJR", 952, 39, 142, Action::ReplayExportNative);
        smallButton("Export .RPL", 1110, 39, 142, Action::ReplayExport);
        r.glassPanel(180, 752, 1080, 102, 14, .9f);
        smallButton(replayPlaying && replayTime < replayDuration() ? "Pause" : "Play", 202, 775, 74,
                    Action::ReplayPlay);
        r.rect(290, 792, 860, 7, {.02f, .06f, .09f, 1}, 4);
        r.rect(290, 793, float(860 * replayTime / std::max(.01, replayDuration())), 5, colors::teal, 3);
        r.rect(float(290 + 860 * replayTime / std::max(.01, replayDuration())) - 4, 788, 8, 15, colors::white,
               4);
        buttons.push_back({290, 778, 860, 31, Action::ReplaySeek});
        r.text(number(replayTime) + " / " + number(replayDuration()) + " s", 290, 817, 11, colors::muted);
        r.textRight(number(replaySpeed, 2) + "x", 1241, 811, 13, colors::teal, true);
        smallButton("-", 1170, 768, 33, Action::ReplaySlow);
        smallButton("+", 1212, 768, 33, Action::ReplayFast);
        r.glassPanel(24, 860, 971, 35, 8, .74f);
        keyHint("SPACE", "Play / pause", 38, 868);
        keyHint("1 - 4", "Speed", 292, 868);
        keyHint("LEFT / RIGHT", "Seek", 509, 868);
        keyHint("C", "Camera", 827, 868);
    }
    void render() {
        buttons.clear();
        bool panorama = screen == Screen::Menu || screen == Screen::Hills || screen == Screen::Help ||
                        screen == Screen::Settings || screen == Screen::Records ||
                        screen == Screen::Standings || screen == Screen::CupSetup ||
                        screen == Screen::ReplayFiles;
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
        renderer->begin(hills()[hi], f, time, camera, panorama, showGhost, showGhost ? &g : nullptr);
        renderer->overlay();
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
            renderer->rect(38, 852, 1360, 40, colors::panel, 5);
            renderer->text(message.substr(0, 160), 50, 864, 12, colors::white);
        }
        if (editingName != -1) {
            renderer->rect(330, 290, 780, 215, colors::panel, 12);
            renderer->text(editingCountry ? "Country code" : "Enter a name", 363, 321, 22, colors::white,
                           true);
            renderer->text(editText + "_", 363, 379, 27, colors::teal, true);
            renderer->text("Backspace to edit / Enter to save / Esc to cancel", 363, 456, 14, colors::muted);
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
        // Exercise the actual menu hit testing, keyboard start, pause and mouse takeoff.
        if (smokeMouseCapture)
            clickButton(Action::Practice);
        else {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.x = 200;
            event.motion.y = 512;
            SDL_PushEvent(&event);
            event = {};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_LEFT;
            SDL_PushEvent(&event);
            events();
        }
        if (screen != Screen::Playing)
            throw std::runtime_error("Practice button input failed");
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
        beginReplay();
        replayTime = replayDuration() * .6;
        std::string previousExport;
        for (int copy = 0; copy < 2; ++copy) {
            clickButton(Action::ReplayExportNative);
            const std::string prefix = "Saved replay: ";
            if (message.compare(0, prefix.size(), prefix) != 0)
                throw std::runtime_error("Native replay export failed: " + message);
            const auto exportedPath = message.substr(prefix.size());
            Replay exported;
            if (exportedPath == previousExport || !loadReplay(exportedPath, exported) ||
                !exported.hasEnvironment || !(exported.environment == replayEnvironment) ||
                exported.hill != lastReplay.hill || exported.frames.size() != lastReplay.frames.size() ||
                exported.originalRpl != lastReplay.originalRpl)
                throw std::runtime_error("Native replay export lost atmosphere, input recording or uniqueness");
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
                if (storageError || !loadReplay(dataDir + "/latest-dsj2.sjr", restored) ||
                    restored.hill != selectedHill || restored.frames.size() != sim.frames.size() ||
                    restored.result.crashed != sim.result.crashed) {
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
                    action(Action::CupResume);
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
                startTurn();
                for (int n = 0; n < 20000 && screen == Screen::Playing; ++n) {
                    sim.step(ORIGINAL_TICK, autopilot(sim));
                    if (sim.phase == Phase::Finished || sim.phase == Phase::Disqualified)
                        completeJump();
                }
                if (screen != Screen::Result)
                    throw std::runtime_error("Visible CPU/human cup jump failed");
                ++visibleJumps;
                Competition restored;
                if (!loadCompetition(dataDir + "/cup.sjc", restored) ||
                    restored.currentJumper() != cup.currentJumper() ||
                    restored.entries()[0].completed != cup.entries()[0].completed)
                    throw std::runtime_error("Completed result checkpoint failed");
                if (visibleJumps == 1) {
                    action(Action::Back);
                    action(Action::CupResume);
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
            if (!saveReplay(dataDir + "/imported.sjr", lastReplay) ||
                !openReplayFile(dataDir + "/imported.sjr") || !canExportOriginal)
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
        App app(argc, argv);
        return app.run();
    } catch (const std::exception &e) {
        std::cerr << "The Perfect V: " << e.what() << '\n';
        return 1;
    }
}
