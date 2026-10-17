// Original menu-music proposal, "Blue Hour — Nordic Theme", revision 2, for The Perfect V.
// Build: c++ -O2 -std=c++17 tools/compose_menu_music.cpp -o /tmp/tpv-music
// Render: /tmp/tpv-music captures/menu-music/blue-hour-v2.wav
// No samples, external libraries, or game runtime changes required.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

constexpr int SR = 48000;
constexpr double PI = 3.14159265358979323846, BEAT = 60.0 / 96;
constexpr int N = SR * 80; // 32 bars, exactly; all tails wrap into the next loop.
struct Stereo { float l = 0, r = 0; };
std::vector<Stereo> dry(N), send(N);
std::mt19937 rng(20261017);
double hz(double midi) { return 440 * std::pow(2., (midi - 69) / 12); }
double noise() { return double(rng()) / rng.max() * 2 - 1; }
void add(int index, double sample, double pan, double wet) {
    index %= N;
    const double l = std::sqrt((1 - pan) * .5), r = std::sqrt((1 + pan) * .5);
    dry[index].l += sample * l; dry[index].r += sample * r;
    send[index].l += sample * l * wet; send[index].r += sample * r * wet;
}
void note(double beat, double length, int midi, double gain, double pan, int voice) {
    const int start = int(beat * BEAT * SR), count = int(length * BEAT * SR);
    const double f = hz(midi);
    for (int i = 0; i < count; ++i) {
        double t = double(i) / SR, duration = double(count) / SR, s = 0, env;
        if (voice == 0) { // Soft, slowly beating string pad, gentle upper harmonics.
            env = std::min(1., t / .55) * std::min(1., (duration - t) / 1.1);
            for (int harmonic = 1; harmonic <= 5; ++harmonic) {
                const double a = 1. / (harmonic * harmonic * 1.6);
                s += a * (std::sin(2 * PI * f * harmonic * .9991 * t + .3) +
                          std::sin(2 * PI * f * harmonic * 1.0009 * t - .3));
            }
            s *= env * (.90 + .10 * std::sin(2 * PI * .23 * t));
        } else if (voice == 1 || voice == 3) { // Felt-like mallet and softer lead.
            const double decay = voice == 1 ? 2.7 : 1.5;
            env = (1 - std::exp(-t * 180)) * std::exp(-t * decay) *
                  std::min(1., (duration - t) / .16);
            s = (std::sin(2 * PI * f * t + .25 * std::sin(2 * PI * f * 2 * t) * std::exp(-t * 5)) +
                .19 * std::sin(2 * PI * f * 2 * t) * std::exp(-t * 4) +
                .045 * std::sin(2 * PI * f * 4.003 * t) * std::exp(-t * 8)) * env;
        } else if (voice == 4) { // Rounded analog lead: triangle core and a quiet pulse edge.
            env = std::min(1., t / .035) * std::min(1., (duration - t) / .24) *
                  (.72 + .28 * std::exp(-t * 3));
            const double vibrato = .0015 * std::sin(2 * PI * 4.8 * t) * std::min(1., t / .6);
            for (int h = 1; h <= 9; h += 2) {
                const double polarity = h % 4 == 1 ? 1 : -1;
                s += polarity * std::sin(2 * PI * f * h * t + h * vibrato) / (h * h);
            }
            s = (s + .10 * std::sin(2 * PI * f * 2 * .999 * t)) * env;
        } else if (voice == 5) { // Airy ice-glass accents, used as punctuation.
            env = (1 - std::exp(-t * 55)) * std::exp(-t * 1.25) *
                  std::min(1., (duration - t) / .5);
            s = (std::sin(2 * PI * f * t) + .22 * std::sin(2 * PI * f * 2.006 * t) * std::exp(-t * 2) +
                 .08 * std::sin(2 * PI * f * 3.98 * t) * std::exp(-t * 3)) * env;
        } else { // Rounded bass, no bright sawtooth edge.
            env = std::min(1., t / .025) * std::min(1., (duration - t) / .22);
            s = (std::sin(2 * PI * f * t) + .16 * std::sin(4 * PI * f * t)) * env;
        }
        add(start + i, s * gain, pan, voice == 2 ? .025 : voice == 4 ? .24 : voice == 5 ? .85 : voice == 0 ? .40 : .44);
    }
}
void drum(double beat, int kind, double gain) {
    int count = int((kind == 0 ? .38 : kind == 1 ? .22 : .085) * SR);
    double phase = 0, previous = 0, filtered = 0;
    for (int i = 0; i < count; ++i) {
        double t = double(i) / SR, sample;
        double white = noise();
        if (kind == 0) {
            phase += 2 * PI * (46 + 66 * std::exp(-t * 32)) / SR;
            sample = std::sin(phase) * (1 - std::exp(-t * 600)) * std::exp(-t * 13);
        } else if (kind == 1) {
            filtered += .42 * (white - filtered);
            sample = (filtered * .8 + std::sin(2 * PI * 185 * t) * .32 +
                      std::sin(2 * PI * 332 * t) * .11) * std::exp(-t * 25) * (1 - std::exp(-t * 1500));
        } else {
            sample = (white - previous) * std::exp(-t * 80) * .4;
            previous = white;
        }
        sample *= std::min(1., (count - i) / (SR * .015));
        add(int(beat * BEAT * SR) + i, sample * gain, kind == 2 ? .32 : -.06, kind == 1 ? .32 : .08);
    }
}
void u16(std::ofstream &out, uint16_t n) { out.put(n & 255); out.put(n >> 8); }
void u32(std::ofstream &out, uint32_t n) { u16(out, n & 65535); u16(out, n >> 16); }
int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "Usage: tpv-music output.wav\n"; return 1; }
    // 96 BPM / 32 bars: intro 0–7.5 s; A 7.5–25; B 25–40;
    // developed A 40–65; outro 65–80. The original file is preserved separately.
    // Bm9, Gmaj9, Dmaj9, Aadd9, Em9, D/F#, F#7sus4, F#7.
    const int chords[8][5] = {{47,54,57,61,66}, {43,54,57,59,62}, {50,57,61,64,66}, {45,52,57,59,64},
                              {40,55,59,62,66}, {42,54,57,62,64}, {42,54,59,61,64}, {42,54,58,61,64}};
    const int harmony[32] = {0,1,2, 0,0,1,1,2,2,3, 4,4,5,1,6,7,
                            0,0,1,1,2,2,3,3,4,1, 2,5,1,4,6,7};
    // Six-note signature: B–D–F#–E–D–C#. Repeated rhythms and a rising
    // pair of thirds provide an identity; answers change with the harmony.
    const int themes[4][6] = {{71,74,78,76,74,73}, {71,74,78,76,74,71},
                              {69,74,78,76,74,73}, {73,76,81,78,76,73}};
    const double motifAt[6] = {0,1.5,2.25,3.5,5,6.5};
    const double motifLength[6] = {.95,.55,.95,1.1,.95,1.1};
    for (int bar = 0; bar < 32; ++bar) {
        const int chord = harmony[bar]; const double base = bar * 4;
        const bool intro = bar < 3, bridge = bar >= 10 && bar < 16;
        const bool reprise = bar >= 16 && bar < 26, outro = bar >= 26;
        const double ease = outro ? 1. - (bar - 26) * .105 : intro ? .66 : 1.;
        for (int j = 1; j < 5; ++j)
            note(base, 5.2, chords[chord][j], .043 * ease, (j - 2.5) * .38, 0);
        // Sparse felt piano in the intro/outro; chordal answers elsewhere.
        const int pianoPattern[8] = {1,3,2,4,2,3,4,2};
        const int steps = intro || outro ? 4 : bridge ? 5 : 6;
        for (int k = 0; k < steps; ++k) {
            double at = intro || outro ? k * .875 : k * .625 + .25;
            note(base + at, 2.4, chords[chord][pianoPattern[(k + bar) % 8]] + 12,
                 (intro ? .052 : .027) * ease * (k % 2 ? .8 : 1.), k % 2 ? .38 : -.38, 1);
        }
        if (intro) {
            // A quiet preview of the signature, before the synth takes it up.
            if (bar == 0) for (int j = 0; j < 3; ++j)
                note(base + j * 1.25, 1.8, themes[0][j], .055, -.08, 3);
        } else if (!outro) {
            int root = chords[chord][0] - 12;
            note(base, 1.35, root, .155, 0, 2);
            note(base + 1.75, .65, root + 7, .105, 0, 2);
            note(base + 2.75, .60, root + 12, .11, 0, 2);
            int nextRoot = chords[harmony[(bar + 1) % 32]][0] - 12;
            int approach = nextRoot > root ? nextRoot - 2 : nextRoot + 2;
            note(base + 3.5, .43, approach, .085, 0, 2);
        } else if (bar < 30) {
            note(base, 3.4, chords[chord][0] - 12, .12 * ease, 0, 2);
        }
        // Main theme occurs in two-bar phrases, with short rests between notes.
        if ((bar >= 3 && bar < 10 && (bar - 3) % 2 == 0) ||
            (reprise && (bar - 16) % 2 == 0)) {
            int phrase = bar < 10 ? (bar - 3) / 2 : (bar - 16) / 2;
            int theme = phrase % 4;
            for (int j = 0; j < 6; ++j) {
                int pitch = themes[theme][j];
                const int lift[6] = {71,74,78,79,78,74};
                if (bar == 24) pitch = lift[j];
                double gain = reprise ? .108 : .098;
                note(base + motifAt[j], motifLength[j] + .18, pitch, gain, -.06, 4);
                // Quiet piano attack keeps the softness of the first proposal.
                note(base + motifAt[j], 1.8, pitch, .024, .1, 3);
            }
        }
        if (bridge && bar % 2 == 0) {
            const int bridgeNotes[3][5] = {{79,78,74,71,73}, {78,76,74,71,69}, {73,76,78,76,70}};
            const double at[5] = {.5,2,3.5,5.25,7};
            for (int j = 0; j < 5; ++j)
                note(base + at[j], 1.4, bridgeNotes[(bar - 10) / 2][j], .080, .12, 4);
        }
        if (reprise && bar % 2 == 1) {
            // Answer in the gaps instead of doubling the entire lead melody.
            const int counter[5][3] = {{85,81,78}, {83,81,78}, {85,81,78}, {83,81,76}, {86,83,78}};
            int phrase = (bar - 17) / 2;
            for (int j = 0; j < 3; ++j)
                note(base + .75 + j * 1.25, 1.7, counter[phrase][j], .025, .36, 5);
        }
        if ((bar % 4 == 2 || bar == 15) && bar < 30)
            note(base + 2.5, 4, chords[chord][4] + 24, .020 * ease, -.5, 5);
        if (outro && (bar == 26 || bar == 28)) {
            for (int j = 0; j < 3; ++j)
                note(base + .5 + j, 2.2, themes[bar == 26 ? 2 : 1][j], .047 * ease, 0, 3);
        }
        if (!intro && bar < 29) {
            double energy = outro ? .55 : bridge && bar < 14 ? .7 : reprise ? 1.05 : .95;
            drum(base, 0, .22 * energy);
            if (!outro) drum(base + 2.5, 0, .12 * energy);
            drum(base + 1, 1, .18 * energy);
            if (!outro) drum(base + 3, 1, .16 * energy);
            for (int h = 0; h < 8; ++h) {
                if ((bar % 3 == 1 && h == 6) || (outro && h % 2 == 0)) continue;
                drum(base + h * .5 + (h % 2 ? .035 : 0), 2,
                     (h % 2 ? .045 : .026) * energy);
            }
            if (bar % 4 == 3 && !outro) drum(base + 3.75, 2, .026 * energy);
        }
    }
    // Crossed tempo delays; circular convolution keeps their tails at the seam.
    auto source = send;
    for (int tap = 1; tap <= 6; ++tap) {
        int offset = int(BEAT * .75 * tap * SR);
        double g = std::pow(.44, tap) * .55;
        for (int i = 0; i < N; ++i) {
            const auto &v = source[i]; auto &d = dry[(i + offset) % N];
            d.l += (tap % 2 ? v.r : v.l) * g;
            d.r += (tap % 2 ? v.l : v.r) * g;
        }
    }
    // Diffuse dark room: independent incommensurate stereo reflection times.
    for (int tap = 0; tap < 40; ++tap) {
        double delay = .061 + tap * .0731 + .009 * std::sin(tap * 2.7);
        int offsetL = int(delay * SR), offsetR = int((delay + .0197) * SR);
        double gain = .11 * std::exp(-delay / .85);
        double lowL = 0, lowR = 0;
        for (int i = -SR; i < N; ++i) {
            const auto &v = source[(i + N) % N];
            lowL += .12 * (v.l - lowL); lowR += .12 * (v.r - lowR);
            if (i >= 0) {
                dry[(i + offsetL) % N].l += (lowL * .7 + lowR * .3) * gain;
                dry[(i + offsetR) % N].r += (lowR * .7 + lowL * .3) * gain;
            }
        }
    }
    double peak = 0, square = 0;
    for (auto &v : dry) {
        v.l = std::tanh(v.l * 1.2); v.r = std::tanh(v.r * 1.2);
        peak = std::max({peak, double(std::abs(v.l)), double(std::abs(v.r))});
        square += v.l * v.l + v.r * v.r;
    }
    const double scale = .53 / peak;
    std::ofstream out(argv[1], std::ios::binary);
    if (!out) return 1;
    out.write("RIFF", 4); u32(out, 36 + N * 4); out.write("WAVEfmt ", 8);
    u32(out, 16); u16(out, 1); u16(out, 2); u32(out, SR); u32(out, SR * 4);
    u16(out, 4); u16(out, 16); out.write("data", 4); u32(out, N * 4);
    for (auto v : dry) for (double x : {double(v.l), double(v.r)})
        u16(out, uint16_t(int16_t(std::lround(std::clamp(x * scale + noise() / 65536, -1., 1.) * 32767))));
    std::cout << "Blue Hour v2: 80 s, 96 BPM, stereo 48 kHz, peak " << 20 * std::log10(peak * scale)
              << " dBFS, RMS " << 20 * std::log10(std::sqrt(square / (2 * N)) * scale) << " dBFS\n";
    return out ? 0 : 1;
}
