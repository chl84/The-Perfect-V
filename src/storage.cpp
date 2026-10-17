#include "storage.h"
#include "platform_file.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace sjp {
namespace {
// A gameplay revision separates records and replays made with different physics/pose models.
constexpr const char *PROFILE_SET = "DSJ2_ORIGINAL_GAMEPLAY_1";
int hillById(const std::string &id) {
    const auto &catalog = hills();
    auto it = std::find_if(catalog.begin(), catalog.end(), [&](const Hill &hill) { return hill.id == id; });
    return it == catalog.end() ? -1 : int(it - catalog.begin());
}
bool preferences(std::istream &in, Records &records) {
    int sound;
    if (!(in >> records.camera >> sound >> records.sensitivity) || records.camera < 0 || records.camera > 2 ||
        (sound != 0 && sound != 1) || !std::isfinite(records.sensitivity))
        return false;
    records.sound = sound != 0;
    records.sensitivity = clamp(records.sensitivity, .3, 3.0);
    return true;
}
bool replaceFile(const std::string &path, std::ofstream &out) {
    out.close();
    if (!out)
        return false;
    return replaceSavedFile(std::filesystem::u8path(path + ".tmp"), std::filesystem::u8path(path));
}
} // namespace
bool importLegacySaves(const std::string &source, const std::string &destination, std::string *error) {
    namespace fs = std::filesystem;
    try {
        const fs::path oldDirectory(source), newDirectory(destination);
        const auto marker = newDirectory / ".legacy-saves-imported";
        if (!fs::exists(marker)) {
            if (fs::create_directories(newDirectory))
                fs::permissions(newDirectory, fs::perms::owner_all);
            if (fs::exists(oldDirectory)) {
                for (const auto &entry : fs::recursive_directory_iterator(oldDirectory)) {
                    if (!fs::is_regular_file(entry.symlink_status()) || entry.path().extension() == ".tmp" ||
                        entry.path().filename() == marker.filename() ||
                        entry.path().filename() == ".skijumppro-imported")
                        continue;
                    const auto target = newDirectory / entry.path().lexically_relative(oldDirectory);
                    if (fs::exists(target))
                        continue;
                    fs::create_directories(target.parent_path());
                    // Finish each copy before making it visible. An interrupted import
                    // can retry without treating a partial copy as an existing save.
                    const auto temporary = fs::path(target.string() + ".importing.tmp");
                    fs::copy_file(entry.path(), temporary, fs::copy_options::overwrite_existing);
                    if (fs::exists(target))
                        fs::remove(temporary);
                    else
                        fs::rename(temporary, target);
                }
            }
            std::ofstream out(marker.string() + ".tmp");
            out << "Imported missing files from the previous save directory; originals retained.\n";
            if (!replaceFile(marker.string(), out))
                throw std::runtime_error("Could not mark the save-directory import as complete.");
        }
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = e.what();
        return false;
    }
}
bool updateHillRecord(Records &records, int hill, const Result &result, bool human) {
    if (!human || result.adjustedSuit || hill < 0 || size_t(hill) >= records.best.size() || result.crashed || result.disqualified ||
        result.beforeTakeoff || !std::isfinite(result.distance) || result.distance <= 0 ||
        result.distance > 1000 || result.distance <= records.best[size_t(hill)])
        return false;
    records.best[size_t(hill)] = result.distance;
    return true;
}
bool saveRecords(const std::string &path, const Records &r) {
    if (!environment::valid(r.environment) || !validJumperAppearance(r.jumper) ||
        !validMsaaSamples(r.graphics.msaaSamples) || !validResolution(r.graphics.resolution))
        return false;
    for (size_t i = 0; i < r.best.size(); i++)
        if (hills()[i].id.empty() || !std::isfinite(r.best[i]) || r.best[i] < 0 || r.best[i] > 1000)
            return false;
    std::ofstream out(path + ".tmp");
    if (!out)
        return false;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "SJP_RECORDS_3\n" << PROFILE_SET << '\n' << r.best.size() << '\n';
    for (size_t i = 0; i < r.best.size(); i++)
        out << std::quoted(hills()[i].id) << ' ' << r.best[i] << '\n';
    out << r.camera << ' ' << r.sound << ' ' << r.sensitivity << '\n';
    out << "ENV 1 " << int(r.environment.time) << ' ' << int(r.environment.weather) << '\n';
    out << "JUMPER 1 " << int(r.jumper) << '\n';
    out << "GRAPHICS 5 " << r.graphics.fullscreen << ' ' << r.graphics.msaaSamples << ' '
        << r.graphics.vsync << ' ' << r.graphics.resolution.width << ' ' << r.graphics.resolution.height << '\n';
    out << "MUSIC 1 " << r.music << '\n';
    out << "CHEATS 1 " << r.adjustedSuit << '\n';
    return replaceFile(path, out);
}
Records loadRecords(const std::string &path) {
    Records r;
    std::ifstream in(path);
    std::string magic;
    if (!(in >> magic))
        return r;
    if (magic == "SJP_RECORDS_1") {
        // The old six synthetic hills have no matching original profile. Keep preferences only.
        for (int i = 0; i < 6; i++) {
            double oldBest;
            if (!(in >> oldBest) || !std::isfinite(oldBest) || oldBest < 0 || oldBest > 1000)
                return Records{};
        }
        return preferences(in, r) ? r : Records{};
    }
    std::string profileSet;
    size_t count;
    bool oldPhysics = magic == "SJP_RECORDS_2";
    if ((magic != "SJP_RECORDS_3" && !oldPhysics) || !(in >> profileSet >> count) ||
        profileSet != (oldPhysics ? "DSJ2_ORIGINAL_PROFILES_1" : PROFILE_SET) || count != hills().size())
        return r;
    std::array<bool, HILL_COUNT> seen{};
    for (size_t i = 0; i < count; i++) {
        std::string id;
        double best;
        if (!(in >> std::quoted(id) >> best) || !std::isfinite(best) || best < 0 || best > 1000)
            return Records{};
        int index = hillById(id);
        if (index < 0 || seen[index])
            return Records{};
        seen[index] = true;
        r.best[index] = best;
    }
    if (!preferences(in, r))
        return {};
    if (oldPhysics)
        r.best.fill(0);
    std::string tag;
    while (in >> tag) {
        int version;
        if (tag == "ENV") {
            int time, weather;
            if (!(in >> version >> time >> weather))
                break;
            const environment::Selection selection{environment::TimeOfDay(time), environment::Weather(weather)};
            if (version == 1 && environment::valid(selection))
                r.environment = selection;
        } else if (tag == "GRAPHICS") {
            int fullscreen, samples;
            if (!(in >> version >> fullscreen >> samples))
                break;
            if (version == 1) {
                // Retain display preferences from the former manual frame-rate UI.
                // Older formats default to VSync on.
                int automatic, vsync, limit;
                if (!(in >> automatic >> vsync >> limit))
                    break;
                if (automatic < 0 || automatic > 1 || vsync < 0 || vsync > 1 ||
                    (limit != 0 && limit != 30 && limit != 60 && limit != 120))
                    continue;
            }
            if (version == 1 || version == 2) {
                if (samples < 0 || samples > 1)
                    continue;
                samples = samples ? 4 : 0;
            }
            int vsync = 1;
            if ((version == 4 || version == 5) && !(in >> vsync))
                break;
            Resolution resolution;
            if (version == 5 && !(in >> resolution.width >> resolution.height))
                break;
            if (version >= 1 && version <= 5 && fullscreen >= 0 && fullscreen <= 1 &&
                validMsaaSamples(samples) && vsync >= 0 && vsync <= 1 && validResolution(resolution))
                r.graphics = {fullscreen == 1, samples, vsync == 1, resolution};
        } else if (tag == "CHEATS") {
            int enabled;
            if (!(in >> version >> enabled)) break;
            if (version == 1 && (enabled == 0 || enabled == 1)) r.adjustedSuit = enabled != 0;
        } else if (tag == "MUSIC") {
            int enabled;
            if (!(in >> version >> enabled))
                break;
            if (version == 1 && (enabled == 0 || enabled == 1))
                r.music = enabled != 0;
        } else if (tag == "JUMPER") {
            int value;
            if (!(in >> version >> value))
                break;
            if (version == 1 && validJumperAppearance(JumperAppearance(value)))
                r.jumper = JumperAppearance(value);
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }
    return r;
}
bool saveReplay(const std::string &path, const Replay &r) {
    if (r.result.adjustedSuit && !r.originalRpl.empty()) return false;
    if (!validJumperAppearance(r.jumper) || (r.hasEnvironment && !environment::valid(r.environment)))
        return false;
    if ((r.originalRpl.size() != 0 && r.originalRpl.size() != 1222) || r.frames.size() < 2 ||
        r.frames.size() > 20000 || r.hill < 0 || size_t(r.hill) >= hills().size() ||
        hills()[r.hill].id.empty())
        return false;
    std::ofstream out(path + ".tmp");
    if (!out)
        return false;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "SJP_REPLAY_5\n"
        << PROFILE_SET << '\n'
        << std::quoted(hills()[r.hill].id) << ' ' << r.wind << ' ' << r.frames.size() << '\n';
    out << r.result.distance << ' ' << r.result.total << ' ' << r.result.crashed << ' '
        << int(r.result.landing) << ' ' << r.result.telemarkWidth << ' ' << r.result.crashKind << '\n';
    for (const auto &f : r.frames) {
        out << f.x << ' ' << f.y << ' ' << f.vx << ' ' << f.vy << ' ' << f.pitch << ' ' << f.elapsed << ' '
            << f.phase << ' ' << f.landing << ' ' << f.crashed << ' ' << f.telemarkWidth << ' ' << f.windSpeed
            << ' ' << f.windAngle;
        for (float component : f.pose)
            out << ' ' << component;
        out << ' ' << f.leadingLeg;
        out << '\n';
    }
    out << "RPL " << r.originalRpl.size();
    for (uint8_t byte : r.originalRpl)
        out << ' ' << unsigned(byte);
    out << '\n';
    if (r.hasEnvironment)
        out << "ENV 1 " << int(r.environment.time) << ' ' << int(r.environment.weather) << ' '
            << r.environment.seed << '\n';
    out << "JUMPER 1 " << int(r.jumper) << '\n';
    if (r.result.adjustedSuit) out << "CHEATS 1 1\n";
    return replaceFile(path, out);
}
bool loadReplay(const std::string &path, Replay &output) {
    std::ifstream in(path);
    std::string magic, profileSet, id;
    Replay r;
    size_t n;
    int crashed, land;
    // Older replays describe synthetic terrain or the previous flight/pose model.
    if (!(in >> magic >> profileSet) || (magic != "SJP_REPLAY_4" && magic != "SJP_REPLAY_5") ||
        profileSet != PROFILE_SET || !(in >> std::quoted(id) >> r.wind >> n) || (r.hill = hillById(id)) < 0 ||
        n < 2 || n > 20000 || !std::isfinite(r.wind) || std::abs(r.wind) > 5)
        return false;
    if (!(in >> r.result.distance >> r.result.total >> crashed >> land >> r.result.telemarkWidth >>
          r.result.crashKind) ||
        !std::isfinite(r.result.distance) || !std::isfinite(r.result.total) || r.result.distance < 0 ||
        r.result.distance > 1000 || crashed < 0 || crashed > 1 || land < 0 || land > 2 ||
        !std::isfinite(r.result.telemarkWidth) || r.result.telemarkWidth < 0 ||
        r.result.telemarkWidth > .62 || r.result.crashKind < 0 || r.result.crashKind > 5)
        return false;
    r.result.crashed = crashed;
    r.result.landing = Landing(land);
    r.frames.resize(n);
    float last = -1;
    for (auto &f : r.frames) {
        if (!(in >> f.x >> f.y >> f.vx >> f.vy >> f.pitch >> f.elapsed >> f.phase >> f.landing >> f.crashed >>
              f.telemarkWidth >> f.windSpeed >> f.windAngle))
            return false;
        if (!std::isfinite(f.x) || !std::isfinite(f.y) || !std::isfinite(f.vx) || !std::isfinite(f.vy) ||
            !std::isfinite(f.pitch) || !std::isfinite(f.elapsed) || std::abs(f.x) > 2000 ||
            std::abs(f.y) > 2000 || std::abs(f.vx) > 500 || std::abs(f.vy) > 500 ||
            std::abs(f.pitch) > 1.01 || f.elapsed < last || f.elapsed > 300 || f.phase < 0 || f.phase > 5 ||
            f.landing < 0 || f.landing > 2 || f.crashed < 0 || f.crashed > 1)
            return false;
        if (!std::isfinite(f.telemarkWidth) || f.telemarkWidth < 0 || f.telemarkWidth > .62)
            return false;
        if (!std::isfinite(f.windSpeed) || !std::isfinite(f.windAngle) || f.windSpeed < 0 ||
            f.windSpeed > 20 || std::abs(f.windAngle) > 100)
            return false;
        for (float &component : f.pose)
            if (!(in >> component) || !std::isfinite(component) || std::abs(component) > 100)
                return false;
        if (magic == "SJP_REPLAY_5") {
            if (!(in >> f.leadingLeg) || f.leadingLeg < 0 || f.leadingLeg > 1)
                return false;
        } else if (f.landing && f.pose[11] != f.pose[2]) {
            f.leadingLeg = f.pose[11] > f.pose[2] ? 1 : 0;
        }
        last = f.elapsed;
    }
    if (magic == "SJP_REPLAY_5") {
        std::string tag;
        size_t count;
        if (!(in >> tag >> count) || tag != "RPL" || (count != 0 && count != 1222))
            return false;
        r.originalRpl.resize(count);
        for (auto &byte : r.originalRpl) {
            unsigned value;
            if (!(in >> value) || value > 255)
                return false;
            byte = uint8_t(value);
        }
    }
    bool hasJumper = false, hasCheats = false;
    while (true) {
        in >> std::ws;
        if (in.eof())
            break;
        std::string tag;
        int version;
        if (!(in >> tag >> version) || version != 1)
            return false;
        if (tag == "ENV") {
            int time, weather;
            if (r.hasEnvironment || !(in >> time >> weather >> r.environment.seed))
                return false;
            r.environment.time = environment::TimeOfDay(time);
            r.environment.weather = environment::Weather(weather);
            if (!environment::valid(r.environment))
                return false;
            r.hasEnvironment = true;
        } else if (tag == "CHEATS") {
            int enabled;
            if (hasCheats || !(in >> enabled) || (enabled != 0 && enabled != 1)) return false;
            r.result.adjustedSuit = enabled != 0;
            hasCheats = true;
        } else if (tag == "JUMPER") {
            int value;
            if (hasJumper || !(in >> value) || !validJumperAppearance(JumperAppearance(value)))
                return false;
            r.jumper = JumperAppearance(value);
            hasJumper = true;
        } else
            return false;
    }
    if (r.result.adjustedSuit && !r.originalRpl.empty()) return false;
    output = std::move(r);
    return true;
}
} // namespace sjp
