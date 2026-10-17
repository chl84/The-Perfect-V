#include "storage.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        Records records;
        Result jump;
        jump.distance = 147.5;
        jump.landing = Landing::Telemark;
        require(!updateHillRecord(records, 2, jump, false) && records.best[2] == 0, "CPU set a hill record");
        require(updateHillRecord(records, 2, jump, true) && records.best[2] == 147.5,
                "First standing human jump did not set a record");
        require(!updateHillRecord(records, 2, jump, true), "A tie counted as a new record");
        jump.distance = 146;
        require(!updateHillRecord(records, 2, jump, true) && records.best[2] == 147.5,
                "A shorter human jump reduced the all-time record");
        jump.distance = 151.5;
        jump.crashed = true;
        require(!updateHillRecord(records, 2, jump, true), "A fall set a record");
        jump.crashed = false;
        jump.disqualified = true;
        require(!updateHillRecord(records, 2, jump, true), "A disqualified jump set a record");
        jump.disqualified = false;
        jump.beforeTakeoff = true;
        require(!updateHillRecord(records, 2, jump, true), "A fall before takeoff set a record");
        jump.beforeTakeoff = false;
        require(!updateHillRecord(records, -1, jump, true) &&
                    !updateHillRecord(records, int(HILL_COUNT), jump, true),
                "Invalid hill accepted");
        for (double distance : {0.0, -1.0, 1001.0, std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()}) {
            jump.distance = distance;
            require(!updateHillRecord(records, 2, jump, true), "Invalid distance set a record");
        }
        jump.distance = 151.5;
        require(updateHillRecord(records, 2, jump, true),
                "Another human could not improve the shared record");
        const auto directory = std::filesystem::temp_directory_path() /
                               ("perfect-v-hill-record-tests-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto path = (directory / "records-dsj2.dat").string();
        require(saveRecords(path, records), "Human record was not persisted");
        auto restarted = loadRecords(path);
        require(restarted.best == records.best, "Restart lost or moved a hill record");
        jump.distance = 175;
        require(!updateHillRecord(restarted, 2, jump, false), "CPU replaced a record after restart");
        jump.distance = 149;
        require(!updateHillRecord(restarted, 2, jump, true), "Restart erased the all-time comparison");
        jump.distance = 152;
        require(updateHillRecord(restarted, 2, jump, true), "Human improvement failed after restart");
        jump.distance = 110.5;
        require(updateHillRecord(restarted, 0, jump, true) && restarted.best[2] == 152,
                "Record on another hill changed the previous hill");
        require(saveRecords(path, restarted) && loadRecords(path).best == restarted.best,
                "Updated records did not survive another restart");
        jump.distance = 152.12;
        require(updateHillRecord(restarted, 2, jump, true) && restarted.best[2] == 152.12,
                "Centimeter improvement did not replace the half-meter record");
        jump.distance = 152.13;
        require(updateHillRecord(restarted, 2, jump, true),
                "A one-centimeter improvement within the same half meter was ignored");
        require(saveRecords(path, restarted) && loadRecords(path).best[2] == 152.13,
                "Centimeter record lost precision across restart");
        std::filesystem::remove_all(directory);
        std::cout << "PASS: standing Humans only, shared per-hill maxima and restart persistence\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
