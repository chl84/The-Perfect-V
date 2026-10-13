# The Perfect V — Development

## Standalone source checkout

The game requires CMake, a C++17 compiler, pkg-config, SDL2, OpenGL and FreeType.
The compiled hill and pose tables are included in `src/`; gameplay does not read
external reference data or analysis captures.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Without external fixtures, CTest registers five standalone suites:

- `renderer-commands`: CPU-only validation of finite rendering arguments, stack balance, current attributes, terrain cache eviction, zero-size drawables and graphics resource lifetime. It does not use a GPU or emulate rasterization.
- `render-interpolation`: presentation interpolation between simulation ticks.
- `fall-presentation`: deterministic display contact, unchanged physics/poses and no GL calls in the contact calculation.
- `physics-and-storage`: native jumps on all 32 hills, controls, scoring and storage.
- `storage-migration`: stable hill identifiers and compatibility with older saves.

`BUILD_TESTING=OFF` builds only the game and its simulation library.

## Optional local fidelity verification

`SJP_REFERENCE_TESTS` enables the additional original-game regression suites.
It defaults to ON only when the local demo replay and hill fixtures are present.
A standalone checkout defaults to OFF and does not require Python.

```sh
cmake -S . -B build -DSJP_REFERENCE_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

These suites also require Python 3 and the separately retained reference and
capture fixtures: original hill coordinates, forces, joint poses, controls,
weather, AI, scoring, outrun, ranking and measured DOS trajectories. The fixture
directories, analysis reports and extraction/calibration utilities remain local
and are intentionally excluded from Git. Enabling the option does not download
or redistribute the original game. Keep a complete matching fixture set when
running these tests.

The full-game `--smoke-test` checks native input flow, all 32 venues, competition
checkpoints and replay persistence without those external fixtures. Its extra
original-demo replay import check is conditional on the local sample file.
Use `--data-dir` with a temporary directory to keep personal records intact.

## Packaging

`./package.sh` builds Release and assembles a fresh Linux archive with the game,
launcher, fonts, icon, README and these development notes. Original files, analysis
captures, local desktop shortcuts and personal saves are excluded. The runtime
libraries come from the destination Linux installation.

## Repository scope

Track source code, required assets with their license, tests, reusable native
verification tools, portable documentation and build/launch/package scripts.
Do not track reference programs, recordings, capture exports, build directories,
packages, personal saves, machine-specific shortcuts, caches or local credentials.

## Prepared snow appearance

The prepared snow strip fades into ordinary snow after the bottom of each
hill's outrun. Only its material, grooming pattern and color change. The
original height grid, surface contours, collisions, simulation and cameras
remain unchanged. Ordinary scenery snow has grain without grooming stripes.
Both textures have averaged mip levels; the material blend reuses the existing
terrain triangles with depth writes disabled and polygon offset.

## Fall model visibility

Crashed outrun/replay models receive a presentation-only vertical offset when
the actual suit, helmet, gloves, boots or skis intersect the existing rendered
snow triangles. Joint angles, equipment geometry, physical state and start
position remain unchanged. The offset is calculated directly from the displayed
pose, including interpolated replay joints, so pause and seeking are deterministic.

Camera tracking, input, scoring, collision, replay storage and the terrain use
the original Frame. Only athlete, shadow and cosmetic snow drawing receive the
display copy. The contact calculation reuses the athlete's mesh without OpenGL
calls and accounts for terrain-grid crossings along its faces. Airborne models
that do not intersect snow receive no offset.

## Rendering resource limits

The terrain display-list cache retains the two most recently used venues. Returning
to an evicted venue rebuilds its unchanged geometry. Static batching finishes before
the outer attribute restoration so cached scenery preserves the caller's current
color, normal and texture coordinates. Drawable dimensions are clamped to at least
one pixel to avoid invalid projections and UI scaling when a backend reports zero.

The CPU command test covers all 32 venues, three cameras, panorama, ghosts, HUD,
falls and autopilot trajectories. These checks do not establish GPU driver stability
or prove that a previously observed AMD graphics timeout is resolved.

Normal play has an independent 60 FPS submission limit. `--graphics-compat`
selects AMD's LLVM shader path before SDL initializes video and calls `glFinish`
before each swap. `--sync-rendering` enables just that synchronization;
`--no-msaa` disables multisample antialiasing independently. These options do not
change simulation, hill geometry, saved data or replay formats. Shader flags are
set only in the game process. Actual graphics settings are printed and flushed at
startup. The smoke test is not frame limited.

An optional diagnostic executable exercises all 32 venues and the affected
replay through the real OpenGL driver. It is never registered as an automatic
test and only reads the supplied save files:

```sh
cmake -S . -B build-diagnostics -DTPV_BUILD_RENDER_PROBE=ON
cmake --build build-diagnostics --target render-probe --parallel 4
AMD_DEBUG=usellvm SDL_VIDEODRIVER=offscreen \
  python3 tools/watch_gpu_test.py --output captures/llvm-test --timeout 330 -- \
  build-diagnostics/render-probe --assets assets --cup /path/to/cup.sjc \
  --replay /path/to/latest-dsj2.sjr --expect-renderer radeonsi \
  --expect-compiler LLVM --samples 4 --seconds 300
```

The output directory must be new. The wrapper persists kernel events and GPU
metrics, bounds test duration, stops the test on a recorded GPU fault and attempts
to save any new device coredump. A driver hang may still affect the desktop before
the wrapper can stop it. `--sync` and `--samples 0` on the probe isolate the two
other rendering variants. For software control tests, set `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`
and pass `--expect-renderer llvmpipe` to the probe instead of the hardware
renderer/compiler expectations.

The physics suite also replays the exact World Cup AI state that reached the
takeoff lip at `x=0`. The following positive step must launch the athlete; missing
that crossing previously left the CPU indefinitely in the inrun and caused the
frontend's simulation limit to abort the cup.

## Minimal jump HUD

Venue, K point, wind, speed and distance sit in compact panels at the screen edges.
The start countdown and a short start hint appear at the gate; competition context
appears only during cup jumps. Inrun progress, flight-position meters and ongoing
coaching text are omitted. Flight controls and physics remain unchanged; control
instructions remain on the Help screen. Outrun distance shows the measured landing
result rather than continuing to count travel after landing.

## Save compatibility

The display name, executable and repository use The Perfect V. The internal SDL
save namespace intentionally remains unchanged so existing records, settings,
competitions and replay files stay available. The executable prints the actual
save directory at startup; `--data-dir PATH` overrides it. Native `.SJR` replay
and `.SJC` competition formats also retain their identifiers for compatibility.

## Cosmetic wind and snowfall

The windsock uses the same smoothed x/y wind angle as the HUD arrow, with light
cloth sag and flutter. Snow and moving boughs use that direction as well. These
vectors are drawing inputs and are never fed back into the simulation.

Light snowfall is selected once per jump with approximately 15 percent probability,
using a cosmetic hash of the existing jump seed without advancing gameplay RNG.
A selected jump keeps snowfall throughout; at most 100 atmospheric flakes are
rendered, with weak wind drift and a mostly downward fall. Their animation uses
the frame/replay clock, so pause freezes flakes and seeking is repeatable. Menus
do not display jump snowfall. Original replays reuse their seed; native recordings
without original metadata use a stable visual hash of their recorded frames.
