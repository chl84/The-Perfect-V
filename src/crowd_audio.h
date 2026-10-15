#pragma once
#include "physics.h"
#include <array>
#include <utility>

namespace sjp {
enum class CrowdPlayback { Active, Paused, Result, Off };
struct CrowdAudioState {
    float murmur = 0, cheer = 0, reaction = 0, rate = 1;
    bool paused = true;
    uint32_t serial = 0;
    int reactionSample = -1;
};

// Presentation follows the athlete, independently of camera, physics and wind.
class CrowdPresentation {
  public:
    static constexpr int sampleRate = 48000;
    void begin(const Hill &, const std::vector<Frame> *recording = nullptr);
    void seek() { ++serial; }
    void restore(const CrowdPresentation &saved) {
        const uint32_t nextSerial = serial + 1;
        *this = saved;
        // A restored fall must seek the mixer even if its saved revision
        // happens to match the replay that just finished.
        serial = nextSerial;
    }
    CrowdAudioState update(const Frame &, double dt, CrowdPlayback, float rate = 1);
    double proximity(const Frame &) const;

  private:
    const Hill *hill = nullptr;
    std::array<Vec3, 14> stands{};
    double near = 0, reach = 0, fallTime = -1, landingTime = -1, landingExcitement = 0;
    double resultAge = 0;
    bool falling = false, wasPaused = true;
    uint32_t serial = 0;
};

struct CrowdSamples {
    std::vector<float> murmur, cheer, fall;
};
CrowdSamples loadCrowdSamples(const std::string &assets);

// Immutable, preconverted samples. No allocation, locks or file I/O while mixing.
class CrowdMixer {
  public:
    explicit CrowdMixer(CrowdSamples samples) : samples(std::move(samples)) {}
    void set(const CrowdAudioState &);
    float next();

  private:
    CrowdSamples samples;
    CrowdAudioState target;
    uint32_t serial = 0;
    double murmurCursor = 0, cheerCursor = 0, reactionCursor = -1;
    float murmurGain = 0, cheerGain = 0, reactionGain = 0;
    static float loop(const std::vector<float> &, double &, float rate);
};
} // namespace sjp
