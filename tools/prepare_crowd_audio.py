#!/usr/bin/env python3
"""Rebuild the small crowd WAVs from the CC0 recordings in assets/audio/CREDITS.md.

Requires ffmpeg. Pass a directory containing cheer.mp3, murmur.mp3, gasp.mp3,
and concern.mp3 (the linked Freesound HQ previews). No runtime decoder is needed.
"""
import argparse
import array
import math
from pathlib import Path
import subprocess
import sys
import wave

RATE = 24000


def read(directory, name, start=0, duration=30):
    command = ["ffmpeg", "-v", "error", "-i", str(directory / (name + ".mp3")),
               "-ss", str(start), "-t", str(duration), "-ac", "1", "-ar", str(RATE),
               "-af", "highpass=f=110,lowpass=f=7000", "-f", "f32le", "-"]
    samples = array.array("f", subprocess.check_output(command))
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def level(samples, rms):
    mean = sum(samples) / len(samples)
    samples = [v - mean for v in samples]
    scale = min(rms / math.sqrt(sum(v * v for v in samples) / len(samples)),
                .8 / max(abs(v) for v in samples))
    return [v * scale for v in samples]


def loop(samples):
    overlap = int(.45 * RATE)
    # End joins the start with a smooth overlap instead of a silent gap.
    return samples[overlap:-overlap] + [
        samples[-overlap + i] * (1 - i / (overlap - 1)) +
        samples[i] * (i / (overlap - 1)) for i in range(overlap)]


def write(path, samples):
    pcm = array.array("h", (round(max(-1, min(1, v)) * 32767) for v in samples))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "assets/audio")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    write(args.output / "crowd-murmur.wav", loop(level(read(args.source, "murmur", 15, 14), .10)))
    write(args.output / "crowd-cheer.wav", loop(level(read(args.source, "cheer", 1.1, 11), .18)))
    gasp = level(read(args.source, "gasp"), .19)
    concern = level(read(args.source, "concern", .2, 8.2), .12)
    offset = int(.55 * RATE)
    fall = [0.] * (offset + len(concern))
    for i, value in enumerate(gasp):
        # Keep the opening gasp beneath the surrounding crowd's response.
        fall[i] += value * .50
    for i, value in enumerate(concern):
        # Stagger the surrounding crowd's response beneath the first gasp.
        envelope = min(1, i / (.35 * RATE), (len(concern) - i) / (.6 * RATE))
        fall[offset + i] += value * .70 * envelope
    for i in range(len(fall)):
        fall[i] *= min(1, i / (.008 * RATE), (len(fall) - i) / (.06 * RATE))
    write(args.output / "crowd-fall.wav", fall)


if __name__ == "__main__":
    main()
