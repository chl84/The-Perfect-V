#include "competition.h"
#include "nationality.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void catalog() {
    for (size_t i = 0; i < hills().size(); ++i) {
        require(nationality(hills()[i].country) == &NATIONALITIES[i], "Original hill maps to wrong flag");
        const auto &n = NATIONALITIES[i];
        require(n.flagX >= 0 && n.flagY >= 0 && n.flagX + n.flagWidth <= FLAG_ATLAS_WIDTH &&
                    n.flagY + n.flagHeight <= FLAG_ATLAS_HEIGHT,
                "Flag outside atlas");
        require(nationality(n.code) == &n && nationality(n.iso) == &n, "Country code aliases disagree");
    }
    require(nationality("HOL") == nationality("NLD") && nationality("Holland") == nationality("NED"),
            "Netherlands aliases disagree");
    require(nationality("Korea") == nationality("KOR") && nationality("Czechia") == nationality("CZE"),
            "Historical hill label maps to wrong modern flag");
    require(nationality("England") != nationality("GBR") && nationality("gb-eng") == nationality("ENG") &&
                nationality("GB") == nationality("GBR"),
            "England incorrectly gets the Union Flag");
    require(nationality("GER") == nationality("DEU") && nationality("SUI") == nationality("CHE") &&
                nationality("SVN") == nationality("SLO"),
            "Olympic and ISO codes disagree");
    require(!nationality("XYZ") && !nationality(""), "Unknown country invents a real flag");
    const auto *swiss = nationality("SUI");
    require(swiss->flagWidth == swiss->flagHeight, "Swiss national flag must remain square");
    // Independent spot checks of flag geometry/color, including easy-to-confuse flags.
    SDL_Surface *bmp = SDL_LoadBMP(SJP_TEST_ASSET_DIR "/flags/atlas.bmp");
    require(bmp != nullptr, "Bundled flag atlas missing");
    SDL_Surface *rgb = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(bmp);
    require(rgb && rgb->w == FLAG_ATLAS_WIDTH && rgb->h == FLAG_ATLAS_HEIGHT, "Atlas dimensions wrong");
    auto sample = [&](const char *code, float u, float v) {
        const auto *n = nationality(code);
        const auto *data = static_cast<const uint8_t *>(rgb->pixels) +
                           (n->flagY + int(v * n->flagHeight)) * rgb->pitch +
                           (n->flagX + int(u * n->flagWidth)) * 3;
        return std::array<int, 3>{data[0], data[1], data[2]};
    };
    auto white = [](auto p) { return p[0] > 230 && p[1] > 230 && p[2] > 230; };
    auto red = [](auto p) { return p[0] > 150 && p[1] < 95 && p[2] < 110; };
    auto blue = [](auto p) { return p[2] > 80 && p[0] < 100; };
    require(white(sample("SUI", .5f, .5f)) && red(sample("SUI", .1f, .1f)), "Swiss cross/colors wrong");
    require(red(sample("JPN", .5f, .5f)) && white(sample("JPN", .1f, .1f)), "Japan disk/colors wrong");
    require(red(sample("ENG", .5f, .5f)) && white(sample("ENG", .1f, .1f)), "England cross/colors wrong");
    require(blue(sample("NOR", .32f, .5f)) && red(sample("NOR", .1f, .1f)), "Norwegian cross/colors wrong");
    require(white(sample("POL", .5f, .2f)) && red(sample("POL", .5f, .8f)), "Polish bands swapped");
    require(red(sample("AUT", .5f, .15f)) && white(sample("AUT", .5f, .5f)), "Austria bands wrong");
    SDL_DestroySurface(rgb);
}
void identities() {
    for (const auto &n : NATIONALITIES) {
        std::set<std::string> variation;
        for (uint32_t seed = 0; seed < 200; ++seed) {
            const auto p = randomAthleteIdentity(seed, n.code);
            require(p.country == n.code && p.name.find(' ') != std::string::npos && p.name.size() <= 64,
                    "CPU identity has wrong country or incomplete name");
            require(p.name == randomAthleteIdentity(seed, n.code).name, "Seeded identity unstable");
            require(std::all_of(p.name.begin(), p.name.end(), [](unsigned char c) { return c < 128; }),
                    "Generated name unsupported by existing font/replay encoding");
            variation.insert(p.name);
        }
        require(variation.size() >= 25, "Country name pool lacks variation");
    }
    std::set<std::string> nations;
    for (uint32_t seed = 0; seed < 1000; ++seed)
        nations.insert(randomAthleteIdentity(seed).country);
    require(nations.size() == NATIONALITIES.size(), "Random nationality never reaches some countries");
    auto setup = defaultCupSetup(123);
    const auto player = setup.participants.front();
    std::set<std::string> names;
    for (const auto &p : setup.participants)
        require(names.insert(p.name).second, "Duplicate CPU name in roster");
    setup.participants.resize(16);
    for (size_t i = 1; i < setup.participants.size(); ++i) {
        setup.participants[i].computer = true;
        setup.participants[i].country = "NOR";
        assignCpuIdentity(setup, i, 42, true);
    }
    names.clear();
    for (const auto &p : setup.participants)
        require(names.insert(p.name).second, "Duplicate names with shared seed/country");
    require(setup.participants[0].name == player.name && setup.participants[0].country == player.country,
            "CPU generator changed human participant");
    std::set<std::string> randomTeamCountries;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        auto randomTeam = defaultTeamCupSetup(seed);
        std::set<std::string> distinct;
        for (const auto &t : randomTeam.teams)
            require(distinct.insert(t.country).second, "Duplicate default team country");
        for (size_t t = 1; t < randomTeam.teams.size(); ++t)
            randomTeamCountries.insert(randomTeam.teams[t].country);
    }
    require(randomTeamCountries.size() > 20, "CPU teams do not have random nationalities");
    auto team = defaultTeamCupSetup(789);
    for (const auto &p : team.participants)
        require(p.country == team.teams[size_t(p.team)].country,
                "CPU competes for different country than team");
    team.teams[1].country = "POL";
    for (size_t i = 4; i < 8; ++i) {
        assignCpuIdentity(team, i, uint32_t(i));
        require(team.participants[i].country == "POL" && team.participants[i].team == 1 &&
                    team.participants[i].member == int(i - 4),
                "Identity change damaged team/member assignment");
    }
}
void persistenceAndPhysics() {
    auto setup = defaultCupSetup(2), other = defaultCupSetup(999);
    setup.hills = other.hills = {0, 1};
    Competition cup, comparison, restored;
    require(cup.start(setup, 101, 202, 303) && comparison.start(other, 101, 202, 303),
            "Generated cup rejected");
    for (size_t i = 0; i < cup.entries().size(); ++i)
        require(cup.entries()[i].skills.values == comparison.entries()[i].skills.values &&
                    cup.entries()[i].skills.level == comparison.entries()[i].skills.level,
                "Cosmetic identity randomness changed original CPU skills/form");
    require(cup.weather().x == comparison.weather().x && cup.weather().y == comparison.weather().y &&
                cup.weather().angle == comparison.weather().angle,
            "Identity generation changed original wind");
    cup.prepareJump(456);
    auto directory = std::filesystem::temp_directory_path() /
                     ("perfect-v-identity-tests-" + std::to_string(athleteIdentitySeed()));
    std::filesystem::create_directories(directory);
    auto path = directory / "cup.sjc";
    require(saveCompetition(path.string(), cup) && loadCompetition(path.string(), restored),
            "CPU identity save/load failed");
    for (size_t i = 0; i < cup.entries().size(); ++i)
        require(cup.entries()[i].participant.name == restored.entries()[i].participant.name &&
                    cup.entries()[i].participant.country == restored.entries()[i].participant.country,
                "Resume rerolled CPU identity");
    Result result;
    result.total = 150;
    while (!cup.eventComplete())
        require(cup.submitResult(result), "Synthetic cup result failed");
    require(cup.advanceEvent(505, 606), "Next event failed");
    for (size_t i = 0; i < cup.entries().size(); ++i)
        require(cup.entries()[i].participant.name == setup.participants[i].name &&
                    cup.entries()[i].participant.country == setup.participants[i].country,
                "CPU identity changed between events");
    std::filesystem::remove_all(directory);
}
} // namespace
int main() {
    try {
        catalog();
        identities();
        persistenceAndPhysics();
        std::cout << "PASS: flag mappings/colors, country name pools, unique rosters, stable resume and "
                     "unchanged skills/wind\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
