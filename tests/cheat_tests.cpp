#include "competition.h"
#include "original_replay.h"
#include "presentation_cache.h"
#include "storage.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
Simulation jump(int hill, bool suit) {
    Simulation s(hill, 0);
    s.resetOriginal(hill, 12345, 67890);
    s.setWind(0, 0);
    s.setAdjustedSuit(suit);
    for (int i = 0; i < 14000 && s.phase != Phase::Finished; ++i) {
        s.step(ORIGINAL_TICK, autopilot(s));
        if (s.phase != Phase::Ready) s.setAdjustedSuit(!suit);
        require(s.result.adjustedSuit == suit, "In-flight toggle changed record eligibility");
    }
    require(s.phase == Phase::Finished, "Adjusted suit jump did not finish");
    return s;
}
}
int main() {
    try {
        const auto dir = std::filesystem::temp_directory_path() /
            ("tpv-cheats-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        Records records;
        require(!records.adjustedSuit, "Cheat must default to off");
        for (int hill = 0; hill < int(HILL_COUNT); ++hill) {
            auto normal = jump(hill, false), cheated = jump(hill, true);
            require(cheated.originalRecordedDistance() >= normal.originalRecordedDistance() * 1.1,
                    "Adjusted suit gave less than 10% extra length in the still-air reference jump");
            size_t i = 0;
            while (i < normal.frames.size() && normal.frames[i].phase == int(Phase::Inrun)) {
                require(i < cheated.frames.size() && visual::samePresentationFrame(normal.frames[i], cheated.frames[i]),
                        "Adjusted suit changed the inrun or takeoff controls");
                ++i;
            }
            require(i > 5, "No inrun frames compared");
            Result record = cheated.result;
            record.crashed = false;
            require(!updateHillRecord(records, hill, record, true) && records.best[size_t(hill)] == 0,
                    "Cheated human jump set a record");
            OriginalReplayData original;
            std::string error;
            require(!buildOriginalReplay(cheated, original, &error) && error.find("suit") != std::string::npos,
                    "Cheat was exported as an original input replay");
            auto replay = cheated.replay();
            const auto path = (dir / "cheat.pvr").string();
            Replay loaded;
            require(saveReplay(path, replay) && loadReplay(path, loaded) && loaded.result.adjustedSuit,
                    "Replay lost its cheat marker");
            require(!updateHillRecord(records, hill, loaded.result, true), "Reloaded cheat replay set a record");
            replay.originalRpl.resize(1222);
            require(!saveReplay(path, replay), "Cheat replay accepted an original input recording");
            cheated.resetOriginal(hill, 12345, 67890);
            require(!cheated.result.adjustedSuit, "Reset retained a previous jump's cheat");
        }
        records.best[4] = 81.23;
        records.adjustedSuit = true;
        const auto prefs = (dir / "records.dat").string();
        require(saveRecords(prefs, records), "Cannot save cheat preference");
        auto restored = loadRecords(prefs);
        require(restored.adjustedSuit && restored.best == records.best, "Preference changed existing records");
        for (CupMode mode : {CupMode::World, CupMode::Team}) {
            auto setup = mode == CupMode::World ? defaultCupSetup(123) : defaultTeamCupSetup(123);
            setup.hills = {4};
            setup.adjustedSuit = true;
            Competition cup;
            require(cup.start(setup, 123, 456, 789), "Cheat cup was rejected");
            while (!cup.eventComplete()) {
                const int who = cup.currentJumper(), round = cup.round();
                const bool human = !cup.entries()[size_t(who)].participant.computer;
                Result result;
                result.distance = 85;
                result.total = 100;
                require(cup.submitResult(result), "Cup rejected cheat-compatible result");
                require(cup.entries()[size_t(who)].results[size_t(round)].adjustedSuit == human,
                        "Cup did not retain human cheat / ordinary CPU distinction");
                const auto file = (dir / "cup.sjc").string();
                require(saveCompetition(file, cup), "Cannot checkpoint cheat cup");
                Competition resumed;
                require(loadCompetition(file, resumed) && resumed.setup().adjustedSuit &&
                            resumed.entries()[size_t(who)].results[size_t(round)].adjustedSuit == human,
                        "Resume lost cup cheat state");
                cup = std::move(resumed);
            }
            require(cup.advanceEvent(123, 456) && cup.complete(), "Cheat cup could not complete");
        }
        std::filesystem::remove_all(dir);
        std::cout << "PASS: adjusted suit, original inrun, record isolation, replay and cup persistence\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
