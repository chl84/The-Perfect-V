#include "audio.h"
#include <cmath>

namespace sjp {
Audio::Audio() {
    SDL_AudioSpec spec{};
    spec.freq = 48000;
    spec.format = AUDIO_F32SYS;
    spec.channels = 1;
    spec.samples = 1024;
    spec.callback = callback;
    spec.userdata = this;
    device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (device)
        SDL_PauseAudioDevice(device, 0);
}
Audio::~Audio() {
    if (device)
        SDL_CloseAudioDevice(device);
}
void Audio::callback(void *data, Uint8 *stream, int bytes) {
    auto &a = *static_cast<Audio *>(data);
    float *out = reinterpret_cast<float *>(stream);
    int n = bytes / sizeof(float);
    float v = a.volume.load();
    int initial = a.remaining.load(), left = initial, cue = a.tone.load();
    bool enabled = a.enabled.load();
    for (int i = 0; i < n; i++) {
        a.noise ^= a.noise << 13;
        a.noise ^= a.noise >> 17;
        a.noise ^= a.noise << 5;
        float noise = float(a.noise & 65535) / 32768 - 1;
        float t = 0;
        if (left > 0) {
            double frequency = cue == 1   ? 620
                               : cue == 2 ? 880
                               : cue == 3 ? 440 + (left / 6000 % 3) * 220
                                          : 220;
            t = std::sin(a.phase) * .14 * std::min(1.f, left / 4000.f);
            a.phase += frequency * 6.283185307 / 48000;
            --left;
        }
        out[i] = enabled ? noise * v * .15f + t : 0;
    }
    // Preserve a new cue if the main thread submitted one during this audio block.
    a.remaining.compare_exchange_strong(initial, left);
}
} // namespace sjp
