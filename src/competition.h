#pragma once
#include "physics.h"
#include "nationality.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sjp {
enum class CupMode : int { World, Team };
struct CupParticipant {
    std::string name = "PLAYER", country = "NOR";
    bool computer = false;
    int level = 2; // Original Poorest / Poor / Average / Good / Superb / Random.
    int team = -1, member = 0; // Team Cup: four members, numbered 0..3.
};
struct CupTeam { std::string name = "TEAM", country = "NOR"; };
struct CupSetup {
    CupMode mode = CupMode::World;
    std::vector<CupParticipant> participants;
    std::vector<CupTeam> teams;
    std::vector<int> hills; // Normalized to original hill-index order by start().
    bool doubleSeason = false, skipCpu = true;
};
struct CupEntry {
    CupParticipant participant;
    OriginalAiSkills skills;
    std::array<Result, 2> results{};
    std::array<bool, 2> completed{};
    float eventTotal = 0;
    int cupPoints = 0;
};
struct CupTeamStanding { float eventTotal = 0; int cupPoints = 0; };

// Rankings use original stable descending float32 comparisons. Ties share
// competition rank (1,1,3) and therefore receive the same cup award.
std::vector<int> originalCompetitionOrder(const std::vector<float> &values);
std::vector<int> originalCompetitionRanks(const std::vector<float> &values);
int originalCupAward(CupMode mode, int rank);
float originalCompetitionTotal(float previous, const Hill &, const Result &);
bool validateCupSetup(const CupSetup &, std::string *error = nullptr);
void assignCpuIdentity(CupSetup &, size_t participant, uint32_t seed, bool retainCountry = false);
CupSetup defaultCupSetup(uint32_t identitySeed = athleteIdentitySeed());
CupSetup defaultTeamCupSetup(uint32_t identitySeed = athleteIdentitySeed());

class Competition {
  public:
    bool start(const CupSetup &, uint32_t skillsSeed, uint32_t weatherSeed,
               uint32_t hillFormSeed, std::string *error = nullptr,
               environment::Selection environment = {});
    // hillFormSeed is retained for API compatibility; form continues the
    // post-weather RNG, because original hill setup Randomize precedes both.
    bool active() const { return started && !finished; }
    bool eventComplete() const { return eventFinished; }
    bool complete() const { return finished; }
    bool finalEvent() const { return started && eventIndex + 1 == eventCount(); }
    int eventCount() const { return int(config.hills.size()) * (config.doubleSeason ? 2 : 1); }
    int event() const { return eventIndex; }
    int round() const { return roundIndex; }
    int group() const { return memberGroup; }
    int position() const { return jumpPosition; }
    int hill() const { return started ? config.hills[eventIndex % config.hills.size()] : 0; }
    int currentJumper() const;
    const CupSetup &setup() const { return config; }
    const std::vector<CupEntry> &entries() const { return competitors; }
    const std::vector<CupTeamStanding> &teamStandings() const { return teamScores; }
    const std::vector<int> &startOrder() const { return jumpOrder; }
    std::vector<int> standings(bool cup = false) const;
    std::vector<int> ranks(bool cup = false) const;
    const OriginalWind &weather() const { return sharedWeather; }
    const std::array<float, 2> &auxiliary() const { return sharedAuxiliary; }
    void retainJumpState(const OriginalWind &weather, std::array<float, 2> auxiliary);
    // Persist the seed before starting a jump so an interrupted jump can be
    // restarted from precisely the same checkpoint without duplicating results.
    void prepareJump(uint32_t seed) { jumpSeed = seed; jumpSeedPrepared = true; }
    bool hasPreparedJump() const { return jumpSeedPrepared; }
    uint32_t preparedJumpSeed() const { return jumpSeed; }
    const environment::Conditions &visualEnvironment() const { return eventEnvironment; }
    bool submitResult(const Result &, std::string *error = nullptr);
    bool advanceEvent(uint32_t weatherSeed, uint32_t hillFormSeed);

  private:
    friend bool saveCompetition(const std::string &, const Competition &);
    friend bool loadCompetition(const std::string &, Competition &, std::string *);
    CupSetup config;
    std::vector<CupEntry> competitors;
    std::vector<CupTeamStanding> teamScores;
    std::vector<int> jumpOrder, teamOrder;
    OriginalWind sharedWeather;
    std::array<float, 2> sharedAuxiliary{};
    int eventIndex = 0, roundIndex = 0, memberGroup = 0, jumpPosition = 0;
    bool started = false, eventFinished = false, finished = false;
    uint32_t jumpSeed = 0;
    bool jumpSeedPrepared = false;
    environment::Selection visualSelection;
    environment::Conditions eventEnvironment;
    void initializeEvent(uint32_t weatherSeed, uint32_t hillFormSeed);
    void scoreEvent();
    void makeTeamGroupOrder();
};
bool saveCompetition(const std::string &, const Competition &);
bool loadCompetition(const std::string &, Competition &, std::string *error = nullptr);
} // namespace sjp
