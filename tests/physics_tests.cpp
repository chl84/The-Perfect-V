#include "physics.h"
#include "storage.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace sjp;
namespace {
int checks = 0;
void require(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
void near(double a, double b, double tolerance, const char *message) {
    require(std::abs(a - b) < tolerance, message);
}
Simulation fly(int hill, double wind, double takeoffLead = .21, Landing landing = Landing::Telemark,
               double pitchError = 0) {
    Simulation sim(hill, wind);
    for (int tick = 0; tick < 6000 && sim.phase != Phase::Finished; tick++) {
        Input in = autopilot(sim);
        in.takeoff = sim.originalStage == 3 && sim.x > -std::max(1.0, sim.vx) * takeoffLead;
        if (sim.phase == Phase::Flight) {
            in.pitchDelta = clamp(sim.optimalPitch() + pitchError - sim.pitch, -.015, .015);
            if (in.land != Landing::None)
                in.land = landing;
        }
        sim.step(1. / 120, in);
        require(std::isfinite(sim.x) && std::isfinite(sim.y) && std::isfinite(sim.vx) &&
                    std::isfinite(sim.vy),
                "Non-finite physics state");
    }
    require(sim.phase == Phase::Finished, "Jump did not finish within the bound");
    return sim;
}
} // namespace
int main() {
    try {
        near(scoreStyle({19, 18, 20, 17, 19.5}), 56.5, 1e-8, "Drop the highest and lowest judge marks");
        near(scoreStyle({18, 18, 18, 18, 18}), 54, 1e-8, "Tied marks still discard exactly two judges");
        near(distancePoints(hills()[14], 100), 80, 1e-8, "Original K90 normal-hill scoring");
        near(distancePoints(hills()[9], 105), 78, 1e-8, "Original K95 large-hill scoring");
        near(distancePoints(hills()[19], 150), 78, 1e-8, "Original K140 large-hill scoring");
        near(distancePoints(hills()[24], 155), 132, 1e-8, "Original K145 ski-flying scoring");
        for (const auto &h : hills()) {
            near(distancePoints(h, h.k), h.flying() ? 120 : 60, 1e-8, "K point score");
            near(distancePoints(h, h.k + 10) - distancePoints(h, h.k), h.pointsPerMeter() * 10, 1e-8,
                 "Distance points per metre");
            near(h.distance(h.atDistance(h.k)), h.k, .001, "Distance measured along hill");
        }
        for (size_t h = 0; h < hills().size(); h++) {
            auto good = fly(h, 2);
            // Distinct original geometry may cause a hard impact on an overlong
            // jump. A fall remains a valid completed result, including in a cup.
            require(good.result.distance > 0 && good.result.distance < 500 &&
                        std::isfinite(good.result.total),
                    "Every original hill produces a valid scored jump");
            near(good.result.stylePoints, scoreStyle(good.result.judges), 1e-8,
                 "Original judges select the middle three marks");
            require(good.result.stylePoints >= 0 && good.result.stylePoints <= (good.crashed ? 30 : 60),
                    "Original standing and fall score bounds");
            require(good.frames.size() > 100, "Replay records the full jump");
            auto tail = fly(h, -3);
            require(std::isfinite(tail.result.total), "Every profile handles tailwind");
        }
        auto tele = fly(2, 2), twoFoot = fly(2, 2, .21, Landing::TwoFoot), early = fly(2, 2, .5),
             unstable = fly(2, 2, .21, Landing::Telemark, .8), noLanding = fly(2, 2, .21, Landing::None);
        require(tele.result.stylePoints > twoFoot.result.stylePoints,
                "Telemark earns more style points than two-foot");
        require(!twoFoot.crashed, "Two-foot landing remains safe");
        require(tele.result.distance > early.result.distance + 10, "Takeoff timing changes distance");
        require(std::abs(tele.result.distance - unstable.result.distance) > 5,
                "Original flight posture changes the trajectory");
        require(noLanding.crashed, "Failing to prepare a landing causes a fall");

        Simulation heldStart(2, 2);
        Input held;
        held.start = true;
        held.buttonMask = 1;
        heldStart.step(ORIGINAL_TICK, held);
        require(heldStart.phase == Phase::Ready, "Original start gate ignores the first64 ticks");
        held.start = false;
        while (heldStart.originalCounter < 65)
            heldStart.step(ORIGINAL_TICK, held);
        require(heldStart.phase == Phase::Inrun, "Held left button starts at original counter65");
        for (int i = 0; i < 25; ++i)
            heldStart.step(ORIGINAL_TICK, held);
        require(heldStart.originalStage == 2 && !heldStart.jumped,
                "Holding the start button cannot trigger takeoff after 20 ticks");
        held.buttonMask = 0;
        heldStart.step(ORIGINAL_TICK, held);
        require(heldStart.originalStage == 3, "Releasing the start button enables takeoff");
        held.buttonMask = 3;
        heldStart.step(ORIGINAL_TICK, held);
        require(heldStart.originalStage == 4 && heldStart.jumped,
                "A new two-button hold initiates original takeoff preparation");

        auto contact = [](int stage, double width) {
            Simulation sim(2, 0);
            sim.setWind(0, 0);
            sim.phase = Phase::Flight;
            sim.originalStage = stage;
            sim.x = sim.hill().atDistance(50);
            sim.y = sim.hill().ground(sim.x) + .001;
            sim.vx = 30;
            sim.vy = -60;
            sim.telemarkWidth = width;
            Input both;
            both.buttonMask = 3;
            sim.step(ORIGINAL_TICK, both);
            require(sim.phase == Phase::Outrun, "Contact fixture must reach the ground");
            return sim;
        };
        auto simultaneous = contact(7, 0);
        near(simultaneous.result.telemarkWidth, ORIGINAL_TICK, 1e-9,
             "Landing initiation and contact advance width once on the same tick");
        auto frozen = contact(9, .3f);
        near(frozen.result.telemarkWidth, float(.3f + ORIGINAL_TICK), 1e-9,
             "A frozen telemark gains one original tick at contact");
        auto widest = contact(9, .6f);
        near(widest.result.telemarkWidth, .6f, 1e-9,
             "Original telemark width remains capped at .6 at contact");

        Simulation wholeTicks(2, 2), halfTicks(2, 2);
        for (int i = 0; i < 140; ++i) {
            Input same;
            same.start = wholeTicks.phase == Phase::Ready;
            wholeTicks.step(ORIGINAL_TICK, same);
            halfTicks.step(ORIGINAL_TICK / 2, same);
            same.start = false;
            halfTicks.step(ORIGINAL_TICK / 2, same);
        }
        require(wholeTicks.x == halfTicks.x && wholeTicks.y == halfTicks.y && wholeTicks.vx == halfTicks.vx &&
                    wholeTicks.vy == halfTicks.vy && wholeTicks.windSpeed == halfTicks.windSpeed &&
                    wholeTicks.pose.angles == halfTicks.pose.angles,
                "Render/input subdivisions must preserve the original 70 Hz physics and random sequence");

        Simulation mouseReference(2, 0);
        mouseReference.setWind(0, 0);
        mouseReference.phase = Phase::Flight;
        mouseReference.originalStage = 7;
        mouseReference.x = 10;
        mouseReference.y = 100;
        mouseReference.vx = 30;
        mouseReference.pitch = .6f;
        Simulation accumulatedMouse = mouseReference;
        Input expectedMouse;
        expectedMouse.buttonMask = 0;
        expectedMouse.pitchDelta = originalMousePitchDelta(127, ORIGINAL_TICK) * .3;
        mouseReference.step(ORIGINAL_TICK, expectedMouse);
        Input rawMouse;
        rawMouse.buttonMask = 0;
        rawMouse.mouseDy = 100;
        rawMouse.mouseSensitivity = .3;
        accumulatedMouse.step(ORIGINAL_TICK / 2, rawMouse);
        accumulatedMouse.step(ORIGINAL_TICK / 2, rawMouse);
        require(mouseReference.pitch == accumulatedMouse.pitch,
                "Mouse deltas accumulate until the original tick, then clamp before sensitivity/gain");

        OriginalRandom weatherRandom(0x12345678u);
        OriginalWind carriedWeather;
        carriedWeather.initialize(weatherRandom);
        carriedWeather.advance(10, weatherRandom);
        Simulation nextJump;
        nextJump.resetWithWeather(0, carriedWeather, 0x543210u);
        require(nextJump.originalCounter == 1 && nextJump.weatherState().frameCounter == 1 &&
                    nextJump.weatherState().x == carriedWeather.x &&
                    nextJump.weatherState().y == carriedWeather.y &&
                    nextJump.weatherState().variationStep == carriedWeather.variationStep &&
                    nextJump.weatherState().displayAngle == carriedWeather.displayAngle,
                "A new jump resets its clock while retaining the hill's evolved weather");
        OriginalRandom skillRandom(0x895231u);
        for (size_t h = 0; h < hills().size(); ++h) {
            auto skills = generateOriginalAiSkills(int(h % 5), skillRandom);
            refreshOriginalAiHillForm(skills, skillRandom);
            Simulation cpu;
            cpu.resetOriginal(int(h), 0x123456u + unsigned(h), 0x789012u + unsigned(h));
            cpu.configureAi(skills);
            while (cpu.originalCounter < 70)
                cpu.step(ORIGINAL_TICK, {});
            require(cpu.phase == Phase::Inrun && cpu.originalCounter == 70,
                    "Original CPU starts with its tick70 left-button pulse");
            for (int n = 0; n < 7000 && cpu.phase != Phase::Finished; ++n)
                cpu.step(ORIGINAL_TICK, {});
            require(cpu.phase == Phase::Finished && cpu.jumped && std::isfinite(cpu.result.total),
                    "Original AI completes a scored jump and outrun on every hill");
        }
        // Regression from a saved World Cup checkpoint: single-precision
        // integration lands exactly on the lip, then crosses it next tick.
        // The old strict-negative crossing missed the launch and left the CPU
        // in Inrun indefinitely, ultimately aborting the whole cup frontend.
        OriginalWind lipWeather;
        lipWeather.x = -.12789125740528107f;
        lipWeather.y = -.081578224897384644f;
        lipWeather.variationStep = .1265455037355423f;
        lipWeather.speed = .15169437229633331f;
        lipWeather.angle = 3.7093946933746338f;
        lipWeather.displayAngle = -2.2250008583068848f;
        lipWeather.angularStep = -.15000000596046448f;
        lipWeather.angularCap = .20000000298023224f;
        lipWeather.frameCounter = 1311;
        OriginalAiSkills lipSkills;
        lipSkills.level = 2;
        lipSkills.values = {-.028366954997181892f, .29698070883750916f,
                            -.55901616811752319f, .8394431471824646f};
        Simulation lipCpu;
        lipCpu.resetWithWeather(2, lipWeather, 439484952u);
        lipCpu.pose.auxiliary = {.056951209902763367f, .0059966873377561569f};
        lipCpu.configureAi(lipSkills, true, true);
        bool reachedLipExactly = false, crossedFromLip = false;
        for (int tick = 0; tick < 7000 && lipCpu.phase != Phase::Finished; ++tick) {
            const double beforeX = lipCpu.x;
            const Phase beforePhase = lipCpu.phase;
            lipCpu.step(ORIGINAL_TICK, {});
            reachedLipExactly |= beforePhase == Phase::Inrun && beforeX < 0 && lipCpu.x == 0;
            if (beforePhase == Phase::Inrun && beforeX == 0 && lipCpu.x > 0) {
                crossedFromLip = true;
                require(lipCpu.phase == Phase::Flight,
                        "A CPU crossing from the exact lip must leave the inrun");
            }
        }
        require(reachedLipExactly && crossedFromLip && lipCpu.phase == Phase::Finished &&
                    lipCpu.result.distance > 0 && std::isfinite(lipCpu.result.total),
                "The exact-lip World Cup jump must complete with a scored result");
        Simulation timeout(2, 2);
        for (int i = 0; i < 1900; i++)
            timeout.step(1. / 120, {});
        require(timeout.phase == Phase::Disqualified && timeout.result.disqualified,
                "15-second start limit disqualifies late starts");
        require(timeout.result.total == -183, "Original Czech start timeout retains negative distance score");
        Simulation delayed(2, 2);
        for (int i = 0; i < 600; i++)
            delayed.step(1. / 120, {});
        Input start;
        start.start = true;
        delayed.step(1. / 120, start);
        delayed.step(ORIGINAL_TICK, {});
        require(delayed.frames.front().elapsed >= 4.99, "Replay begins at start and excludes gate waiting");
        std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("sjp-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        Records records;
        records.best[2] = tele.result.distance;
        records.camera = 1;
        records.sound = false;
        records.sensitivity = 1.4;
        auto recordPath = (directory / "records.dat").string();
        require(saveRecords(recordPath, records), "Save records");
        auto loaded = loadRecords(recordPath);
        near(loaded.best[2], records.best[2], .001, "Records persist");
        require(loaded.camera == 1 && !loaded.sound, "Preferences persist");
        auto replayPath = (directory / "test.pvr").string();
        Replay replay = tele.replay(), restored;
        require(saveReplay(replayPath, replay) && loadReplay(replayPath, restored), "Replay round trip");
        require(restored.frames.size() == replay.frames.size(), "Replay retains every frame");
        near(restored.frames[200].x, replay.frames[200].x, .001, "Replay coordinate precision");
        require(saveReplay(replayPath, noLanding.replay()) && loadReplay(replayPath, restored),
                "Fall replay round trip");
        require(restored.frames.back().crashed == 1, "Replays preserve the fall animation state");
        auto widestReplay = widest.replay();
        widestReplay.frames.push_back(widest.frame());
        require(saveReplay(replayPath, widestReplay) && loadReplay(replayPath, restored),
                "The widest original telemark must remain a valid saved replay");
        std::ofstream(replayPath) << "SJP_REPLAY_4\nDSJ2_ORIGINAL_GAMEPLAY_1\n"
                                  << hills()[2].id << " 2 999999999\n";
        require(!loadReplay(replayPath, restored), "Reject oversized replay before allocation");
        require(saveReplay(replayPath, replay), "Write original pose replay fixture");
        auto decreasing = replay;
        decreasing.frames[200].elapsed = decreasing.frames[199].elapsed - 1;
        require(saveReplay(replayPath, decreasing), "Write decreasing-time replay fixture");
        require(!loadReplay(replayPath, restored), "Reject decreasing replay time");
        std::ofstream(recordPath) << "SJP_RECORDS_1\n-2 0 0 0 0 0\n";
        require(loadRecords(recordPath).best[0] == 0, "Malformed records fall back to defaults");
        std::filesystem::remove_all(directory);
        std::cout << "Passed " << checks << " physics and persistence checks.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
