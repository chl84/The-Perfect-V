# The Perfect V — Development

## Standalone source checkout

The game requires CMake, a C++17 compiler, pkg-config, SDL3 3.4+, FreeType and glslc.
The runtime requires a Vulkan driver. Compiled SPIR-V shaders are embedded in the executable.
The compiled hill and pose tables are included in `src/`; gameplay does not read
external reference data or analysis captures.

```sh
cmake -S . -B build-vulkan -DCMAKE_BUILD_TYPE=Release
cmake --build build-vulkan --parallel 4
ctest --test-dir build-vulkan --output-on-failure
```

Without external fixtures, CTest registers eight standalone suites:

- `renderer-commands`: CPU-only validation of finite rendering arguments, stack balance, current attributes, terrain cache eviction, zero-size drawables, graphics resource lifetime and camera response at different frame cadences. It does not use a GPU or emulate rasterization.
- `environment`: manual/random choices, atmospheric lighting, preference/replay compatibility and fixed conditions through both rounds and all participants in World/Team Cup events.
- `presentation-timing`: frame deadlines without accumulated timer overshoot, recovery after a stall and camera smoothing independent of frame cadence.
- `camera-paths`: finished camera 2 rails on all 32 hills, continuous spline velocity, snow clearance and sightlines during normal jumps and falls with varying wind and frame cadence.
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
cmake -S . -B build-vulkan -DSJP_REFERENCE_TESTS=ON
cmake --build build-vulkan --parallel 4
ctest --test-dir build-vulkan --output-on-failure
```

These suites also require Python 3 and the separately retained reference and
capture fixtures: original hill coordinates, forces, joint poses, controls,
weather, AI, scoring, outrun, ranking and measured DOS trajectories. The fixture
directories, analysis reports and extraction/calibration utilities remain local
and are intentionally excluded from Git. Enabling the option does not download
or redistribute the original game. Keep a complete matching fixture set when
running these tests.

The explicit full-game `--smoke-test` checks native input flow, all 32 venues, competition
checkpoints and replay persistence without those external fixtures. Its extra
original-demo replay import check is conditional on the local sample file.
Use `--data-dir` with a temporary directory to keep personal records intact.
`--smoke-mouse-capture` additionally uses a visible fullscreen window, real
relative mouse mode and Ready/Next button hit testing. The eight standalone suites
and the standard Vulkan smoke test passed on 2026-10-13; the additional fullscreen
mouse-capture test has not been run. See [migration status](vulkan-sdl3-migration.md).

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
display copy. The contact calculation reuses the athlete's mesh without drawing
commands and accounts for terrain-grid crossings along its faces. Airborne models
that do not intersect snow receive no offset.

## Rendering resource limits

The terrain GPU mesh cache retains the two most recently used venues. Returning
to an evicted venue rebuilds its unchanged geometry. Static batching finishes before
the outer attribute restoration so cached scenery preserves the caller's current
color, normal and texture coordinates. Drawable dimensions are clamped to at least
one pixel to avoid invalid projections and UI scaling when a backend reports zero.

The CPU command test covers all 32 venues, three cameras, panorama, ghosts, HUD,
falls and autopilot trajectories. These checks do not establish GPU driver stability
or prove that a previously observed AMD graphics timeout is resolved.

The jumper casts one silhouette shadow from the displayed body and ski joints.
A bounded 96 by 64 CPU coverage mask combines their shapes before softening;
overlapping parts never accumulate opacity. Gaussian softening increases with
height above the snow and preserves the silhouette's coverage rather than adding
an oval underneath it. One non-overlapping mesh draws the result onto the existing
snow triangles, without new textures, shadow maps or offscreen rendering passes.
The CPU command suite checks overlap, ski gaps and height-dependent softening.

Normal play has an independent 60 FPS submission limit. Its deadlines advance
from a fixed clock schedule so timer wake-up delays do not accumulate; long
stalls discard missed deadlines. Camera smoothing uses elapsed presentation
time, retaining the previous 16 percent response per frame at 60 FPS. The
original 70 Hz physics and terrain geometry remain unchanged. The renderer uses
SDL3's GPU API with Vulkan selected explicitly, embedded SPIR-V shaders and at
most one frame in flight. `--graphics-compat` and `--sync-rendering` wait for each
submitted frame immediately. `--no-msaa` disables multisample antialiasing;
`--gpu-validation` enables SDL GPU/Vulkan validation explicitly. Actual graphics
settings are printed and flushed at startup. The smoke test is not frame limited.

An optional diagnostic executable exercises all 32 venues and a supplied replay
through the Vulkan renderer. It is never registered as an automatic test and
only reads supplied save files. The following commands are for later explicitly
authorized validation; no GPU probe was run during migration:

```sh
cmake -S . -B build-vulkan -DTPV_BUILD_RENDER_PROBE=ON
cmake --build build-vulkan --target render-probe --parallel 4
python3 tools/watch_gpu_test.py --output captures/vulkan-test --timeout 330 -- \
  build-vulkan/render-probe --assets assets --cup /path/to/cup.sjc \
  --replay /path/to/latest-dsj2.sjr --expect-renderer Vulkan \
  --samples 4 --seconds 300
```

The output directory must be new. The wrapper persists kernel events and GPU
metrics, bounds test duration, stops the test on a recorded GPU fault and attempts
to save any new device coredump. A driver hang may still affect the desktop before
the wrapper can stop it. `--sync` and `--samples 0` on the probe isolate frame
waiting and antialiasing. Vulkan uses the system's Vulkan driver selection;
OpenGL variables such as `AMD_DEBUG=usellvm` and `GALLIUM_DRIVER` do not select
this renderer's shader compiler or software backend.

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

## Time, weather and wind

Time and weather are cosmetic. Only the existing original wind affects physics,
AI, input, scoring or jump distance; cosmetic selection uses its own stateless
hash without advancing gameplay RNG. Day and sun are the defaults. Settings let
the player independently choose Morning, Day, Evening, Night or Random, and
Sunny/clear, Overcast, Snowfall, Fog or Random. The controls preview the chosen
lighting and save with the existing preferences.

Practice resolves random choices once per new jump. A cup resolves choices once
per event and retains them for all participants, team member groups and both
rounds. Its resolved conditions and selection policy survive checkpoint/resume.
The next event gets a fresh draw, including a second season on the same hill.
Random times are equally weighted. Random weather uses 45% sunny, 30% overcast,
15% snowfall and 10% fog. Native replays store the resolved conditions and cosmetic
seed, so seeking, camera changes and changed preferences cannot reselect weather.
Existing replay files and version 1/2 cup saves remain readable, defaulting their
missing cosmetic metadata to day and sun. Original .RPL exports retain their
original bytes and cannot carry this modern atmosphere metadata.

Four compact lighting presets set sun/moon direction, sky gradient, ambient and
directional light, fog color and visibility. Overcast, snow and fog reduce direct
light and soften the single silhouette shadow. Shadow projection uses the same
light direction and the local receiver tangent. A broad arena fill makes night
jumping readable. The sun/moon disc and a small fixed star field are projected as
background primitives when in view; clouds suppress them. These reuse the existing
two-light shader and fog blend, without offscreen passes, shadow maps, volumetric
clouds or new textures. Time and weather do not alter terrain, camera rails or
collision geometry, and changes reuse the cached terrain meshes.

The windsock, flakes and moving boughs still use the same smoothed x/y wind angle
as the HUD arrow. Snowfall uses the existing compact world-space volume: at most
225 candidate flakes, with only visible flakes drawn. Sizes and fall speeds vary,
with weak wind drift and a mostly downward fall. Soft volume and lifetime fades
prevent popping. Flake animation uses the frame/replay clock, so pause freezes it
and seeking is repeatable. Menus preview lighting/fog without snowfall particles.

## Camera 2 rails

Chase (camera 2 in the interface) follows a separate fixed spline for each of
the 32 hills. All rails are calculated at startup from the existing snow
triangles and inrun deck, with clearance and sightlines accounted for before
play. The original terrain vertices, profiles and physics are unchanged.

The camera moves along the finished rail using smoothed jumper progress and
aims at the jumper's upper body. Cubic interpolation keeps velocity continuous
through rail points. No terrain checks, ray tests or corrective height changes
run during a jump. The side and wide cameras retain their previous behavior.

The CPU tests independently sample the receiver planes and exercise normal
jumps, falls, headwind/tailwind, 30/144 FPS and irregular frame intervals on
all 32 hills. Renderer command tests check actual rail selection, frame cadence
and paused camera position. Native Vulkan captures and the full-game smoke
test cover the installed renderer.
