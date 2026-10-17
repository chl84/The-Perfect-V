#include "audio.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace sjp {
namespace {
std::vector<float> loadMenuMusic(const std::string &assets) {
    const auto path = assets + "/audio/menu-theme.wav";
    SDL_AudioSpec source{}, destination{SDL_AUDIO_F32, 2, 48000};
    Uint8 *bytes = nullptr, *converted = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path.c_str(), &source, &bytes, &length))
        throw std::runtime_error("Cannot load menu music: " + path + ": " + SDL_GetError());
    int convertedLength = 0;
    const bool ok = SDL_ConvertAudioSamples(&source, bytes, int(length), &destination, &converted, &convertedLength);
    SDL_free(bytes);
    if (!ok)
        throw std::runtime_error("Cannot convert menu music: " + path + ": " + SDL_GetError());
    if (convertedLength <= 0 || convertedLength % (2 * sizeof(float)) != 0) {
        SDL_free(converted);
        throw std::runtime_error("Invalid stereo menu music: " + path);
    }
    std::vector<float> samples(size_t(convertedLength) / sizeof(float));
    std::memcpy(samples.data(), converted, size_t(convertedLength));
    SDL_free(converted);
    return samples;
}
} // namespace
Audio::Audio(const std::string &assets) : musicSamples(loadMenuMusic(assets)), mixer(loadCrowdSamples(assets)) {
    SDL_AudioSpec spec{};
    spec.freq = 48000;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
    if (stream)
        SDL_ResumeAudioStreamDevice(stream);
    spec.channels = 2;
    musicStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, musicCallback, this);
    if (musicStream)
        SDL_ResumeAudioStreamDevice(musicStream);
}
void Audio::publish(const CrowdAudioState &state) {
    murmur = state.murmur;
    cheer = state.cheer;
    reaction = state.reaction;
    playbackRate = state.rate;
    crowdPaused = state.paused;
    crowdCommand = (uint64_t(state.serial) << 32) | uint32_t(state.reactionSample + 1);
}
void Audio::beginJump(const Hill &hill, const std::vector<Frame> *recording) {
    liveCrowd.reset();
    resetCrowd(hill, recording);
}
void Audio::beginReplay(const Hill &hill, const std::vector<Frame> &recording) {
    // Replacing an open replay must retain the original live jump context.
    if (!liveCrowd)
        liveCrowd = crowd;
    resetCrowd(hill, &recording);
}
void Audio::endReplay() {
    if (!liveCrowd)
        return;
    crowd.restore(*liveCrowd);
    liveCrowd.reset();
    stop();
}
void Audio::resetCrowd(const Hill &hill, const std::vector<Frame> *recording) {
    crowd.begin(hill, recording);
    publish(crowd.update({}, 0, CrowdPlayback::Off));
    remaining = 0;
    volume = 0;
}
void Audio::update(const Frame &frame, double dt, CrowdPlayback mode, bool soundOn, float rate) {
    if (enabled.load() != soundOn)
        crowd.seek();
    enabled = soundOn;
    volume = mode == CrowdPlayback::Active && soundOn
                 ? float(std::min(1.0, std::hypot(frame.vx, frame.vy) / 150.0))
                 : 0;
    publish(crowd.update(frame, dt, mode, rate));
}
void Audio::stop() {
    crowd.seek();
    publish(crowd.update({}, 0, CrowdPlayback::Off));
    volume = 0;
    remaining = 0;
}
Audio::~Audio() {
    if (musicStream)
        SDL_DestroyAudioStream(musicStream);
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
void Audio::musicCallback(void *data, SDL_AudioStream *stream, int additional, int) {
    auto &audio = *static_cast<Audio *>(data);
    std::array<float, 1024> samples{};
    constexpr int frameBytes = 2 * sizeof(float);
    int remaining = additional > 0 ? (additional + frameBytes - 1) / frameBytes : 0;
    while (remaining > 0) {
        const int frames = std::min(remaining, int(samples.size() / 2));
        audio.generateMusic(samples.data(), frames);
        if (!SDL_PutAudioStreamData(stream, samples.data(), frames * frameBytes))
            break;
        remaining -= frames;
    }
}
void Audio::generateMusic(float *stereo, int frames) {
    constexpr float level = .55f;
    constexpr float fadeStep = level / (48000 * .4f);
    const float target = musicActive.load() ? level : 0;
    for (int frame = 0; frame < frames; ++frame) {
        musicGain = musicGain < target ? std::min(target, musicGain + fadeStep)
                                       : std::max(target, musicGain - fadeStep);
        if (musicGain == 0 || musicSamples.empty()) {
            stereo[frame * 2] = stereo[frame * 2 + 1] = 0;
            continue;
        }
        stereo[frame * 2] = musicSamples[musicPosition * 2] * musicGain;
        stereo[frame * 2 + 1] = musicSamples[musicPosition * 2 + 1] * musicGain;
        if (++musicPosition == musicSamples.size() / 2)
            musicPosition = 0;
    }
}
void Audio::generate(float *out, int n) {
    auto &a = *this;
    float v = a.volume.load();
    int initial = a.remaining.load(), left = initial, cue = a.tone.load();
    bool enabled = a.enabled.load();
    const uint64_t command = a.crowdCommand.load();
    CrowdAudioState crowdState;
    crowdState.murmur = a.murmur.load();
    crowdState.cheer = a.cheer.load();
    crowdState.reaction = a.reaction.load();
    crowdState.rate = a.playbackRate.load();
    crowdState.paused = a.crowdPaused.load();
    crowdState.serial = uint32_t(command >> 32);
    crowdState.reactionSample = int(uint32_t(command)) - 1;
    a.mixer.set(crowdState);
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
        const float audience = a.mixer.next();
        out[i] = enabled ? std::clamp(noise * v * .15f + t + audience, -1.f, 1.f) : 0;
    }
    // Preserve a new cue if the main thread submitted one during this audio block.
    a.remaining.compare_exchange_strong(initial, left);
}
} // namespace sjp
