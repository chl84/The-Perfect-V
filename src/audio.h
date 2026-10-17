#pragma once
#include "crowd_audio.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <optional>

namespace sjp {
class Audio {
  public:
    explicit Audio(const std::string &assets);
    ~Audio();
    void beginJump(const Hill &, const std::vector<Frame> *recording = nullptr);
    void beginReplay(const Hill &, const std::vector<Frame> &recording);
    void endReplay();
    void seekCrowd() { crowd.seek(); }
    void update(const Frame &, double dt, CrowdPlayback, bool soundOn, float rate = 1);
    void stop();
    void setMenuMusic(bool play) { musicActive = play; }
    bool menuMusicRequested() const { return musicActive.load(); }
    void cue(int cue) {
        tone = cue;
        remaining = cue == 3 ? 24000 : 6000;
    }

  private:
    friend struct AudioTestAccess;
    SDL_AudioStream *stream = nullptr, *musicStream = nullptr;
    // Only the audio callback changes the cursor and gain. Main thread publishes a target.
    std::vector<float> musicSamples;
    size_t musicPosition = 0;
    float musicGain = 0;
    std::atomic<bool> musicActive{false};
    std::atomic<float> volume{0};
    std::atomic<bool> enabled{true};
    std::atomic<int> tone{0}, remaining{0};
    CrowdPresentation crowd;
    std::optional<CrowdPresentation> liveCrowd;
    CrowdMixer mixer;
    std::atomic<float> murmur{0}, cheer{0}, reaction{0}, playbackRate{1};
    std::atomic<bool> crowdPaused{true};
    // A single atomic command keeps the event revision and seek position together.
    std::atomic<uint64_t> crowdCommand{0};
    void publish(const CrowdAudioState &);
    void resetCrowd(const Hill &, const std::vector<Frame> *recording);
    double phase = 0;
    uint32_t noise = 0xdeadbeef;
    static void callback(void *, SDL_AudioStream *, int, int);
    void generate(float *, int);
    static void musicCallback(void *, SDL_AudioStream *, int, int);
    void generateMusic(float *stereo, int frames);
};
} // namespace sjp
