#pragma once
#include "physics.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sjp {
// DSJ 2.00/2.10 uses an encrypted, fixed-size input recording, not saved poses.
struct OriginalReplayData {
    int hill = 0;
    OriginalWind weather;
    float cameraDistance = 0;
    uint32_t seed = 1;
    double recordedDistance = 0;
    uint32_t startCounter = 0, takeoffCounter = 0, flightCounter = 0;
    uint32_t leftLandingCounter = 0, rightLandingCounter = 0, impactCounter = 0;
    std::array<uint8_t, 20> takeoffButtons{};
    std::array<int8_t, 1049> mouseCounts{};
    std::string athleteName, date, author;
    // Preserve opaque/padding bytes when round-tripping an imported file.
    std::array<uint8_t, 1205> originalRecord{};
};
struct OriginalReplayMetadata {
    std::string athleteName, date, author;
    double recordedDistance = 0;
    uint32_t seed = 0;
    uint32_t impactCounter = 0, stopCounter = 0;
    bool distanceMatches = false;
};
bool decodeOriginalReplay(const std::vector<uint8_t> &bytes, OriginalReplayData &output, std::string *error = nullptr);
bool encodeOriginalReplay(const OriginalReplayData &, std::vector<uint8_t> &bytes, std::string *error = nullptr);
bool readOriginalReplay(const std::string &path, OriginalReplayData &output,
                        std::string *error = nullptr);
bool saveOriginalReplay(const std::string &path, const OriginalReplayData &data,
                        std::string *error = nullptr);
bool buildOriginalReplay(const Simulation &, OriginalReplayData &, std::string *error = nullptr);
Input originalReplayInput(const OriginalReplayData &, uint32_t counter);
bool reconstructOriginalReplay(const OriginalReplayData &, Replay &output,
                               OriginalReplayMetadata *metadata = nullptr,
                               std::string *error = nullptr);
bool loadOriginalReplay(const std::string &path, Replay &output,
                        OriginalReplayMetadata *metadata = nullptr,
                        std::string *error = nullptr);

// Captures the consumed 70 Hz controls. Export uses the source's compact event
// representation and independently reconstructs it before accepting a file.
class OriginalReplayRecording {
  public:
    void begin(int hill, const OriginalWind &, uint32_t seed,
               const std::string &athleteName = "PLAYER", const std::string &author = "The Perfect V");
    void capture(uint32_t counter, int beforeStage, int afterStage, const Input &,
                 bool firstImpact, float distance);
    bool finish(OriginalReplayData &output, std::string *error = nullptr) const;
  private:
    OriginalReplayData data;
    struct Tick { uint32_t counter; Input input; };
    std::vector<Tick> ticks;
    bool active = false;
};
} // namespace sjp
