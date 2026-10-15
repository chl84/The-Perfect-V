#include "audio.h"
#include "visual_grandstands.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace sjp {
struct AudioTestAccess {
    static void freeze(Audio &audio) {
        if (!audio.stream || !SDL_PauseAudioStreamDevice(audio.stream) || !SDL_LockAudioStream(audio.stream))
            throw std::runtime_error("Dummy audio device failed");
    }
    static void unfreeze(Audio &audio) { SDL_UnlockAudioStream(audio.stream); }
    static void generate(Audio &audio, float *out, int count) { audio.generate(out, count); }
    static CrowdAudioState state(const Audio &audio) {
        CrowdAudioState state;
        state.murmur = audio.murmur.load();
        state.cheer = audio.cheer.load();
        state.reaction = audio.reaction.load();
        state.rate = audio.playbackRate.load();
        state.paused = audio.crowdPaused.load();
        const uint64_t command = audio.crowdCommand.load();
        state.serial = uint32_t(command >> 32);
        state.reactionSample = int(uint32_t(command)) - 1;
        return state;
    }
};
struct AudioFreeze {
    Audio &audio;
    explicit AudioFreeze(Audio &a) : audio(a) { AudioTestAccess::freeze(audio); }
    ~AudioFreeze() { AudioTestAccess::unfreeze(audio); }
};
} // namespace sjp

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
sjp::Frame frame(const sjp::Hill &hill, double x, sjp::Phase phase, double elapsed, bool crashed = false) {
    sjp::Frame f;
    f.x = float(x);
    f.y = float(hill.ground(x) + (phase == sjp::Phase::Flight ? 5 : 0));
    f.phase = int(phase);
    f.elapsed = float(elapsed);
    f.crashed = crashed;
    return f;
}
double energy(const std::vector<float> &samples) {
    double sum = 0;
    for (float v : samples) {
        require(std::isfinite(v) && std::abs(v) <= 1, "Audio sample is invalid or clips");
        sum += v * v;
    }
    return std::sqrt(sum / samples.size());
}
} // namespace

int main() {
    using namespace sjp;
    try {
        for (const auto &h : hills()) {
            CrowdPresentation crowd;
            crowd.begin(h);
            auto f = frame(h, h.start(), Phase::Ready, 0);
            const auto ready = crowd.update(f, .016, CrowdPlayback::Active);
            require(ready.cheer == 0 && ready.murmur == 0, "Audience is audible at the start gate");
            f = frame(h, 0, Phase::Flight, 2);
            require(crowd.proximity(f) == 0, "Distant audience is audible near takeoff");
            const double first = visual::GrandstandLayout::center(h, 0);
            f = frame(h, first - 25 * h.scale(), Phase::Flight, 3);
            const auto approaching = crowd.update(f, .016, CrowdPlayback::Active);
            f = frame(h, first, Phase::Flight, 4);
            const auto near = crowd.update(f, .016, CrowdPlayback::Active);
            require(approaching.murmur > 0 && near.murmur > approaching.murmur,
                    "Crowd does not fade in as the jumper approaches");
            f = frame(h, visual::GrandstandLayout::center(h, 8), Phase::Flight, 5);
            const auto longer = crowd.update(f, .016, CrowdPlayback::Active);
            require(longer.cheer > near.cheer * 1.10, "Longer flights do not increase the cheering");
            f.phase = int(Phase::Outrun);
            f.crashed = 1;
            const auto crash = crowd.update(f, .016, CrowdPlayback::Active);
            require(crash.cheer > 0 && crash.cheer < longer.cheer * .2 && crash.reaction > 0 &&
                        crash.reaction < .5 && crash.reactionSample == 0,
                    "A fall must hush cheers and use a restrained concern reaction");
            require(crash.murmur > 0 && crash.murmur < longer.murmur,
                    "A fall must retain subdued crowd ambience beneath the reaction");
            f.elapsed += 1;
            const auto falling = crowd.update(f, .016, CrowdPlayback::Active);
            require(falling.serial == crash.serial && falling.reactionSample == 48000,
                    "Fall reaction restarts on every update");
            const auto paused = crowd.update(f, .5, CrowdPlayback::Paused);
            require(paused.cheer == 0 && paused.murmur == 0 && paused.reaction == 0 && paused.paused,
                    "Paused jump does not silence the audience");
            const auto resume = crowd.update(f, .016, CrowdPlayback::Active);
            require(resume.reactionSample == 48000 && resume.serial != falling.serial,
                    "Pause advanced or restarted the fall reaction");
            const auto result = crowd.update(f, .5, CrowdPlayback::Result);
            require(result.reactionSample == 72000 && result.cheer < crash.cheer && result.murmur > 0,
                    "Result screen cuts off the fall reaction or subdued background");
            crowd.begin(h);
            f = frame(h, 0, Phase::Outrun, 2, true);
            const auto early = crowd.update(f, .016, CrowdPlayback::Active);
            require(early.reaction == 0 && early.cheer == 0 && early.murmur == 0,
                    "Early distant fall forces an audible crowd reaction");
            crowd.begin(h);
            const auto retry =
                crowd.update(frame(h, h.start(), Phase::Ready, 0), .016, CrowdPlayback::Active);
            require(retry.reactionSample == -1 && retry.serial != early.serial,
                    "New jump retains the previous fall reaction");
        }

        const auto &hill = hills()[2];
        // Real recorded trajectories exercise the same frame data used by live
        // play, including frames around takeoff, landing and the original outrun.
        for (int index = 0; index < int(HILL_COUNT); ++index) {
            Simulation sim(index, 0);
            CrowdPresentation live;
            live.begin(sim.hill());
            float peak = 0;
            int fallSerialChanges = 0;
            uint32_t lastSerial = 0;
            for (int tick = 0; tick < 20000 && sim.phase != Phase::Finished; ++tick) {
                sim.step(ORIGINAL_TICK, autopilot(sim));
                const auto state = live.update(sim.frame(), ORIGINAL_TICK, CrowdPlayback::Active);
                if (sim.phase == Phase::Flight)
                    peak = std::max(peak, state.cheer);
                if (sim.crashed) {
                    require(state.cheer < .08, "Actual crashed trajectory retains loud cheering");
                    if (state.serial != lastSerial)
                        ++fallSerialChanges;
                }
                lastSerial = state.serial;
            }
            require(sim.phase == Phase::Finished && peak > .10, "Actual jump never reaches an audible crowd");
            require(fallSerialChanges <= 1, "Actual fall triggers its reaction more than once");
        }
        const double x = visual::GrandstandLayout::center(hill, 4);
        const std::vector<Frame> replay{frame(hill, x, Phase::Flight, 4),
                                        frame(hill, x, Phase::Outrun, 5, true),
                                        frame(hill, x + 8, Phase::Outrun, 8, true)};
        CrowdPresentation crowd;
        crowd.begin(hill, &replay);
        auto state = crowd.update(replay.back(), .016, CrowdPlayback::Active, .5);
        require(state.reactionSample == 144000 && state.rate == .5,
                "Replay seek into a fall restarts the gasp or ignores replay speed");
        const uint32_t previous = state.serial;
        crowd.seek();
        state = crowd.update(replay.front(), .016, CrowdPlayback::Active);
        require(state.reactionSample == -1 && state.reaction == 0 && state.cheer > 0 &&
                    state.serial != previous,
                "Seeking back into flight keeps the fall reaction");
        crowd.seek();
        state = crowd.update(replay[1], .016, CrowdPlayback::Active);
        require(state.reactionSample == 0 && state.cheer > 0 && state.cheer < .08,
                "Replaying a fall does not trigger its reaction");
        state = crowd.update(replay.back(), .016, CrowdPlayback::Off);
        require(state.paused && state.reactionSample == -1 && state.reaction == 0,
                "Leaving the scene leaks crowd audio");

        const auto clips = loadCrowdSamples(SJP_TEST_ASSET_DIR);
        for (const auto *clip : {&clips.murmur, &clips.cheer, &clips.fall})
            require(clip->size() > 48000 && energy(*clip) > .02, "Crowd recording is empty or inaudible");
        for (const auto *clip : {&clips.murmur, &clips.cheer})
            require(std::abs(clip->front() - clip->back()) < .08, "Crowd loop seam produces a click");
        // Synthetic isolated stems make fades and the one-shot behavior measurable.
        CrowdMixer cheerOnly({{}, std::vector<float>(48000, .5f), {}});
        CrowdAudioState mixState;
        mixState.serial = 1;
        mixState.paused = false;
        mixState.cheer = 1;
        cheerOnly.set(mixState);
        float v = 0;
        for (int i = 0; i < 48000; ++i)
            v = cheerOnly.next();
        require(v > .49, "Cheering never reaches its target level");
        mixState.serial = 2;
        mixState.cheer = 0;
        mixState.reactionSample = 0;
        cheerOnly.set(mixState);
        require(std::abs(cheerOnly.next() - v) < .001, "Hushing the cheer makes an abrupt click");
        for (int i = 0; i < 7200; ++i)
            v = cheerOnly.next();
        require(v < .002, "Jubilation continues after a fall");
        CrowdMixer fallOnly({{}, {}, std::vector<float>(4800, .5f)});
        mixState.reaction = 1;
        fallOnly.set(mixState);
        for (int i = 0; i < 4800; ++i)
            v = fallOnly.next();
        require(v > .48, "Fall reaction does not become audible quickly");
        for (int block = 0; block < 4; ++block) {
            fallOnly.set(mixState);
            for (int i = 0; i < 4800; ++i)
                require(fallOnly.next() == 0, "Fall clip loops or retriggers on the same event");
        }

        CrowdMixer real(clips);
        mixState.murmur = .23;
        mixState.cheer = .76;
        mixState.reaction = .85;
        mixState.serial = 3;
        mixState.rate = 2;
        real.set(mixState);
        std::vector<float> output(48000 * 15);
        for (auto &sample : output)
            sample = real.next();
        require(energy(output) > .025, "Mixed crowd is inaudible");
        mixState.paused = true;
        mixState.murmur = mixState.cheer = mixState.reaction = 0;
        real.set(mixState);
        for (int i = 0; i < 24000; ++i)
            v = real.next();
        require(v == 0, "Pause fails to fade crowd audio to silence");

        require(SDL_Init(SDL_INIT_AUDIO), "SDL audio initialization failed");
        {
            Audio audio(SJP_TEST_ASSET_DIR);
            AudioFreeze freeze(audio);
            audio.beginJump(hill);
            auto f = replay.front();
            f.vx = 30;
            audio.update(f, .016, CrowdPlayback::Active, true);
            AudioTestAccess::generate(audio, output.data(), int(output.size()));
            require(energy(output) > .02, "Audio device path does not mix the crowd");
            audio.update(f, .016, CrowdPlayback::Active, false);
            audio.cue(2);
            AudioTestAccess::generate(audio, output.data(), int(output.size()));
            require(energy(output) == 0, "Sound toggle fails to mute samples or cues");
            audio.update(replay[1], .016, CrowdPlayback::Active, true);
            AudioTestAccess::generate(audio, output.data(), 1024);
            audio.stop();
            audio.update(replay[1], .016, CrowdPlayback::Off, true);
            AudioTestAccess::generate(audio, output.data(), int(output.size()));
            require(std::all_of(output.end() - 48000, output.end(), [](float s) { return s == 0; }),
                    "Menu audio retains a fall, cheer, or signal");

            // Interrupt a live jump with replays of a different hill and
            // different fall times. Restoring must retain the live timeline,
            // including result-screen reaction progress, across replacements.
            const auto &liveHill = hills().back();
            const double liveX = visual::GrandstandLayout::center(liveHill, 4);
            for (auto mode : {CrowdPlayback::Active, CrowdPlayback::Paused, CrowdPlayback::Result}) {
                for (bool crashed : {false, true}) {
                    audio.beginJump(liveHill);
                    CrowdPresentation expected;
                    expected.begin(liveHill);
                    auto live = frame(liveHill, liveX, Phase::Flight, 4);
                    audio.update(live, .016, CrowdPlayback::Active, true);
                    expected.update(live, .016, CrowdPlayback::Active);
                    if (crashed) {
                        live = frame(liveHill, liveX, Phase::Outrun, 5, true);
                        audio.update(live, .016, CrowdPlayback::Active, true);
                        expected.update(live, .016, CrowdPlayback::Active);
                        live.elapsed = 6;
                    }
                    audio.update(live, 1.25, mode, true);
                    expected.update(live, 1.25, mode);
                    audio.beginReplay(hill, replay);
                    audio.update(replay.back(), .5, CrowdPlayback::Active, true, 2);
                    audio.seekCrowd();
                    audio.beginReplay(hills()[0], {});
                    audio.update(replay.front(), .5, CrowdPlayback::Active, true, .25);
                    const auto replayState = AudioTestAccess::state(audio);
                    audio.endReplay();
                    audio.update(live, .016, mode, true);
                    const auto restored = AudioTestAccess::state(audio);
                    const auto wanted = expected.update(live, .016, mode);
                    require(restored.murmur == wanted.murmur && restored.cheer == wanted.cheer &&
                                restored.reaction == wanted.reaction && restored.rate == wanted.rate &&
                                restored.paused == wanted.paused &&
                                restored.reactionSample == wanted.reactionSample,
                            "Returning from replay lost the live hill, fall timing or result fade");
                    require(restored.serial != replayState.serial,
                            "Returning from replay failed to seek the audio mixer");
                    audio.endReplay();
                    require(AudioTestAccess::state(audio).serial == restored.serial,
                            "Closing an already closed replay changed live audio");
                }
            }
            audio.beginReplay(hill, replay);
            audio.beginJump(liveHill);
            audio.endReplay();
            auto live = frame(liveHill, liveX, Phase::Flight, 4);
            audio.update(live, .016, CrowdPlayback::Active, true);
            require(AudioTestAccess::state(audio).murmur > .2,
                    "Starting a new jump retained a stale replay audio context");
        }
        SDL_Quit();
        std::cout << "Crowd approach, flight intensity, distant/near falls, replay, fades and mute passed\n";
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
