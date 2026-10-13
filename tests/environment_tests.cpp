#include "competition.h"
#include "storage.h"
#include "visual_environment.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string contents(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}
} // namespace
int main() {
    using namespace sjp;
    using namespace sjp::environment;
    const auto directory = std::filesystem::temp_directory_path() / "perfect-v-environment-tests";
    std::filesystem::create_directories(directory);
    try {
        const auto defaults = resolve({}, 98765);
        require(defaults.time == TimeOfDay::Day && defaults.weather == Weather::Sunny,
                "Default conditions must be day and sun");
        std::array<size_t, 4> times{}, weather{};
        for (uint32_t seed = 0; seed < 100000; ++seed) {
            const auto random = resolve({TimeOfDay::Random, Weather::Random}, seed);
            require(random == resolve({TimeOfDay::Random, Weather::Random}, seed),
                    "Fixed jump seed produced changing conditions");
            ++times[size_t(random.time)];
            ++weather[size_t(random.weather)];
            require(resolve({TimeOfDay::Night, Weather::Random}, seed).time == TimeOfDay::Night &&
                        resolve({TimeOfDay::Random, Weather::Fog}, seed).weather == Weather::Fog,
                    "Randomizing one choice changes the other manual choice");
        }
        for (size_t count : times)
            require(count > 24000 && count < 26000, "Random time does not include all four periods");
        require(weather[0] > 43500 && weather[0] < 46500 && weather[1] > 28500 && weather[1] < 31500 &&
                    weather[2] > 14000 && weather[2] < 16000 && weather[3] > 9000 && weather[3] < 11000,
                "Random weather distribution is incorrect");
        for (int time = 0; time < 4; ++time)
            for (int weather = 0; weather < 4; ++weather) {
                const Conditions conditions{TimeOfDay(time), Weather(weather), 7654321};
                const auto light = visual::environmentLight(conditions);
                require(std::abs(light.direction.length() - 1) < 1e-12 && light.direction.y > .1 &&
                            light.fogStart > 0 && light.fogEnd > light.fogStart &&
                            light.shadowStrength >= 0 && light.shadowStrength <= .4,
                        "Invalid environmental light or fog");
                for (Vec3 color :
                     {light.skyTop, light.skyHorizon, light.fog, light.ambient, light.direct, light.fill})
                    for (double channel : {color.x, color.y, color.z})
                        require(std::isfinite(channel) && channel >= 0 && channel <= 1,
                                "Environmental color is invalid");
                require(light.moon == (time == int(TimeOfDay::Night)), "Night does not use moonlight");
                const auto clear = visual::environmentLight({TimeOfDay(time), Weather::Sunny, 0});
                if (weather != int(Weather::Sunny))
                    require(light.shadowStrength < clear.shadowStrength && light.celestialVisibility == 0,
                            "Cloudy conditions keep sharp sunlight and shadows");
                if (weather == int(Weather::Fog))
                    require(light.fogEnd < clear.fogEnd / 2, "Fog does not shorten visibility");
            }
        Records records;
        records.best[2] = 147.5;
        records.environment = {TimeOfDay::Random, Weather::Snowfall};
        const auto recordPath = directory / "records.dat";
        require(saveRecords(recordPath.string(), records), "Cannot save environment preferences");
        auto loadedRecords = loadRecords(recordPath.string());
        require(loadedRecords.best == records.best && loadedRecords.environment.time == TimeOfDay::Random &&
                    loadedRecords.environment.weather == Weather::Snowfall,
                "Preferences or records changed after restoring environment choices");
        auto legacyRecords = contents(recordPath);
        legacyRecords.erase(legacyRecords.find("ENV "));
        write(recordPath, legacyRecords);
        loadedRecords = loadRecords(recordPath.string());
        require(loadedRecords.best == records.best && loadedRecords.environment.time == TimeOfDay::Day &&
                    loadedRecords.environment.weather == Weather::Sunny,
                "Older preferences do not default safely to day and sun");
        write(recordPath, legacyRecords + "ENV 1 99 99\n");
        require(loadRecords(recordPath.string()).best == records.best,
                "Malformed optional environment preferences discarded personal records");
        Simulation sim(2, 0);
        sim.resetOriginal(2, 12345, 98765);
        for (int tick = 0; tick < 20000 && sim.phase != Phase::Finished; ++tick)
            sim.step(ORIGINAL_TICK, autopilot(sim));
        require(sim.phase == Phase::Finished, "Replay fixture jump did not finish");
        auto replay = sim.replay();
        replay.hasEnvironment = true;
        replay.environment = {TimeOfDay::Evening, Weather::Snowfall, 0xffffffffU};
        const auto replayPath = directory / "replay.sjr";
        Replay loadedReplay;
        require(saveReplay(replayPath.string(), replay) && loadReplay(replayPath.string(), loadedReplay) &&
                    loadedReplay.hasEnvironment && loadedReplay.environment == replay.environment &&
                    loadedReplay.frames.size() == replay.frames.size(),
                "Replay does not retain the jump's actual resolved conditions");
        auto legacyReplay = contents(replayPath);
        legacyReplay.erase(legacyReplay.find("ENV "));
        write(replayPath, legacyReplay);
        require(loadReplay(replayPath.string(), loadedReplay) && !loadedReplay.hasEnvironment,
                "Existing native replays are no longer readable");
        write(replayPath, legacyReplay + "ENV 1 4 0 5\n");
        require(!loadReplay(replayPath.string(), loadedReplay),
                "Replay accepts unresolved random conditions");
        for (CupMode mode : {CupMode::World, CupMode::Team}) {
            auto setup = mode == CupMode::World ? defaultCupSetup() : defaultTeamCupSetup();
            setup.hills = {0, 1};
            setup.doubleSeason = true;
            Competition cup, reference;
            require(cup.start(setup, 12345, 67890, 98765, nullptr, {TimeOfDay::Random, Weather::Random}) &&
                        reference.start(setup, 12345, 67890, 98765),
                    "Cannot start test cups");
            require(cup.weather().x == reference.weather().x && cup.weather().y == reference.weather().y,
                    "Cosmetic environment changes original wind initialization");
            const auto legacyCupPath = directory / "legacy-cup.sjc";
            require(saveCompetition(legacyCupPath.string(), cup), "Cannot create legacy cup fixture");
            const auto currentCup = contents(legacyCupPath);
            const size_t header = currentCup.find('\n', currentCup.find('\n') + 1);
            auto payload = currentCup.substr(header + 1);
            payload.erase(payload.rfind('\n', payload.size() - 2) + 1);
            uint64_t checksum = 14695981039346656037ull;
            for (unsigned char byte : payload) {
                checksum ^= byte;
                checksum *= 1099511628211ull;
            }
            write(legacyCupPath, "SJP_COMPETITION_2\n" + std::to_string(checksum) + "\n" + payload);
            Competition legacyCup;
            require(loadCompetition(legacyCupPath.string(), legacyCup) &&
                        legacyCup.visualEnvironment().time == TimeOfDay::Day &&
                        legacyCup.visualEnvironment().weather == Weather::Sunny &&
                        legacyCup.weather().x == cup.weather().x && legacyCup.weather().y == cup.weather().y,
                    "Legacy cup lost its wind or did not default to day and sun");
            for (size_t i = 0; i < cup.entries().size(); ++i)
                require(cup.entries()[i].skills.values == reference.entries()[i].skills.values,
                        "Environment changes original AI random state");
            uint32_t previousSeed = cup.visualEnvironment().seed;
            for (int event = 0; event < 4; ++event) {
                const auto fixed = cup.visualEnvironment();
                while (!cup.eventComplete()) {
                    cup.prepareJump(123456 + uint32_t(cup.position()));
                    require(cup.visualEnvironment() == fixed,
                            "Preparing another jumper changed event weather");
                    const auto cupPath = directory / "cup.sjc";
                    Competition restored;
                    require(saveCompetition(cupPath.string(), cup) &&
                                loadCompetition(cupPath.string(), restored) &&
                                restored.visualEnvironment() == fixed,
                            "Cup resume changes its fixed time or weather");
                    cup = std::move(restored);
                    Result result;
                    result.distance = 100;
                    result.landing = Landing::Telemark;
                    result.judges.fill(18);
                    require(cup.submitResult(result), "Cannot submit cup test result");
                    require(cup.visualEnvironment() == fixed,
                            "Changing jumper or round changed event weather");
                }
                if (event < 3) {
                    require(cup.advanceEvent(67890, 98765), "Cannot advance test cup");
                    require(cup.visualEnvironment().seed != previousSeed,
                            "Next event reused the previous random draw");
                    previousSeed = cup.visualEnvironment().seed;
                }
            }
            require(cup.advanceEvent(67890, 98765) && cup.complete(), "Test cup did not finish");
        }
        std::filesystem::remove_all(directory);
        std::cout << "Manual/random conditions, lighting, save compatibility and fixed World/Team Cup events "
                     "passed.\n";
    } catch (const std::exception &error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
