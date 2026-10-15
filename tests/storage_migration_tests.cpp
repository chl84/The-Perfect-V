#include "storage.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string contents(const std::filesystem::path &path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream(path) << text;
}
} // namespace
int main() {
    auto directory =
        std::filesystem::temp_directory_path() /
        ("sjp-migration-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(directory);
        const auto oldPath = directory / "records.dat";
        const auto newPath = directory / "records-dsj2.dat";
        const std::string legacy = "SJP_RECORDS_1\n65 95 125 155 195 255\n2 0 1.6\n";
        write(oldPath, legacy);
        Records migrated = loadRecords(oldPath.string());
        require(std::all_of(migrated.best.begin(), migrated.best.end(), [](double d) { return d == 0; }),
                "Synthetic-hill records must not migrate onto original profiles");
        require(migrated.camera == 2 && !migrated.sound && std::abs(migrated.sensitivity - 1.6) < 1e-8,
                "Legacy preferences should survive the transition");
        require(saveRecords(newPath.string(), migrated), "Write original-profile records separately");
        require(contents(oldPath) == legacy, "Legacy records must remain intact");
        for (size_t i = 0; i < migrated.best.size(); i++)
            migrated.best[i] = 100 + double(i);
        require(saveRecords(newPath.string(), migrated), "Save all original-profile records");
        auto restored = loadRecords(newPath.string());
        require(restored.best == migrated.best, "All 32 records should persist");

        // The first original-profile release used different flight physics.
        std::ostringstream previousPhysics;
        previousPhysics << "SJP_RECORDS_2\nDSJ2_ORIGINAL_PROFILES_1\n32\n";
        for (size_t i = 0; i < hills().size(); ++i)
            previousPhysics << std::quoted(hills()[i].id) << ' ' << 100 + i << '\n';
        previousPhysics << "1 0 1.7\n";
        const auto previousPath = directory / "records-original.dat";
        write(previousPath, previousPhysics.str());
        const auto previousPreferences = loadRecords(previousPath.string());
        require(std::all_of(previousPreferences.best.begin(), previousPreferences.best.end(),
                            [](double d) { return d == 0; }),
                "Previous flight-model records must not migrate into original gameplay");
        require(previousPreferences.camera == 1 && !previousPreferences.sound &&
                    std::abs(previousPreferences.sensitivity - 1.7) < 1e-8,
                "Original-profile preferences should survive the physics transition");
        require(contents(previousPath) == previousPhysics.str(), "Previous records must remain intact");

        // Records are associated with stable IDs even if a file is reordered.
        std::istringstream input(contents(newPath));
        std::string magic, profiles, count, line, preferences;
        std::getline(input, magic);
        std::getline(input, profiles);
        std::getline(input, count);
        std::vector<std::string> rows;
        for (size_t i = 0; i < hills().size(); i++) {
            std::getline(input, line);
            rows.push_back(line);
        }
        std::getline(input, preferences);
        std::ostringstream reordered;
        reordered << magic << '\n' << profiles << '\n' << count << '\n';
        for (auto row = rows.rbegin(); row != rows.rend(); ++row)
            reordered << *row << '\n';
        reordered << preferences << '\n';
        write(newPath, reordered.str());
        require(loadRecords(newPath.string()).best == migrated.best,
                "File row order must not change hill attribution");
        rows.back() = rows.front();
        std::ostringstream duplicate;
        duplicate << magic << '\n' << profiles << '\n' << count << '\n';
        for (const auto &row : rows)
            duplicate << row << '\n';
        duplicate << preferences << '\n';
        write(newPath, duplicate.str());
        auto invalidRecords = loadRecords(newPath.string());
        require(std::all_of(invalidRecords.best.begin(), invalidRecords.best.end(),
                            [](double d) { return d == 0; }),
                "Duplicate IDs should reject a corrupted catalogue");

        require(loadRecords(oldPath.string()).jumper == JumperAppearance::Pro,
                "Old preferences must default to the original Pro jumper");
        Records appearanceRecords = migrated;
        appearanceRecords.jumper = JumperAppearance::President;
        appearanceRecords.environment = {environment::TimeOfDay::Night, environment::Weather::Fog};
        require(saveRecords(newPath.string(), appearanceRecords), "Save President appearance");
        const auto appearanceFile = contents(newPath);
        const auto appearanceRestored = loadRecords(newPath.string());
        require(appearanceRestored.jumper == JumperAppearance::President &&
                    appearanceRestored.best == migrated.best &&
                    appearanceRestored.environment.time == environment::TimeOfDay::Night &&
                    appearanceRestored.environment.weather == environment::Weather::Fog,
                "Appearance persistence changed records or environment preferences");
        const auto withoutAppearance = appearanceFile.substr(0, appearanceFile.find("JUMPER "));
        write(newPath, withoutAppearance);
        require(loadRecords(newPath.string()).jumper == JumperAppearance::Pro,
                "Existing current-format saves must remain compatible");
        write(newPath, withoutAppearance + "JUMPER 1 99\n");
        require(loadRecords(newPath.string()).jumper == JumperAppearance::Pro &&
                    loadRecords(newPath.string()).best == migrated.best,
                "An unknown appearance must fall back without losing hill records");

        Replay replay;
        replay.hill = int(hills().size() - 1);
        replay.wind = 2;
        replay.result.distance = 120;
        replay.result.total = 150;
        replay.result.landing = Landing::Telemark;
        replay.result.telemarkWidth = .3f;
        replay.frames = {{0, 0, 30, 0, 0, 0, int(Phase::Flight), 0, 0},
                         {1, -1, 30, -1, .1f, 1, int(Phase::Outrun), 1, 0}};
        for (size_t i = 0; i < replay.frames.size(); ++i) {
            replay.frames[i].leadingLeg = int(i % 2);
            replay.frames[i].telemarkWidth = .3f;
            replay.frames[i].windSpeed = float(2 + i);
            replay.frames[i].windAngle = float(-1.5 + i);
            for (size_t joint = 0; joint < replay.frames[i].pose.size(); ++joint)
                replay.frames[i].pose[joint] = float(joint * .01 + i);
        }
        auto replayPath = directory / "latest-dsj2.sjr";
        Replay loaded;
        require(saveReplay(replayPath.string(), replay) && loadReplay(replayPath.string(), loaded),
                "The final original hill must support replay persistence");
        require(loaded.hill == replay.hill && loaded.frames.size() == replay.frames.size(),
                "Replay should resolve its stable profile ID");
        require(loaded.result.telemarkWidth == replay.result.telemarkWidth,
                "Replay retains the assessed telemark width");
        for (size_t i = 0; i < replay.frames.size(); ++i)
            require(loaded.frames[i].leadingLeg == replay.frames[i].leadingLeg &&
                        loaded.frames[i].pose == replay.frames[i].pose &&
                        loaded.frames[i].telemarkWidth == replay.frames[i].telemarkWidth &&
                        loaded.frames[i].windSpeed == replay.frames[i].windSpeed &&
                        loaded.frames[i].windAngle == replay.frames[i].windAngle,
                    "Replay retains all original joints, telemark width and wind exactly");
        replay.originalRpl.resize(1222);
        for (size_t i = 0; i < replay.originalRpl.size(); ++i)
            replay.originalRpl[i] = uint8_t(i);
        require(saveReplay(replayPath.string(), replay) && loadReplay(replayPath.string(), loaded) &&
                    loaded.originalRpl == replay.originalRpl,
                "Original replay attachment persists without byte changes");
        auto badAttachment = contents(replayPath);
        badAttachment.replace(badAttachment.find("RPL 1222"), 8, "RPL 1223");
        write(replayPath, badAttachment);
        require(!loadReplay(replayPath.string(), loaded) && loaded.originalRpl == replay.originalRpl,
                "Malformed replay attachment rejects without changing output");
        require(saveReplay(replayPath.string(), replay), "Restore valid native replay");
        auto savedReplay = contents(replayPath);
        auto unknown = savedReplay;
        const auto idPosition = unknown.find('"');
        const auto idEnd = unknown.find('"', idPosition + 1);
        unknown.replace(idPosition, idEnd - idPosition + 1, "\"unknown-profile\"");
        write(replayPath, unknown);
        require(!loadReplay(replayPath.string(), loaded), "Unknown replay profiles should be rejected");
        require(loaded.hill == replay.hill, "A failed replay load must preserve its output");
        auto wrongRevision = savedReplay;
        wrongRevision.replace(wrongRevision.find(profiles), profiles.size(), "OTHER_PROFILE_SET");
        write(replayPath, wrongRevision);
        require(!loadReplay(replayPath.string(), loaded), "Reject incompatible geometry revisions");
        auto invalidPose = replay;
        invalidPose.frames.back().pose[4] = std::numeric_limits<float>::infinity();
        require(saveReplay(replayPath.string(), invalidPose), "Write an invalid pose fixture");
        require(!loadReplay(replayPath.string(), loaded) &&
                    loaded.frames.back().pose == replay.frames.back().pose,
                "Reject nonfinite pose data while preserving the last valid replay");
        const auto previousReplayPath = directory / "latest-original.sjr";
        const std::string previousReplay = "SJP_REPLAY_3\nDSJ2_ORIGINAL_PROFILES_1\n";
        write(previousReplayPath, previousReplay);
        require(!loadReplay(previousReplayPath.string(), loaded) &&
                    contents(previousReplayPath) == previousReplay,
                "Previous flight-model replay must be rejected and preserved");
        replay.frames.back().elapsed = -1;
        require(saveReplay(replayPath.string(), replay), "Write a malformed time sequence fixture");
        require(!loadReplay(replayPath.string(), loaded), "Replay validation must reject decreasing time");
        replay.hill = int(hills().size());
        require(!saveReplay(replayPath.string(), replay), "Reject out-of-range hill indices when saving");
        const auto legacyReplayPath = directory / "latest.sjr";
        const std::string legacyReplay = "SJP_REPLAY_2\n2 2 2\n100 150 0 1\n"
                                         "0 0 30 0 0 0 2 0 0\n1 -1 30 -1 0 1 3 1 0\n";
        write(legacyReplayPath, legacyReplay);
        require(!loadReplay(legacyReplayPath.string(), loaded),
                "Legacy coordinates must not be played on original hill geometry");
        require(contents(legacyReplayPath) == legacyReplay, "Legacy replays must remain intact");
        std::filesystem::remove_all(directory);
        std::cout << "Original-profile storage and legacy migration checks passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::filesystem::remove_all(directory);
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
