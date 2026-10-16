#include "competition.h"
#include "storage.h"
#include "visual_environment.h"
#include "visual_snow_glitter.h"
#include "visual_sky.h"
#include "visual_mountains.h"
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
    // A rotating camera must cross the photographic wrap continuously; height
    // below the ring clamps to the opaque lower edge, and the zenith to sky.
    const auto seamLeft = visual::mountainU({-1, 0, -.00001});
    const auto seamRight = visual::mountainU({-1, 0, .00001});
    require(std::abs((seamRight - seamLeft) - 1) < .00001,
            "Mountain panorama wrap is not continuous");
    require(visual::mountainV({0,1,0}) == 0 && visual::mountainV({0,-1,0}) == 1,
            "Mountain panorama exposes its edges above or below the horizon");
    require(visual::mountainHazeDistance({1,-.4,0}) > visual::mountainHazeDistance({1,.3,0}),
            "Mountain foothills lack atmospheric depth");
    for (const auto &hill : hills()) {
        const double core = std::max(50 * hill.scale(), 24.0);
        for (double x : {hill.inrunStart(), hill.start(), 0.0, hill.outrunBottom(),
                         hill.end(), std::max(420 * hill.scale(), hill.end() + 80 * hill.scale())}) {
            for (double z : {0.0, -core, core})
                require(x > std::max(hill.outrunBottom() + 47, 190 * hill.scale()) ||
                            visual::backdropGround(hill, x, z) == visual::arenaGround(hill, x, z),
                        "Background correction moves the protected ski/arena surface");
            if (x <= hill.outrunBottom())
                require(visual::backdropGround(hill, x, -200 * hill.scale()) ==
                            visual::arenaGround(hill, x, -200 * hill.scale()),
                        "Background correction changes terrain before the outrun bottom");
        }
        const double distant = hill.end() + 160 * hill.scale();
        require(visual::backdropGround(hill, distant, 0) < visual::arenaGround(hill, distant, 0),
                "Background beyond the arena still extrapolates the uphill quadratic");
        const double tailStart = std::max(hill.outrunBottom() + 47, 190 * hill.scale());
        require(visual::backdropGround(hill, tailStart, 0) == visual::arenaGround(hill, tailStart, 0) &&
                    visual::backdropGround(hill, tailStart + 80 * hill.scale(), 0) <
                        visual::backdropGround(hill, tailStart + 40 * hill.scale(), 0),
                "Backdrop continuation breaks its join or still slopes uphill");
        for (int side : {-1,1}) {
            const double z = side * 200 * hill.scale();
            const double a = hill.outrunBottom() + 60 * hill.scale();
            const double b = a + 60 * hill.scale();
            require(visual::backdropGround(hill, b, z) < visual::backdropGround(hill, a, z),
                    "Distant outrun flank still climbs into a background wall");
        }
        for (int side : {-1,1}) {
            require(visual::valleyEdgeOpacity(hill, side * 178 * hill.scale()) == 1 &&
                        visual::valleyEdgeOpacity(hill, side * 200 * hill.scale()) < 1e-12,
                    "Valley transition affects tree foundations or leaves a hard outer edge");
            require(visual::valleyEdgeOpacity(hill, side * 190 * hill.scale()) == 1 &&
                        visual::valleyEdgeOpacity(hill, side * 195 * hill.scale()) == 1,
                    "Terrain must remain opaque up to the final five scaled metres");
            require(visual::valleyEdgeOpacity(hill, side * 196 * hill.scale()) >
                        visual::valleyEdgeOpacity(hill, side * 199 * hill.scale()),
                    "Valley transition does not fade toward the boundary");
        }
        require(visual::valleyEdgeOpacity(hill, 0) == 1 &&
                    visual::valleyBackOpacity(hill, hill.inrunStart()) == 1,
                "Valley transition changes opacity in the playable corridor");
        require(visual::valleyBackStart(hill) <= hill.inrunStart() - 60 * hill.scale() &&
                    visual::valleyEdgeOpacity(hill, 0, visual::valleyBackStart(hill)) == 0 &&
                    visual::valleyBackOpacity(hill, visual::valleyBackStart(hill) + 5 * hill.scale()) > .999999,
                "Decorative upstream cap must fade outside the original inrun");
    }
    {
        auto redLight = visual::environmentLight({});
        redLight.ambient = {1,0,0};
        redLight.direct = redLight.fill = {0,0,0};
        const auto tint = visual::mountainTint(redLight);
        require(tint.x > .9 && tint.y == 0 && tint.z == 0,
                "Photographic snow ignores the scene's actual light colors");
    }
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
        require(weather[0] > 43500 && weather[0] < 46500 && weather[1] > 14000 && weather[1] < 16000 &&
                    weather[2] > 28500 && weather[2] < 31500 && weather[3] > 9000 && weather[3] < 11000,
                "Random weather distribution is incorrect");
        for (int time = 0; time < 4; ++time)
            for (int weather = 0; weather < 4; ++weather) {
                const Conditions conditions{TimeOfDay(time), Weather(weather), 7654321};
                const auto light = visual::environmentLight(conditions);
                const float glitter = visual::snowGlitterSunlight(light);
                require(glitter >= 0 && glitter <= 1, "Snow glitter strength out of range");
                require((time == 3 || weather != 0) ? glitter == 0 : glitter > .8f,
                        "Solar snow glints ignore daylight/weather");
                for (int angle = 0; angle < 48; ++angle) {
                    const Vec3 direction = Vec3{std::cos(angle * .31), (angle % 9 - 2) * .15,
                                                std::sin(angle * .31)}.normalized();
                    const auto sky = visual::skyColor(light, conditions, direction);
                    const auto repeat = visual::skyColor(light, conditions, direction);
                    const auto neighbor = visual::skyColor(light, conditions,
                                                          (direction + Vec3{1e-6, 0, 0}).normalized());
                    const double cover = visual::skyCloudCover(conditions, direction);
                    if (weather == int(Weather::Sunny))
                        require(cover == 0, "Clear weather must not draw clouds");
                    require(cover >= 0 && cover <= 1 && (sky - repeat).length() == 0 &&
                                (sky - neighbor).length() < 1e-4,
                            "Sky clouds flicker, have hard seams or invalid coverage");
                    for (double channel : {sky.x, sky.y, sky.z})
                        require(std::isfinite(channel) && channel >= 0 && channel <= 1,
                                "Sky color is outside its valid range");
                }
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
                if (time == int(TimeOfDay::Day) && weather == int(Weather::Sunny))
                    require(light.skyTop.z - light.skyTop.x > .35 &&
                                light.fogEnd >
                                    visual::environmentLight({TimeOfDay::Day, Weather::Overcast, 0}).fogEnd *
                                        1.8,
                            "Clear daylight has a gray sky or excessive distance haze");
                if (weather == int(Weather::Sunny) &&
                    (time == int(TimeOfDay::Morning) || time == int(TimeOfDay::Evening))) {
                    const Vec3 toward = Vec3{light.direction.x, .02, light.direction.z}.normalized();
                    const Vec3 away = Vec3{-light.direction.x, .02, -light.direction.z}.normalized();
                    const auto upper = visual::skyColor(light, conditions, {0, 1, 0});
                    const auto warm = visual::skyColor(light, conditions, toward);
                    const auto cool = visual::skyColor(light, conditions, away);
                    require(upper.z > upper.x && cool.z > cool.x &&
                                (warm.x - warm.z) - (cool.x - cool.z) > .1,
                            "Twilight paints the upper/opposite sky orange or loses the warm sun horizon");
                }
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
        replay.jumper = JumperAppearance::Cartoon;
        replay.environment = {TimeOfDay::Evening, Weather::Snowfall, 0xffffffffU};
        const auto replayPath = directory / "replay.pvr";
        Replay loadedReplay;
        require(saveReplay(replayPath.string(), replay) && loadReplay(replayPath.string(), loadedReplay) &&
                    loadedReplay.hasEnvironment && loadedReplay.environment == replay.environment &&
                    loadedReplay.frames.size() == replay.frames.size() && loadedReplay.jumper == replay.jumper,
                "Replay does not retain the jump's actual resolved conditions and character");
        auto legacyReplay = contents(replayPath);
        const auto environmentTag = legacyReplay.find("ENV ");
        legacyReplay.erase(environmentTag, legacyReplay.find('\n', environmentTag) - environmentTag + 1);
        write(replayPath, legacyReplay);
        require(loadReplay(replayPath.string(), loadedReplay) && !loadedReplay.hasEnvironment &&
                    loadedReplay.jumper == JumperAppearance::Cartoon,
                "Character metadata without atmosphere must remain readable");
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
