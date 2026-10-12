#include "competition.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
Result result(double total, double distance = 100) {
    Result r; r.total = total; r.distance = distance; return r;
}
CupSetup roster(int players, std::vector<int> selected = {0, 1}) {
    CupSetup setup;
    setup.hills = std::move(selected);
    for (int p = 0; p < players; ++p)
        setup.participants.push_back({"Player " + std::to_string(p), "NOR", false, p % 6, -1, 0});
    return setup;
}
std::string contents(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int sourceRankCases(const std::string &path) {
    std::ifstream in(path);
    require(bool(in), "Missing independent original rank fixtures");
    std::string line, caseName, previous;
    std::getline(in, line);
    std::vector<float> values;
    std::vector<int> order, ranks;
    int cases = 0;
    auto verify = [&] {
        if (values.empty()) return;
        require(originalCompetitionOrder(values) == order, "Original instruction order mismatch");
        require(originalCompetitionRanks(values) == ranks, "Original instruction rank mismatch");
        ++cases;
    };
    while (std::getline(in, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        int count, index, expectedOrder, expectedRank;
        float value;
        row >> caseName >> count >> index >> value >> expectedOrder >> expectedRank;
        require(bool(row), "Malformed original rank fixture");
        if (previous != caseName) { verify(); values.clear(); order.clear(); ranks.clear(); }
        previous = caseName;
        values.push_back(value); order.push_back(expectedOrder); ranks.push_back(expectedRank);
    }
    verify();
    return cases;
}
int sourceAccumulationCases(const std::filesystem::path &path) {
    std::ifstream in(path);
    require(bool(in), "Missing independent original cumulative score fixtures");
    std::string line, name;
    std::getline(in, line);
    int cases = 0;
    while (std::getline(in, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        int hill, k, disqualified;
        float previous, expected;
        Result r;
        row >> name >> hill >> k >> previous >> r.distance >> disqualified >> expected;
        for (auto &mark : r.judges) row >> mark;
        require(bool(row) && hills()[hill].k == k, "Malformed original cumulative score fixture");
        r.disqualified = disqualified;
        require(originalCompetitionTotal(previous, hills()[hill], r) == expected,
                "Original cumulative distance/judge float32 stores and truncation mismatch");
        if (disqualified) {
            // The before-board source branch uses the same zero-distance
            // arithmetic but leaves judge values20 without selecting them.
            r.disqualified = false;
            r.beforeTakeoff = true;
            r.judges.fill(20);
            require(originalCompetitionTotal(previous, hills()[hill], r) == expected,
                    "Before-board contact must skip unselected judges and score truncation");
        }
        ++cases;
    }
    return cases;
}
void sourceCupRules() {
    CupSetup setup = roster(4, {5, 0, 1});
    setup.doubleSeason = true;
    Competition cup;
    require(cup.start(setup, 1, 2, 3), "Start configurable World Cup");
    require(cup.setup().hills == std::vector<int>({0, 1, 5}) && cup.eventCount() == 6,
            "Original hill selection traverses index order and may repeat once");
    require(cup.startOrder() == std::vector<int>({0, 1, 2, 3}), "First hill starts in roster order");
    require(cup.submitResult(result(100)), "Player zero round one");
    require(cup.submitResult(result(150)), "Player one round one");
    require(cup.submitResult(result(150)), "Player two tied round one");
    require(cup.submitResult(result(50)), "Player three round one");
    require(cup.round() == 1 && cup.startOrder() == std::vector<int>({3, 0, 2, 1}),
            "Round two starts weakest first; tied source ranks reverse roster order");
    for (int i = 0; i < 4; ++i) require(cup.submitResult(result(0)), "Complete second round");
    require(cup.eventComplete() && cup.currentJumper() == -1, "Completed event awaits standings");
    require(cup.entries()[1].cupPoints == 100 && cup.entries()[2].cupPoints == 100 &&
                cup.entries()[0].cupPoints == 60 && cup.entries()[3].cupPoints == 50,
            "Equal first places both receive100; next rank is third, worth60");
    require(!cup.submitResult(result(500)), "Repeated event result cannot award extra points");
    require(cup.advanceEvent(4, 5), "Advance to next selected hill");
    require(cup.hill() == 1 && cup.startOrder() == std::vector<int>({3, 0, 2, 1}),
            "Subsequent first round starts in reverse cup standings");
    int jumps = 0;
    while (cup.active()) {
        if (cup.eventComplete()) require(cup.advanceEvent(6, 7), "Advance repeated season");
        else { require(cup.submitResult(result(100)), "Complete configured full season"); ++jumps; }
    }
    require(cup.complete() && jumps == 40, "All selected hills run twice, two jumps per entrant");
    Competition all16;
    require(all16.start(roster(16, {31}), 1, 2, 3), "Original maximum16 participants supported");
    for (int p = 0; p < 32; ++p) require(all16.submitResult(result(double(p))), "Full16-player event");
    auto ranks = all16.ranks();
    for (int p = 0; p < 16; ++p)
        require(all16.entries()[p].cupPoints == originalCupAward(CupMode::World, ranks[p]),
                "All16 source cup places award original points");
    CupSetup invalid = roster(17);
    require(!all16.start(invalid, 1, 2, 3), "Reject more than original16 participants");
    invalid = roster(1, {0, 0});
    require(!all16.start(invalid, 1, 2, 3), "Reject duplicate selected hills");
}
void sourceTeamRules() {
    CupSetup setup = defaultTeamCupSetup();
    setup.hills = {0, 1};
    Competition cup;
    require(cup.start(setup, 123, 456, 789), "Start four original teams");
    for (int round = 0; round < 2; ++round)
        for (int member = 0; member < 4; ++member)
            for (int team = 0; team < 4; ++team) {
                int index = cup.currentJumper();
                const auto &p = cup.entries()[index].participant;
                require(cup.round() == round && cup.group() == member && p.member == member && p.team == team,
                        "Original Team Cup alternates teams in each of four member groups");
                require(cup.submitResult(result(team < 2 ? 200 : team == 2 ? 100 : 50)), "Team member result");
            }
    require(cup.eventComplete(), "Team event completes after32 jumps");
    require(cup.teamStandings()[0].eventTotal == 1600 && cup.teamStandings()[1].eventTotal == 1600 &&
                cup.teamStandings()[0].cupPoints == 200 && cup.teamStandings()[1].cupPoints == 200 &&
                cup.teamStandings()[2].cupPoints == 120 && cup.teamStandings()[3].cupPoints == 100,
            "Eight scores sum per team and tied teams share source200-point first place");
    require(cup.advanceEvent(234, 567), "Second team hill");
    require(cup.entries()[cup.currentJumper()].participant.team == 3, "Lowest cup team starts next hill");
    for (int group = 0; group < 8; ++group)
        for (int team : {3, 2, 1, 0}) {
            require(cup.entries()[cup.currentJumper()].participant.team == team,
                    "Team start order remains fixed for all groups in both rounds");
            require(cup.submitResult(result(100)), "Complete team season");
        }
    require(cup.advanceEvent(1, 2) && cup.complete(), "Team Cup finishes");
    setup.participants.pop_back();
    require(!cup.start(setup, 1, 2, 3), "Reject incomplete team");
}
void checkpoints(const std::filesystem::path &directory) {
    Competition cup, restored;
    auto setup = roster(4, {0, 1});
    setup.participants[2].computer = true;
    setup.participants[2].level = 5;
    setup.participants[0].name = "Åse \"A\" Hansen";
    require(cup.start(setup, 0x124578ab, 0x789023ff, 0x864cd112), "Seeded checkpoint cup");
    auto path = directory / "cup.sjc";
    int saves = 0;
    while (cup.active()) {
        if (cup.eventComplete()) {
            require(cup.advanceEvent(789, 345), "Checkpoint next hill");
        } else {
            cup.prepareJump(0x34567200u + unsigned(saves));
            auto weather = cup.weather();
            OriginalRandom random(0x56780000u + unsigned(saves));
            weather.advance(10, random);
            require(saveCompetition(path.string(), cup) && loadCompetition(path.string(), restored),
                    "Every jump checkpoint saves and loads");
            auto copy = directory / "cup-copy.sjc";
            require(saveCompetition(copy.string(), restored) && contents(path) == contents(copy),
                    "Every field round-trips exactly, including float32 weather and AI traits");
            require(restored.currentJumper() == cup.currentJumper() && restored.preparedJumpSeed() == cup.preparedJumpSeed(),
                    "Current player and seed survive interruption");
            cup.retainJumpState(weather, {.123456f, -.234567f});
            cup.submitResult(result(125.1 + saves));
            ++saves;
        }
    }
    require(saves == 16 && saveCompetition(path.string(), cup) && loadCompetition(path.string(), restored) && restored.complete(),
            "A completed cup round-trips without becoming resumable");
    auto bytes = contents(path);
    bytes.back() ^= 1;
    std::ofstream(path, std::ios::binary) << bytes;
    require(!loadCompetition(path.string(), restored) && restored.complete(),
            "Corruption is rejected while retaining the destination's existing state");
    Competition team;
    setup = defaultTeamCupSetup(); setup.hills = {0};
    require(team.start(setup, 100, 200, 300), "Team checkpoints");
    for (int n = 0; n < 32; ++n) {
        require(saveCompetition(path.string(), team) && loadCompetition(path.string(), restored),
                "Interleaved team member checkpoint reloads");
        require(restored.currentJumper() == team.currentJumper(), "Team cursor round-trip");
        require(team.submitResult(result(100 + n)), "Submit checkpoint team result");
    }
    require(saveCompetition(path.string(), team) && loadCompetition(path.string(), restored) && restored.eventComplete(),
            "Completed team event cursor reloads");
}
void nativeResume(const std::filesystem::path &directory) {
    auto setup = roster(2, {0});
    for (auto &p : setup.participants) p.computer = true;
    Competition original, restored;
    require(original.start(setup, 123456, 98765, 34567), "Real native CPU resume test");
    // Independently generated closed-form LCG/x87 weather-to-form fixture:
    // seed98765 produces18 weather draws, then12 form draws per CPU.
    require(original.weather().x == -.47153300046920776f &&
                original.weather().y == -1.1234498023986816f &&
                original.entries()[0].skills.values[0] == .21090324223041534f &&
                original.entries()[0].skills.values[1] == -.2607806622982025f &&
                original.entries()[1].skills.values[0] == .13935260474681854f &&
                original.entries()[1].skills.values[1] == .4217098355293274f,
            "Original weather and hill form share one continuous RNG draw sequence");
    original.prepareJump(76543);
    require(saveCompetition((directory / "cpu.sjc").string(), original) &&
                loadCompetition((directory / "cpu.sjc").string(), restored), "CPU checkpoint persists");
    Simulation before, after;
    for (auto pair : {std::pair<Competition *, Simulation *>(&original, &before), {&restored, &after}}) {
        auto &cup = *pair.first; auto &sim = *pair.second;
        sim.resetWithWeather(cup.hill(), cup.weather(), cup.preparedJumpSeed());
        sim.pose.auxiliary = cup.auxiliary();
        sim.configureAi(cup.entries()[cup.currentJumper()].skills, true, true);
        for (int n = 0; n < 10000 && sim.phase != Phase::Finished && sim.phase != Phase::Disqualified; ++n)
            sim.step(ORIGINAL_TICK, {});
        require(sim.phase == Phase::Finished || sim.phase == Phase::Disqualified, "CPU jump completes after resume");
    }
    require(before.result.total == after.result.total && before.frames.size() == after.frames.size(),
            "Resumed CPU jump preserves result and duration");
    for (size_t i = 0; i < before.frames.size(); ++i)
        require(before.frames[i].x == after.frames[i].x && before.frames[i].y == after.frames[i].y &&
                    before.frames[i].pose == after.frames[i].pose && before.frames[i].windSpeed == after.frames[i].windSpeed,
                "Every independently propagated frame matches after checkpoint resume");
}
void beforeBoardCheckpoint(const std::filesystem::path &directory, const std::filesystem::path &legacyPath) {
    Competition cup, restored;
    require(cup.start(roster(2, {1}), 123, 456, 789), "Before-board checkpoint cup");
    Result contact;
    contact.beforeTakeoff = contact.crashed = true;
    contact.crashKind = 2;
    contact.distancePoints = contact.total = -84;
    contact.judges.fill(20);
    require(cup.submitResult(contact) && cup.entries()[0].eventTotal == -84,
            "Actual Swiss before-board zero-distance score excludes all unselected20 marks");
    auto path = directory / "before-board.sjc";
    require(saveCompetition(path.string(), cup) && loadCompetition(path.string(), restored) &&
                restored.entries()[0].results[0].beforeTakeoff && restored.entries()[0].eventTotal == -84 &&
                restored.entries()[0].results[0].judges == contact.judges,
            "Version2 checkpoints preserve before-board result provenance and untouched judges");
    require(contents(path).find("SJP_COMPETITION_2\n") == 0, "Write current checkpoint version2");
    require(loadCompetition(legacyPath.string(), restored) && restored.currentJumper() == 0 &&
                restored.preparedJumpSeed() == 123456 && !restored.entries()[0].results[0].beforeTakeoff,
            "Version1 checkpoints remain readable with absent before-board flag defaultingfalse");
}
}
int main(int argc, char **argv) {
    auto directory = std::filesystem::temp_directory_path() /
        ("sjp-competition-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        require(argc == 2, "Supply original sort fixtures");
        std::filesystem::create_directories(directory);
        int cases = sourceRankCases(argv[1]);
        int accumulation = sourceAccumulationCases(std::filesystem::path(argv[1]).parent_path() / "accumulation-golden.csv");
        sourceCupRules(); sourceTeamRules(); checkpoints(directory); nativeResume(directory);
        beforeBoardCheckpoint(directory, std::filesystem::path(argv[1]).parent_path() / "checkpoint-v1.sjc");
        std::filesystem::remove_all(directory);
        std::cout << cases << " original ASM ranking cases, " << accumulation << " original aggregate/timeout scores; World16, Team4, hotseat, two seasons, exact checkpoints and independent CPU resume passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
