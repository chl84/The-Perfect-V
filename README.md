# The Perfect V

A native Linux 3D ski jumping game built with C++17, SDL3, Vulkan and FreeType.
It has 32 distinct hill profiles, a 70 Hz simulation, local World Cup and Team
Cup competitions, CPU opponents, saved records and replay playback. The scenery,
athlete, snow effects and sports interface use a modern stylized presentation.
The compact HUD keeps the venue, wind, speed and distance at the screen edges,
leaving the jumper clear of coaching text and progress meters.
Settings include morning, day, evening and night, with sunny, overcast, snowfall
and fog conditions. Day and sun are the defaults; time and weather are cosmetic.
Random choices stay fixed per practice jump and throughout each cup event.
Crowd audio fades in near the stands and grows with flight distance. Falls hush
the cheers and trigger gasps and concerned voices at the same distance-dependent
volume, including in replays. Recordings and credits are in [assets/audio](assets/audio/CREDITS.md).

The project reconstructs hill geometry and gameplay rules from Deluxe Ski Jump
2.10. The original game's executable, data, reference recordings and analysis
captures are not distributed in this repository and are not needed to play.

Vulkan/SDL3 is now the version on `main`. The standalone test suites and
the full-game Vulkan smoke test passed on Intel Iris Xe; long-session stability
and other GPUs remain unverified. See [migration notes](docs/vulkan-sdl3-migration.md).

## Build and play

Install a C++17 compiler, CMake 3.16 or newer, pkg-config and the development
packages for SDL3 3.4 or newer and FreeType, Vulkan drivers and `glslc` (shaderc).
Then run:

```sh
git clone https://github.com/chl84/The-Perfect-V.git
cd The-Perfect-V
cmake -S . -B build-vulkan -DCMAKE_BUILD_TYPE=Release
cmake --build build-vulkan --parallel 4
./play-vulkan.sh
```

The Vulkan launcher builds into `build-vulkan` if its executable is missing,
preserving any previous executable in `build`. Normal play requires
a graphical desktop; no browser, network connection or DOS emulator is needed.
Use **How to jump** in the game for a short guide. England K50 and Austria K70
are small practice hills.

**Ski Jumper** in the main menu selects Pro, the original athlete, or President,
a suited jumper with a red tie and swept golden hair. The saved choice changes
appearance only; both use the same skeleton, controls, physics and scoring.

The renderer uses SDL3's GPU API with Vulkan selected explicitly. It retains
the previous scene geometry, materials, cameras, interface and 4× antialiasing
where supported. One frame may be in flight, with VSync and an independent
60 FPS cap in normal play. To wait for each submitted frame immediately:

```sh
./play-vulkan.sh --graphics-compat
```

`--sync-rendering` enables the same frame wait. `--no-msaa` disables antialiasing;
`--gpu-validation` explicitly enables SDL GPU/Vulkan validation. Startup output
reports the GPU, SDL version, MSAA, synchronization and submission limits.

## Controls

Open **Settings > Controls** for the in-game control reference.

| Action | Mouse | Keyboard |
| --- | --- | --- |
| Leave the gate | Hold left button | Space |
| Take off | Hold both buttons near the board, then release | Space |
| Adjust flying position | Gentle vertical movement | Up / Down or W / S |
| Prepare telemark | One button, then the other before contact | T |
| Prepare two-foot landing | Both buttons together | B or Space in flight |
| Pause / resume | Pause menu | Esc |
| Change camera | Settings | C |
| Toggle ghost | Settings | G |
| Toggle sound | Settings | M |
| Toggle fullscreen | — | F11 |
| Watch replay | Results or Replays | R |

Release the start button during the inrun. Hold both buttons near the board to
extend through takeoff, and release both afterward to enable landing preparation.
Use small mouse movements in flight. A wider telemark improves style but increases
the risk of falling.

Replays support pause, timeline seeking, camera changes and 0.25×–2× speed.
Native `.PVR` recordings preserve the selected ski jumper, time of day and weather.
The replay library reads `.PVR` and original DSJ `.RPL` recordings. Drop a
replay onto the window or use `./play.sh --replay FILE`. Original replay export
is available when the recorded input sequence fits that format. Hover over either
export button for a short explanation of its format.

## Competitions and saves

Practice supports unlimited retries. World Cup accepts 1–16 human/CPU entrants;
Team Cup accepts 1–4 teams of four. CPU entrants receive random nationalities and
country-specific first/last names; CPU teams share one randomly selected nationality.
Names and countries stay fixed through the cup and save/resume. Flags replace country
codes in the interface; click a flag in cup setup to edit the nationality.

Select hills, seasons, CPU levels and whether
to watch CPU jumps. Local players share the machine, and interrupted competitions
can be resumed from checkpoints. Standing human jumps in practice and cups can set all-time hill records. The blue
spray line marks the saved record on each hill; CPU jumps and replays cannot update it.

On a local Linux installation, game data is grouped under
`~/.local/share/the-perfect-v/`: `assets/` contains installed resources and
`saves/` contains preferences, records, cups and replays. Saves respect
`XDG_DATA_HOME`; `--prefix` controls where resources are installed. Resource
installation never writes to `saves/`. The first default launch imports missing
files from the previous save directory once, preferring the most recent namespace
and retaining originals and existing destination files. The launcher prints the
active save location as `Saves:`.
Use `--data-dir PATH` to select another location without importing old saves.
Personal saves and replays are excluded from Git.

## Tests and packaging

```sh
ctest --test-dir build-vulkan --output-on-failure
./package.sh
```

A standalone checkout includes tests for simulation/render interpolation, physics
and storage, save migration, fall presentation and CPU rendering commands.
Additional fidelity tests require private local fixtures and are enabled
automatically when those fixtures are available. See
[development notes](docs/DEVELOPMENT.md).

`package.sh` creates `dist/ThePerfectV-linux-x86_64.tar.gz`. Extract it and run
`./play.sh`. The package includes the executable, fonts, icon and documentation;
it uses the host's SDL3, Vulkan driver, FreeType and C++ runtime libraries.
Shaders are embedded in the executable; `glslc` is required only when building.

For an automated full-game check, use an isolated save directory:

```sh
SDL_AUDIODRIVER=dummy ./build-vulkan/the-perfect-v --smoke-test \
  --data-dir /tmp/tpv-test-saves
```

Run this from a graphical desktop. `--capture-dir PATH` optionally saves test
screenshots. The original demo replay import check runs only when its local
fixture is present; native replay, cup and all 32 hill checks remain available
without it.

## Source layout

- `src/`: simulation, hill tables, controls, competition, storage, replay,
  athlete geometry, rendering, interface and generated audio.
- `assets/`: game icon and fonts.
- `tests/`: standalone and optional fidelity tests.
- `tools/`: native trajectory/replay probes and comparison utilities.
- `docs/DEVELOPMENT.md`: build and verification details.

Fonts are DejaVu Sans, distributed with their
[license](assets/fonts/LICENSE-DejaVu.txt). Deluxe Ski Jump remains the work of
its original author; its program and resources are not included here.
