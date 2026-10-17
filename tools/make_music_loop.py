#!/usr/bin/env python3
"""Make a stereo PCM16 loop with a circular equal-power crossfade.

Example for the supplied The Perfect V theme (original is never overwritten):
  python3 tools/make_music_loop.py captures/menu-music/tpv_theme_music.wav \
      captures/menu-music/tpv_theme_music_loop.wav \
      --start 8.42 --end 59.14672 --crossfade 2.30576

Start/end are corresponding musical positions in the source. The crossfade is
centered on the loop boundary, reading a half-width outside both positions.
The output period remains end - start; there is no fade to silence or time stretch.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--start', type=float, required=True)
    parser.add_argument('--end', type=float, required=True)
    parser.add_argument('--crossfade', type=float, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('Source and output must be different files')
    if args.output.exists():
        parser.error('Output already exists; choose a new name')
    if not all(math.isfinite(x) for x in (args.start, args.end, args.crossfade)):
        parser.error('Times must be finite')
    with wave.open(str(args.source), 'rb') as wav:
        if wav.getsampwidth() != 2 or wav.getnchannels() != 2 or wav.getcomptype() != 'NONE':
            parser.error('Source must be uncompressed stereo 16-bit PCM WAV')
        rate, frames = wav.getframerate(), wav.getnframes()
        source = array.array('h', wav.readframes(frames))
    if sys.byteorder != 'little':
        source.byteswap()
    start, end = round(args.start * rate), round(args.end * rate)
    half = round(args.crossfade * rate / 2)
    length = end - start
    if half <= 0 or length <= 2 * half or start < half or end + half > frames:
        parser.error('Crossfade must fit around start/end and be shorter than the loop')
    output = array.array('h', source[start * 2:end * 2])
    peak = 0.
    # Around the file boundary, outgoing audio comes from end+offset, incoming
    # audio from start+offset. The same continuous fade spans last and first frames.
    for offset in range(-half, half):
        fraction = (offset + half) / (2 * half)
        incoming = math.sin(fraction * math.pi / 2)
        outgoing = math.cos(fraction * math.pi / 2)
        target = offset % length
        for channel in (0, 1):
            value = (source[(end + offset) * 2 + channel] * outgoing +
                     source[(start + offset) * 2 + channel] * incoming)
            peak = max(peak, abs(value))
            # Refuse clipping rather than altering the dynamics of the rest of the song.
            if abs(value) > 32767:
                parser.error('Crossfade exceeds PCM headroom; reduce source level before editing')
            output[target * 2 + channel] = round(value)
    original_hash = hashlib.sha256(args.source.read_bytes()).hexdigest()
    seam = max(abs(output[-2 + c] - output[c]) for c in (0, 1))
    if sys.byteorder != 'little':
        output.byteswap()
    with wave.open(str(args.output), 'wb') as wav:
        wav.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        wav.writeframes(output.tobytes())
    report = dict(source=str(args.source), source_sha256=original_hash,
                  output=str(args.output), sample_rate=rate, channels=2,
                  start_seconds=start / rate, end_seconds=end / rate,
                  duration_seconds=length / rate, crossfade_seconds=2 * half / rate,
                  peak_crossfade_dbfs=20 * math.log10(max(peak, 1) / 32768),
                  boundary_step_dbfs=20 * math.log10(max(seam, 1) / 32768),
                  time_stretch=False)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
