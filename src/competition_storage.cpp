#include "competition.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>

namespace sjp {
namespace {
uint64_t checksum(const std::string &payload) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char b : payload) { hash ^= b; hash *= 1099511628211ull; }
    return hash;
}
template <typename T> void writeArray(std::ostream &out, const T &array) {
    for (auto value : array) out << value << ' ';
    out << '\n';
}
template <typename T> bool readArray(std::istream &in, T &array) {
    for (auto &value : array) if (!(in >> value)) return false;
    return true;
}
void writeResult(std::ostream &out, const Result &r) {
    out << r.distance << ' ' << r.distancePoints << ' ' << r.stylePoints << ' ' << r.total << ' '
        << r.takeoff << ' ' << r.crashed << ' ' << r.disqualified << ' ' << r.beforeTakeoff << ' ' << int(r.landing) << ' '
        << r.telemarkWidth << ' ' << r.crashKind << ' ' << std::quoted(r.feedback) << '\n';
    writeArray(out, r.judges);
}
bool readResult(std::istream &in, Result &r, int version) {
    int landing;
    if (!(in >> r.distance >> r.distancePoints >> r.stylePoints >> r.total >> r.takeoff >> r.crashed
             >> r.disqualified)) return false;
    if (version >= 2 && !(in >> r.beforeTakeoff)) return false;
    if (!(in >> landing >> r.telemarkWidth >> r.crashKind >> std::quoted(r.feedback)) ||
        !readArray(in, r.judges)) return false;
    if (landing < 0 || landing > 2 || r.crashKind < 0 || r.crashKind > 5 || r.feedback.size() > 4096)
        return false;
    r.landing = Landing(landing);
    for (double value : {r.distance, r.distancePoints, r.stylePoints, r.total, r.takeoff, r.telemarkWidth})
        if (!std::isfinite(value) || std::abs(value) > 100000) return false;
    for (double mark : r.judges) if (!std::isfinite(mark) || mark < 0 || mark > 20) return false;
    return r.distance >= 0 && r.distance <= 1000;
}
bool readVector(std::istream &in, std::vector<int> &values, size_t limit) {
    size_t count;
    if (!(in >> count) || count > limit) return false;
    values.resize(count);
    return readArray(in, values);
}
void writeVector(std::ostream &out, const std::vector<int> &values) {
    out << values.size() << ' '; writeArray(out, values);
}
bool permutation(const std::vector<int> &values, int size) {
    if (int(values.size()) != size) return false;
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < size; ++i) if (sorted[i] != i) return false;
    return true;
}
}
bool saveCompetition(const std::string &path, const Competition &cup) {
    if (!cup.started || !validateCupSetup(cup.config)) return false;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << int(cup.config.mode) << ' ' << cup.config.doubleSeason << ' ' << cup.config.skipCpu << '\n';
    writeVector(out, cup.config.hills);
    out << cup.config.teams.size() << '\n';
    for (const auto &team : cup.config.teams)
        out << std::quoted(team.name) << ' ' << std::quoted(team.country) << '\n';
    out << cup.competitors.size() << '\n';
    for (const auto &entry : cup.competitors) {
        const auto &p = entry.participant;
        out << std::quoted(p.name) << ' ' << std::quoted(p.country) << ' ' << p.computer << ' '
            << p.level << ' ' << p.team << ' ' << p.member << '\n';
        out << entry.skills.level << ' '; writeArray(out, entry.skills.values);
        out << entry.eventTotal << ' ' << entry.cupPoints << ' ' << entry.completed[0] << ' '
            << entry.completed[1] << '\n';
        for (const auto &result : entry.results) writeResult(out, result);
    }
    out << cup.teamScores.size() << '\n';
    for (const auto &team : cup.teamScores) out << team.eventTotal << ' ' << team.cupPoints << '\n';
    writeVector(out, cup.jumpOrder); writeVector(out, cup.teamOrder);
    out << cup.eventIndex << ' ' << cup.roundIndex << ' ' << cup.memberGroup << ' ' << cup.jumpPosition
        << ' ' << cup.eventFinished << ' ' << cup.finished << ' ' << cup.jumpSeed << ' '
        << cup.jumpSeedPrepared << '\n';
    const auto &w = cup.sharedWeather;
    out << w.x << ' ' << w.y << ' ' << w.variationStep << ' ' << w.speed << ' ' << w.angle << ' '
        << w.displayAngle << ' ' << w.angularStep << ' ' << w.angularCap << ' ' << w.frameCounter
        << ' ' << unsigned(w.weatherIntensity) << '\n';
    writeArray(out, cup.sharedAuxiliary);
    const std::string payload = out.str(), temporary = path + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << "SJP_COMPETITION_2\n" << checksum(payload) << '\n' << payload;
    file.flush();
    if (!file) { file.close(); std::error_code ec; std::filesystem::remove(temporary, ec); return false; }
    file.close();
    if (!file) return false;
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) { std::filesystem::remove(temporary, ec); return false; }
    return true;
}
bool loadCompetition(const std::string &path, Competition &cup, std::string *error) {
    auto fail = [&] { if (error) *error = "The saved cup is incomplete, corrupt or incompatible"; return false; };
    std::error_code ec;
    auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > 2 * 1024 * 1024) return fail();
    std::ifstream file(path, std::ios::binary);
    std::string magic, hashLine;
    if (!std::getline(file, magic) || (magic != "SJP_COMPETITION_1" && magic != "SJP_COMPETITION_2") ||
        !std::getline(file, hashLine)) return fail();
    const int version = magic == "SJP_COMPETITION_2" ? 2 : 1;
    uint64_t hash;
    std::istringstream hashInput(hashLine);
    if (!(hashInput >> hash) || (hashInput >> std::ws && !hashInput.eof())) return fail();
    std::string payload{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (checksum(payload) != hash) return fail();
    std::istringstream in(payload);
    in.imbue(std::locale::classic());
    Competition next;
    int mode;
    if (!(in >> mode >> next.config.doubleSeason >> next.config.skipCpu) || mode < 0 || mode > 1 ||
        !readVector(in, next.config.hills, HILL_COUNT)) return fail();
    next.config.mode = CupMode(mode);
    size_t count;
    if (!(in >> count) || count > 4) return fail();
    next.config.teams.resize(count);
    for (auto &team : next.config.teams)
        if (!(in >> std::quoted(team.name) >> std::quoted(team.country))) return fail();
    if (!(in >> count) || !count || count > 16) return fail();
    next.competitors.resize(count);
    for (auto &entry : next.competitors) {
        auto &p = entry.participant;
        if (!(in >> std::quoted(p.name) >> std::quoted(p.country) >> p.computer >> p.level >> p.team >> p.member
                 >> entry.skills.level) || !readArray(in, entry.skills.values) ||
            !(in >> entry.eventTotal >> entry.cupPoints >> entry.completed[0] >> entry.completed[1])) return fail();
        if (entry.skills.level < 0 || entry.skills.level > 5 || !std::isfinite(entry.eventTotal) ||
            std::abs(entry.eventTotal) > 200000 || entry.cupPoints < 0 || entry.cupPoints > 6400) return fail();
        for (float value : entry.skills.values) if (!std::isfinite(value) || std::abs(value) > 1000) return fail();
        for (auto &result : entry.results) if (!readResult(in, result, version)) return fail();
        next.config.participants.push_back(p);
    }
    if (!validateCupSetup(next.config) || !std::is_sorted(next.config.hills.begin(), next.config.hills.end())) return fail();
    if (!(in >> count) || count != next.config.teams.size()) return fail();
    next.teamScores.resize(count);
    for (auto &team : next.teamScores)
        if (!(in >> team.eventTotal >> team.cupPoints) || !std::isfinite(team.eventTotal) ||
            std::abs(team.eventTotal) > 800000 || team.cupPoints < 0 || team.cupPoints > 12800) return fail();
    if (!readVector(in, next.jumpOrder, 16) || !readVector(in, next.teamOrder, 4) ||
        !(in >> next.eventIndex >> next.roundIndex >> next.memberGroup >> next.jumpPosition >> next.eventFinished
             >> next.finished >> next.jumpSeed >> next.jumpSeedPrepared)) return fail();
    next.started = true;
    if (next.eventIndex < 0 || next.eventIndex >= next.eventCount() || next.roundIndex < 0 || next.roundIndex > 1 ||
        next.memberGroup < 0 || next.memberGroup > 3 || next.jumpPosition < 0 ||
        next.jumpPosition >= int(next.jumpOrder.size()) ||
        (next.finished && (!next.finalEvent() || !next.eventFinished)) ||
        (next.eventFinished && (next.roundIndex != 1 || next.jumpSeedPrepared))) return fail();
    if (next.config.mode == CupMode::World) {
        if (!permutation(next.jumpOrder, int(next.competitors.size())) || !next.teamOrder.empty() || next.memberGroup)
            return fail();
    } else {
        if (!permutation(next.teamOrder, int(next.teamScores.size()))) return fail();
        auto savedOrder = next.jumpOrder;
        next.makeTeamGroupOrder();
        if (savedOrder != next.jumpOrder) return fail();
    }
    // Completion flags must match the cursor. This rejects files that would
    // replay an already counted jump or silently skip one after loading.
    for (size_t p = 0; p < next.competitors.size(); ++p) {
        for (int round = 0; round < 2; ++round) {
            bool expected = next.eventFinished || round < next.roundIndex;
            if (!expected && round == next.roundIndex) {
                int member = next.competitors[p].participant.member;
                if (next.config.mode == CupMode::Team && member < next.memberGroup) expected = true;
                auto it = std::find(next.jumpOrder.begin(), next.jumpOrder.end(), int(p));
                if (it != next.jumpOrder.end() && int(it - next.jumpOrder.begin()) < next.jumpPosition) expected = true;
            }
            if (next.competitors[p].completed[round] != expected) return fail();
        }
    }
    auto &w = next.sharedWeather;
    unsigned weatherIntensity;
    if (!(in >> w.x >> w.y >> w.variationStep >> w.speed >> w.angle >> w.displayAngle >> w.angularStep
             >> w.angularCap >> w.frameCounter >> weatherIntensity) || weatherIntensity > 255 ||
        !readArray(in, next.sharedAuxiliary)) return fail();
    w.weatherIntensity = uint8_t(weatherIntensity);
    for (float value : {w.x, w.y, w.variationStep, w.speed, w.angle, w.displayAngle, w.angularStep,
                        w.angularCap, next.sharedAuxiliary[0], next.sharedAuxiliary[1]})
        if (!std::isfinite(value) || std::abs(value) > 100000) return fail();
    if (w.speed < 0 || w.variationStep < 0 || w.angularCap < 0 || w.angularCap > 1) return fail();
    in >> std::ws;
    if (!in.eof()) return fail();
    cup = std::move(next);
    return true;
}
} // namespace sjp
