#include "crowd_audio.h"
#include "visual_grandstands.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace sjp {
namespace {
double smooth(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
double excitement(const Hill &h, const Frame &f) {
    return smooth(h.distance(std::max(0.0, double(f.x))) / (h.k * 1.3));
}
std::vector<float> load(const std::string &path) {
    SDL_AudioSpec source{}, destination{SDL_AUDIO_F32, 1, CrowdPresentation::sampleRate};
    Uint8 *bytes = nullptr, *converted = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path.c_str(), &source, &bytes, &length))
        throw std::runtime_error("Cannot load crowd audio: " + path + ": " + SDL_GetError());
    int convertedLength = 0;
    const bool ok =
        SDL_ConvertAudioSamples(&source, bytes, int(length), &destination, &converted, &convertedLength);
    SDL_free(bytes);
    if (!ok)
        throw std::runtime_error("Cannot convert crowd audio: " + path + ": " + SDL_GetError());
    std::vector<float> samples(size_t(convertedLength) / sizeof(float));
    std::memcpy(samples.data(), converted, size_t(convertedLength));
    SDL_free(converted);
    return samples;
}
} // namespace

void CrowdPresentation::begin(const Hill &h, const std::vector<Frame> *recording) {
    hill = &h;
    near = visual::GrandstandLayout::nearDistance(h) + 3.15;
    reach = std::max(72 * h.scale(), 36.0);
    const visual::CameraSurface surface(h);
    for (size_t i = 0; i < stands.size(); ++i) {
        const double x = visual::GrandstandLayout::center(h, int(i));
        stands[i] = {x, surface.height(x, near) + 2, near};
    }
    fallTime = landingTime = -1;
    landingExcitement = resultAge = 0;
    falling = false;
    wasPaused = true;
    ++serial;
    // Recorded event times make forward/backward seeking deterministic, and
    // prevent a gasp from restarting when seeking into an existing fall.
    if (recording)
        for (const auto &f : *recording) {
            if (fallTime < 0 && f.crashed)
                fallTime = f.elapsed;
            if (landingTime < 0 && Phase(f.phase) == Phase::Outrun) {
                landingTime = f.elapsed;
                landingExcitement = excitement(h, f);
            }
        }
}
double CrowdPresentation::proximity(const Frame &f) const {
    if (!hill)
        return 0;
    double distance = reach;
    for (Vec3 stand : stands)
        distance = std::min(distance, (stand - Vec3{f.x, f.y + .8, 0}).length());
    return smooth((reach - distance) / (reach - near));
}
CrowdAudioState CrowdPresentation::update(const Frame &f, double dt, CrowdPlayback mode, float rate) {
    CrowdAudioState state;
    const bool paused = mode == CrowdPlayback::Paused || mode == CrowdPlayback::Off;
    if (paused != wasPaused)
        ++serial;
    wasPaused = paused;
    state.serial = serial;
    state.rate = std::clamp(rate, .25f, 2.f);
    state.paused = paused;
    if (!hill || mode == CrowdPlayback::Off)
        return state;
    const Phase phase = Phase(f.phase);
    if (landingTime < 0 && (phase == Phase::Outrun || phase == Phase::Finished)) {
        landingTime = f.elapsed;
        landingExcitement = excitement(*hill, f);
    }
    if (f.crashed && fallTime < 0)
        fallTime = f.elapsed;
    const bool fall = f.crashed && fallTime >= 0 && f.elapsed >= fallTime;
    if (fall != falling) {
        falling = fall;
        state.serial = ++serial;
    }
    if (mode == CrowdPlayback::Result)
        resultAge += std::max(0.0, dt);
    else
        resultAge = 0;
    if (fall)
        state.reactionSample =
            int(std::min(60.0, std::max(0.0, f.elapsed - fallTime + resultAge)) * sampleRate);
    if (paused || phase == Phase::Ready || phase == Phase::Disqualified)
        return state;

    const double p = proximity(f);
    const double e = phase == Phase::Flight                               ? excitement(*hill, f)
                     : phase == Phase::Outrun || phase == Phase::Finished ? landingExcitement
                                                                          : 0;
    const double age = landingTime < 0 ? 0 : std::max(0.0, f.elapsed - landingTime + resultAge);
    const double release = std::exp(-age / 2.8);
    state.murmur = float(p * (fall ? .15 : .23));
    // Retain a quiet crowd bed beneath the reaction, with the cheering hushed
    // to a tenth of its previous level instead of cutting the venue to silence.
    state.cheer = float(p * (.06 + .70 * e) * release * (fall ? .10 : 1.0));
    // A distant early fall gets no artificial minimum volume. The gasp and
    // concerned voices obey the same attenuation as the cheering crowd.
    if (fall)
        state.reaction = float(p * .35);
    if (mode == CrowdPlayback::Result)
        state.murmur *= float(std::exp(-resultAge / 1.5));
    return state;
}

CrowdSamples loadCrowdSamples(const std::string &assets) {
    return {load(assets + "/audio/crowd-murmur.wav"), load(assets + "/audio/crowd-cheer.wav"),
            load(assets + "/audio/crowd-fall.wav")};
}
void CrowdMixer::set(const CrowdAudioState &state) {
    target = state;
    if (state.serial != serial) {
        serial = state.serial;
        reactionCursor = state.reactionSample;
    }
}
float CrowdMixer::loop(const std::vector<float> &clip, double &cursor, float rate) {
    if (clip.empty())
        return 0;
    const size_t at = size_t(cursor);
    const double fraction = cursor - at;
    const float value = float(clip[at] * (1 - fraction) + clip[(at + 1) % clip.size()] * fraction);
    cursor += rate;
    if (cursor >= clip.size())
        cursor = std::fmod(cursor, double(clip.size()));
    return value;
}
float CrowdMixer::next() {
    // Slow arrival, quick hush on a fall, and short pause fades avoid clicks.
    constexpr int sampleRate = CrowdPresentation::sampleRate;
    constexpr float rise = 1.f / (sampleRate * .20f), release = 1.f / (sampleRate * .35f);
    constexpr float hush = 1.f / (sampleRate * .025f);
    auto follow = [](float &gain, float goal, float down) {
        gain += (goal - gain) * (goal > gain ? rise : down);
    };
    follow(murmurGain, target.murmur, target.paused ? hush : release);
    follow(cheerGain, target.cheer, target.reactionSample >= 0 || target.paused ? hush : release);
    reactionGain +=
        (target.reaction - reactionGain) * (target.reaction > reactionGain || target.paused ? hush : release);
    const bool silent = target.paused && murmurGain + cheerGain + reactionGain < .00001f;
    if (silent)
        return 0;
    float value = loop(samples.murmur, murmurCursor, target.rate) * murmurGain +
                  loop(samples.cheer, cheerCursor, target.rate) * cheerGain;
    if (reactionCursor >= 0 && reactionCursor < samples.fall.size()) {
        const size_t at = size_t(reactionCursor);
        const double fraction = reactionCursor - at;
        const float next = at + 1 < samples.fall.size() ? samples.fall[at + 1] : 0;
        value += float(samples.fall[at] * (1 - fraction) + next * fraction) * reactionGain;
        reactionCursor += target.rate;
    }
    return std::clamp(value, -1.f, 1.f);
}
} // namespace sjp
