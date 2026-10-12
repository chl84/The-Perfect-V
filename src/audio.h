#pragma once
#include <SDL.h>
#include <atomic>

namespace sjp {
class Audio {
  public:
    Audio();
    ~Audio();
    void set(double speed, bool soundOn) {
        enabled = soundOn;
        volume = soundOn ? float(speed / 150) : 0;
    }
    void cue(int cue) {
        tone = cue;
        remaining = cue == 3 ? 24000 : 6000;
    }

  private:
    SDL_AudioDeviceID device = 0;
    std::atomic<float> volume{0};
    std::atomic<bool> enabled{true};
    std::atomic<int> tone{0}, remaining{0};
    double phase = 0;
    uint32_t noise = 0xdeadbeef;
    static void callback(void *, Uint8 *, int);
};
} // namespace sjp
