# The Perfect V

A native Linux ski jumping game with 32 hills, local competitions and four distinct
jumpers. Built with C++17, SDL3 and Vulkan.

![Pro in flight above a snowy valley in daylight](docs/screenshots/daylight-flight.webp)

## Features

- **32 hills**, from small practice venues to ski flying.
- **Practice, World Cup and Team Cup**, with local players and CPU opponents.
- **Four jumpers:** Pro, President, Actor and Cartoon. Different looks, identical physics.
- **Changing conditions:** morning, day, evening and night, with clear skies, clouds, snow or fog.
- **Replays, personal records and resumable cups.**

## Get started

The game needs a graphical Linux desktop, a Vulkan-capable GPU and driver, SDL3
3.4 or newer, and FreeType.

### From a game package

Extract `ThePerfectV-linux-x86_64.tar.gz`, enter the extracted folder and run:

```sh
./play.sh
```

### Build from source

Install a C++17 compiler, CMake 3.16 or newer, pkg-config, the SDL3 and FreeType
development packages, and `glslc` from shaderc. Then:

```sh
git clone https://github.com/chl84/The-Perfect-V.git
cd The-Perfect-V
cmake -S . -B build-vulkan -DCMAKE_BUILD_TYPE=Release
cmake --build build-vulkan --parallel 4
./play-vulkan.sh
```

## How to jump

Start with **Practice** on England K50 or Austria K70. The in-game guide is under
**Settings → Controls**.

| Action | Mouse | Keyboard |
| --- | --- | --- |
| Leave the gate | Hold left button | Space |
| Take off | Hold both buttons near the board, then release | Space |
| Adjust flying position | Gentle vertical movement | Up / Down or W / S |
| Telemark landing | One button, then the other before contact | T |
| Two-foot landing | Both buttons together | B or Space in flight |
| Pause / resume | — | Esc |

Release the start button during the inrun. Time the takeoff near the end of the
ramp, then release both buttons before preparing your landing.

**Shortcuts:** C changes camera, G toggles the ghost during a jump, M toggles sound,
F11 toggles fullscreen, and R opens the replay from the menu or results.

## Choose your jumper

Select **Ski Jumper** from the main menu. Each character has its own appearance
and ski design; your choice never changes jump performance or scoring.

![Pro, President, Actor and Cartoon on the Ski Jumper selection screen](docs/screenshots/ski-jumpers.webp)

## Replays and saves

Replays support pause, timeline seeking, three cameras and 0.25×–2× playback.
**PVR** preserves the jump, character, time of day and weather. Original DSJ
**RPL** files can also be opened, and exported when the recording supports it.
Hover over the export buttons for a short explanation. Open a replay through
**Replays**, drop it onto the window, or use `./play-vulkan.sh --replay FILE`
(`./play.sh --replay FILE` in the packaged game).

![Cartoon flying through falling snow under the floodlights in Slovenia](docs/screenshots/night-jump.webp)

The default save folder is `~/.local/share/the-perfect-v/saves/`, respecting
`XDG_DATA_HOME`. It holds settings, records, cups and replays. Interrupted cups
can be resumed; resource updates leave saves intact. Use `--data-dir PATH` for
a separate save folder. Personal saves and replays are excluded from Git.

## Development

```sh
ctest --test-dir build-vulkan --output-on-failure
./package.sh
```

Packaging requires a Git checkout and creates `dist/ThePerfectV-linux-x86_64.tar.gz`
with the game, resources, documentation, license and matching `source.tar.gz`.
See [development notes](docs/DEVELOPMENT.md) for diagnostics, rendering options,
full-game checks and the source layout.

## License and credits

Copyright (C) 2026 chl84 and contributors. Original code, documentation and
resources are licensed under [GPL-3.0-only](LICENSE), without warranty.
Third-party resources retain their own terms:
[fonts](assets/fonts/LICENSE-DejaVu.txt), [audio](assets/audio/CREDITS.md),
[mountain panorama](assets/environment/CREDITS.md) and [flags](assets/flags/CREDITS.md).

The hills and gameplay rules are reconstructed from Deluxe Ski Jump 2.10.
Original game files are neither included nor required. Deluxe Ski Jump belongs
to its original author; this project's license grants no rights to that game.
