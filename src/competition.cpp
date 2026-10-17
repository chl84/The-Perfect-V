#include "competition.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace sjp {
namespace {
bool fail(std::string *error, const char *message) {
    if (error) *error = message;
    return false;
}
}
std::vector<int> originalCompetitionOrder(const std::vector<float> &values) {
    std::vector<int> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return values[a] > values[b]; });
    return order;
}
std::vector<int> originalCompetitionRanks(const std::vector<float> &values) {
    auto order = originalCompetitionOrder(values);
    std::vector<int> ranks(values.size());
    int rank = 1;
    for (size_t i = 0; i < order.size(); ++i) {
        if (i && values[order[i]] != values[order[i - 1]]) rank = int(i + 1);
        ranks[order[i]] = rank;
    }
    return ranks;
}
int originalCupAward(CupMode mode, int rank) {
    constexpr int world[] = {100, 80, 60, 50, 45, 40, 36, 32, 29, 26, 24, 22, 20, 18, 16, 15};
    constexpr int team[] = {200, 160, 120, 100};
    if (rank < 1) return 0;
    if (mode == CupMode::Team) return rank <= 4 ? team[rank - 1] : 0;
    return rank <= 16 ? world[rank - 1] : 0;
}
float originalCompetitionTotal(float previous, const Hill &hill, const Result &result) {
    // Main274c2..2754e adds the distance expression to the previous aggregate
    // BEFORE its DWORD store. Main27a91..27aae adds each retained judge mark
    // with a separate store; main27c6b truncates the resulting total to0.1.
    const long double perMeter = hill.k >= 145 ? 1.2L : hill.k > 90 ? 1.8L : 2.0L;
    const long double base = hill.k >= 145 ? 120.0L : 60.0L;
    const bool zeroDistance = result.disqualified || result.beforeTakeoff;
    float total = float((static_cast<long double>(previous) + base) +
                        (static_cast<long double>(zeroDistance ? 0 : float(result.distance)) - hill.k) * perMeter);
    // The timeout path263ac..26481 has zero distance/style, but still adds
    // negative zero-distance points. It skips the post-landing truncation.
    if (zeroDistance) return total;
    auto judges = result.judges;
    std::sort(judges.begin(), judges.end());
    for (int i = 1; i < 4; ++i) total = float(static_cast<long double>(total) + float(judges[i]));
    return float(std::trunc(static_cast<long double>(total) * 10.0L) / 10.0L);
}
bool validateCupSetup(const CupSetup &setup, std::string *error) {
    if (setup.mode != CupMode::World && setup.mode != CupMode::Team)
        return fail(error, "Unknown cup mode");
    if (setup.participants.empty() || setup.participants.size() > 16)
        return fail(error, "Select between one and sixteen participants");
    if (setup.hills.empty() || setup.hills.size() > HILL_COUNT)
        return fail(error, "Select at least one hill");
    std::set<int> selected;
    for (int hill : setup.hills)
        if (hill < 0 || hill >= int(HILL_COUNT) || !selected.insert(hill).second)
            return fail(error, "Invalid or duplicate hill");
    for (const auto &p : setup.participants) {
        if (p.name.empty() || p.name.size() > 64 || p.country.empty() || p.country.size() > 16 ||
            p.name.find_first_of("\r\n") != std::string::npos ||
            p.country.find_first_of("\r\n") != std::string::npos || p.level < 0 || p.level > 5)
            return fail(error, "Invalid participant name, country or skill level");
    }
    if (setup.mode == CupMode::Team) {
        if (setup.teams.empty() || setup.teams.size() > 4 || setup.participants.size() != 4 * setup.teams.size())
            return fail(error, "Each of one to four teams must have exactly four members");
        std::set<std::pair<int, int>> members;
        for (const auto &p : setup.participants)
            if (p.team < 0 || p.team >= int(setup.teams.size()) || p.member < 0 || p.member > 3 ||
                !members.insert({p.team, p.member}).second)
                return fail(error, "Invalid or duplicate team member");
        for (const auto &team : setup.teams)
            if (team.name.empty() || team.name.size() > 64 || team.country.empty() || team.country.size() > 16 ||
                team.name.find_first_of("\r\n") != std::string::npos ||
                team.country.find_first_of("\r\n") != std::string::npos)
                return fail(error, "Invalid team name or country");
    } else if (!setup.teams.empty()) return fail(error, "World Cup does not use teams");
    return true;
}
void assignCpuIdentity(CupSetup &setup, size_t index, uint32_t seed, bool retainCountry) {
    if (index >= setup.participants.size())
        return;
    auto &p = setup.participants[index];
    const std::string country = setup.mode == CupMode::Team && p.team >= 0 && p.team < int(setup.teams.size())
                                    ? setup.teams[size_t(p.team)].country
                                : retainCountry ? p.country
                                                : "";
    if (!country.empty() && !nationality(country))
        return;
    for (;;) {
        const auto identity = randomAthleteIdentity(seed++, country);
        const bool duplicate = std::any_of(
            setup.participants.begin(), setup.participants.end(),
            [&](const CupParticipant &other) { return &other != &p && other.name == identity.name; });
        if (!duplicate) {
            p.name = identity.name;
            p.country = identity.country;
            return;
        }
    }
}
CupSetup defaultCupSetup(uint32_t identitySeed) {
    CupSetup setup;
    setup.participants.push_back({"PLAYER 1", "NOR", false, 2, -1, 0});
    for (int i = 1; i < 8; ++i) {
        setup.participants.push_back({"", "", true, 2, -1, 0});
        assignCpuIdentity(setup, size_t(i), environment::hash(identitySeed ^ uint32_t(i)));
    }
    for (int h = 0; h < int(HILL_COUNT); ++h) setup.hills.push_back(h);
    return setup;
}
CupSetup defaultTeamCupSetup(uint32_t identitySeed) {
    CupSetup setup;
    setup.mode = CupMode::Team;
    for (int t = 0; t < 4; ++t) {
        const Nationality *nation = nationality("NOR");
        if (t != 0) {
            uint32_t seed = environment::hash(identitySeed ^ uint32_t(t));
            do {
                nation = nationality(randomAthleteIdentity(seed++).country);
            } while (std::any_of(setup.teams.begin(), setup.teams.end(),
                                 [&](const CupTeam &team) { return team.country == nation->code; }));
        }
        setup.teams.push_back({t == 0 ? "NORWAY" : nation->name, nation->code});
        for (int member = 0; member < 4; ++member) {
            setup.participants.push_back({std::string(nation->code) + " " + std::to_string(member + 1),
                                          nation->code, t != 0, 2, t, member});
            if (t != 0)
                assignCpuIdentity(setup, setup.participants.size() - 1,
                                  environment::hash(identitySeed ^ uint32_t(t * 4 + member)));
        }
    }
    for (int h = 0; h < int(HILL_COUNT); ++h) setup.hills.push_back(h);
    return setup;
}
bool Competition::start(const CupSetup &setup, uint32_t skillsSeed, uint32_t weatherSeed,
                        uint32_t hillFormSeed, std::string *error, environment::Selection environment) {
    if (!validateCupSetup(setup, error)) return false;
    Competition next;
    next.visualSelection = sjp::environment::valid(environment) ? environment : sjp::environment::Selection{};
    next.config = setup;
    std::sort(next.config.hills.begin(), next.config.hills.end());
    next.competitors.resize(setup.participants.size());
    OriginalRandom random(skillsSeed);
    int staleTeamLevel = 0;
    // Original Team Cup stores members interleaved by group, even when the
    // modern setup supplies them contiguously per team. Preserve draw order.
    std::vector<int> generationOrder(setup.participants.size());
    std::iota(generationOrder.begin(), generationOrder.end(), 0);
    if (setup.mode == CupMode::Team)
        std::stable_sort(generationOrder.begin(), generationOrder.end(), [&](int a, int b) {
            const auto &pa = setup.participants[a], &pb = setup.participants[b];
            return pa.member != pb.member ? pa.member < pb.member : pa.team < pb.team;
        });
    for (size_t i = 0; i < setup.participants.size(); ++i)
        next.competitors[i].participant = setup.participants[i];
    for (int i : generationOrder) {
        const auto &p = setup.participants[i];
        if (p.computer)
            next.competitors[i].skills = generateOriginalAiSkills(
                p.level, random, setup.mode == CupMode::Team ? &staleTeamLevel : nullptr);
        else next.competitors[i].skills.level = p.level;
    }
    next.teamScores.resize(setup.teams.size());
    next.started = true;
    next.initializeEvent(weatherSeed, hillFormSeed);
    *this = std::move(next);
    return true;
}
void Competition::initializeEvent(uint32_t weatherSeed, uint32_t hillFormSeed) {
    eventEnvironment = environment::resolve(
        visualSelection, environment::hash(weatherSeed ^ uint32_t(eventIndex) * 0x9e3779b9U));
    // Keep the argument for caller compatibility. The original Randomize is
    // in hill setup; subsequent hill-form draws continue that weather RNG.
    (void)hillFormSeed;
    roundIndex = memberGroup = jumpPosition = 0;
    eventFinished = jumpSeedPrepared = false;
    OriginalRandom weatherRandom(weatherSeed);
    sharedWeather.initialize(weatherRandom);
    OriginalRandom formRandom = weatherRandom;
    // Original weather initialization retains angularCap between hills.
    std::vector<int> formOrder(competitors.size());
    std::iota(formOrder.begin(), formOrder.end(), 0);
    if (config.mode == CupMode::Team)
        std::stable_sort(formOrder.begin(), formOrder.end(), [&](int a, int b) {
            const auto &pa = competitors[a].participant, &pb = competitors[b].participant;
            return pa.member != pb.member ? pa.member < pb.member : pa.team < pb.team;
        });
    for (int i : formOrder) {
        auto &entry = competitors[i];
        entry.results = {};
        entry.completed = {};
        entry.eventTotal = 0;
        if (entry.participant.computer) refreshOriginalAiHillForm(entry.skills, formRandom);
    }
    for (auto &team : teamScores) team.eventTotal = 0;
    if (config.mode == CupMode::World) {
        if (!eventIndex) {
            jumpOrder.resize(competitors.size());
            std::iota(jumpOrder.begin(), jumpOrder.end(), 0);
        } else {
            jumpOrder = standings(true);
            std::reverse(jumpOrder.begin(), jumpOrder.end());
        }
    } else {
        teamOrder = standings(true);
        if (eventIndex) std::reverse(teamOrder.begin(), teamOrder.end());
        makeTeamGroupOrder();
    }
}
void Competition::makeTeamGroupOrder() {
    jumpOrder.clear();
    for (int team : teamOrder)
        for (size_t p = 0; p < competitors.size(); ++p)
            if (competitors[p].participant.team == team && competitors[p].participant.member == memberGroup)
                jumpOrder.push_back(int(p));
}
int Competition::currentJumper() const {
    return active() && !eventFinished && jumpPosition >= 0 && jumpPosition < int(jumpOrder.size())
               ? jumpOrder[jumpPosition] : -1;
}
std::vector<int> Competition::standings(bool cup) const {
    std::vector<float> values;
    if (config.mode == CupMode::Team)
        for (const auto &t : teamScores) values.push_back(cup ? float(t.cupPoints) : t.eventTotal);
    else for (const auto &p : competitors) values.push_back(cup ? float(p.cupPoints) : p.eventTotal);
    return originalCompetitionOrder(values);
}
std::vector<int> Competition::ranks(bool cup) const {
    std::vector<float> values;
    if (config.mode == CupMode::Team)
        for (const auto &t : teamScores) values.push_back(cup ? float(t.cupPoints) : t.eventTotal);
    else for (const auto &p : competitors) values.push_back(cup ? float(p.cupPoints) : p.eventTotal);
    return originalCompetitionRanks(values);
}
void Competition::retainJumpState(const OriginalWind &weather, std::array<float, 2> auxiliary) {
    sharedWeather = weather;
    sharedAuxiliary = auxiliary;
}
bool Competition::submitResult(const Result &result, std::string *error) {
    int jumper = currentJumper();
    if (jumper < 0) return fail(error, "No jump is awaiting a result");
    if (!std::isfinite(result.total) || !std::isfinite(result.distance) ||
        !std::isfinite(result.distancePoints) || !std::isfinite(result.stylePoints) ||
        std::abs(result.total) > 100000 || result.distance < 0 || result.distance > 1000 ||
        std::any_of(result.judges.begin(), result.judges.end(), [](double mark) {
            return !std::isfinite(mark) || mark < 0 || mark > 20;
        }))
        return fail(error, "Invalid jump result");
    auto &entry = competitors[jumper];
    if (entry.completed[roundIndex]) return fail(error, "This jump already has a result");
    entry.results[roundIndex] = result;
    entry.results[roundIndex].adjustedSuit |= config.adjustedSuit && !entry.participant.computer;
    entry.completed[roundIndex] = true;
    // Complete Simulation results carry distance/judge details. The backend
    // also accepts numeric-only externally supplied scores for tournament
    // clients; those cannot recover the source's pre-truncation arithmetic.
    const bool detailedScore = result.disqualified || result.beforeTakeoff || result.distancePoints != 0 || result.stylePoints != 0 ||
        std::any_of(result.judges.begin(), result.judges.end(), [](double mark) { return mark != 0; });
    entry.eventTotal = detailedScore
        ? originalCompetitionTotal(entry.eventTotal, hills()[hill()], result)
        : float(entry.eventTotal + float(result.total));
    if (config.mode == CupMode::Team) {
        auto &team = teamScores[entry.participant.team];
        team.eventTotal = detailedScore
            ? originalCompetitionTotal(team.eventTotal, hills()[hill()], result)
            : float(team.eventTotal + float(result.total));
    }
    jumpSeedPrepared = false;
    ++jumpPosition;
    if (jumpPosition < int(jumpOrder.size())) return true;
    jumpPosition = 0;
    if (config.mode == CupMode::Team && ++memberGroup < 4) {
        makeTeamGroupOrder();
        return true;
    }
    memberGroup = 0;
    if (!roundIndex) {
        roundIndex = 1;
        if (config.mode == CupMode::World) {
            jumpOrder = standings(false);
            std::reverse(jumpOrder.begin(), jumpOrder.end());
        } else makeTeamGroupOrder();
        return true;
    }
    scoreEvent();
    if (config.mode == CupMode::Team) makeTeamGroupOrder();
    eventFinished = true;
    return true;
}
void Competition::scoreEvent() {
    auto eventRanks = ranks();
    if (config.mode == CupMode::World) {
        for (size_t i = 0; i < competitors.size(); ++i)
            competitors[i].cupPoints += originalCupAward(config.mode, eventRanks[i]);
    } else {
        for (size_t i = 0; i < teamScores.size(); ++i)
            teamScores[i].cupPoints += originalCupAward(config.mode, eventRanks[i]);
    }
}
bool Competition::advanceEvent(uint32_t weatherSeed, uint32_t hillFormSeed) {
    if (!active() || !eventFinished) return false;
    if (finalEvent()) {
        finished = true;
        return true;
    }
    ++eventIndex;
    initializeEvent(weatherSeed, hillFormSeed);
    return true;
}
} // namespace sjp
