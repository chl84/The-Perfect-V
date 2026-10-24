#include "original_replay.h"
#include "storage.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
// Physical mouse inputs can be represented exactly by the original format.
Input mouseControls(const Simulation &sim) {
    Input input;
    input.buttonMask = 0;
    if (sim.phase == Phase::Ready)
        input.buttonMask = 1;
    if ((sim.originalStage == 3 && sim.x > -std::max(1.0, sim.vx) * .21) ||
        sim.originalStage == 4 || sim.originalStage == 5)
        input.buttonMask = 3;
    if (sim.originalStage == 7) {
        input.mouseDy = int(std::clamp(std::round((sim.pitch + .35) / (ORIGINAL_TICK * 1.5)), -3.0, 3.0));
        const double approach = -(sim.vy - sim.hill().slope(sim.x) * sim.vx);
        if (approach > 0 && sim.clearance() < approach * .65 && sim.phaseTime > .4)
            input.buttonMask = 3;
    }
    return input;
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("tpv-replay-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(directory);
        // These seeds include fixed-wind jumps whose rounded distance used to
        // match a reconstruction with different weather and a different path.
        for (uint32_t seed : {7u, 11u, 14u, 17u, 28u, 30u}) {
            OriginalRandom random(seed * 2654435761u);
            OriginalWind weather;
            weather.initialize(random);
            for (int mode = 0; mode < 4; ++mode) {
                Simulation sim;
                sim.resetWithWeather(int(seed % HILL_COUNT), weather, seed * 977u);
                require(!sim.usesFixedWind(), "Reset retained a fixed wind override");
                if (mode != 0) {
                    const double wind = mode == 1 ? 0 : mode == 2 ? 4.5 : -2.5;
                    sim.setWind(std::abs(wind), wind >= 0 ? PI : 0);
                }
                for (int tick = 0; tick < 20000 && sim.phase != Phase::Finished &&
                     sim.phase != Phase::Disqualified; ++tick)
                    sim.step(ORIGINAL_TICK, mouseControls(sim));
                require(sim.phase == Phase::Finished, "Replay export fixture did not finish");
                OriginalReplayData original;
                original.athleteName = "UNCHANGED";
                std::string error;
                if (mode == 0) {
                    require(buildOriginalReplay(sim, original, &error),
                            "Normal variable-wind jump lost original export support");
                    Replay restored;
                    require(reconstructOriginalReplay(original, restored, nullptr, &error) &&
                                restored.result.distance == sim.result.distance &&
                                restored.result.crashed == sim.result.crashed,
                            "Normal original export changed the jump");
                } else {
                    require(!buildOriginalReplay(sim, original, &error) &&
                                error.find("fixed wind") != std::string::npos &&
                                original.athleteName == "UNCHANGED",
                            "Fixed wind must reject original export without changing its output");
                    const auto native = sim.replay();
                    Replay restored;
                    const auto path = (directory / "fixed-wind.pvr").string();
                    require(saveReplay(path, native) && loadReplay(path, restored) &&
                                restored.originalRpl.empty() && restored.frames.size() == native.frames.size(),
                            "Fixed-wind jumps must retain native replay support");
                    for (size_t i = 0; i < native.frames.size(); ++i)
                        require(restored.frames[i].y == native.frames[i].y &&
                                    restored.frames[i].windSpeed == native.frames[i].windSpeed &&
                                    restored.frames[i].windAngle == native.frames[i].windAngle,
                                "Native replay changed the fixed-wind trajectory or weather");
                }
                sim.resetWithWeather(0, weather, seed);
                require(!sim.usesFixedWind(), "Next jump retained the previous fixed wind setting");
            }
        }
        std::filesystem::remove_all(directory);
        std::cout << "Variable-wind RPL export, all three fixed-wind presets and native replay preservation passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
