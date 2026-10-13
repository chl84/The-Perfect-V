#include "audio.h"
#include <cmath>
#include <array>

namespace sjp {
Audio::Audio() {
    SDL_AudioSpec spec{};
    spec.freq = 48000;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
    if (stream)
        SDL_ResumeAudioStreamDevice(stream);
}
Audio::~Audio() {
    if (stream)
        SDL_DestroyAudioStream(stream);
}
void Audio::callback(void *data, SDL_AudioStream *stream, int additional, int) {
    auto &a = *static_cast<Audio *>(data);
    // Fixed-sized blocks retain the 48 kHz waveform and avoid allocation in
    // SDL's audio thread. SDL converts only for the actual output device.
    std::array<float, 1024> samples{};
    int remaining = additional > 0 ? (additional + int(sizeof(float)) - 1) / int(sizeof(float)) : 0;
    while (remaining > 0) {
        const int count = std::min(remaining, int(samples.size()));
        a.generate(samples.data(), count);
        if (!SDL_PutAudioStreamData(stream, samples.data(), count * int(sizeof(float))))
            break;
        remaining -= count;
    }
}
void Audio::generate(float *out, int n) {
    auto &a = *this;
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
