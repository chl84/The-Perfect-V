# Audio assets

## Crowd recordings

All source recordings are dedicated to the public domain under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
The source pages and their CC0 declarations were checked on 2026-10-13.
The game distributes edited excerpts of the publicly available HQ MP3 previews.

| Game asset | Original recording | Author |
| --- | --- | --- |
| crowd-murmur.wav | [crowd in church.mp3](https://freesound.org/people/jaava73/sounds/40626/) | jaava73 |
| crowd-cheer.wav | [Crowd Cheering](https://freesound.org/people/SoundsExciting/sounds/365132/) | SoundsExciting |
| crowd-fall.wav, initial gasp | [Gasp.wav](https://freesound.org/people/Dvideoguy/sounds/207779/) | Dvideoguy |
| crowd-fall.wav, concerned group | [small crowd gasp shock surprise](https://freesound.org/people/HowardV/sounds/264376/) | HowardV |

Edits: mono downmix, 24 kHz / 16-bit PCM, gentle high/low-pass filtering,
level balancing, overlap at loop boundaries, and staggered gasps/group concern.
The opening gasp is attenuated by a further 50% to balance the crowd background.
Rebuild with `tools/prepare_crowd_audio.py` and the four original previews.
The samples are converted to 48 kHz float once at startup, then mixed without
runtime decompression, audio-thread allocation, or per-spectator sound sources.

## Menu music

`menu-theme.wav` is the project owner's selected theme, supplied as
`tpv_theme_music.wav` and edited into the approved `tpv_theme_music_loop.wav`.
Stereo 48 kHz / 16-bit PCM; 50.726729 seconds. The loop omits the original
opening silence and joins matching passages with a 2.30575-second circular
crossfade. Pitch, tempo and the body outside that crossfade are unchanged.
The edit is reproducible with `tools/make_music_loop.py` and the original file.
