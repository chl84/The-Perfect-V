# Vulkan and SDL3 migration

The previous game state was committed and pushed to `origin/main` as `7cd871e`
before migration began. Implementation from `vulkan-sdl3` was promoted to `main`
on 2026-10-13 after the build and runtime checks below passed. The new executable
builds in `build-vulkan`; the existing executable in `build` and its desktop
shortcut remain available. A local binary copy and source hashes are retained
in the ignored `captures/vulkan-migration/` directory.

## Validation on 2026-10-13

Commit `b73d016` was built in Release mode and installed locally with SDL 3.4.16.
All five standalone CTest suites passed. After the user authorized runtime testing,
the installed executable passed `--smoke-test` on Intel Iris Xe Graphics (ADL GT2)
using Vulkan, 4× MSAA, one frame in flight and the normal asynchronous submission
mode. The smoke test disables the normal 60 FPS cap and used dummy audio and an
isolated temporary save directory.

The test covered all 32 venues, menus, input, native replay, a 64-jump World Cup
and Team Cup/checkpoint flows. The optional original replay fixture was absent,
so its import check was skipped. Captured menu, flight, replay and final standings
images were visually inspected without apparent rendering defects. No GPU faults
were recorded in the kernel journal during the test. Logs and screenshots are
retained locally in the ignored `captures/vulkan-runtime-2026-10-13/` directory.
The user also reported that normal gameplay graphics worked correctly.

This does not establish image equivalence with the OpenGL checkpoint, long-session
stability, audio behavior or a fix for the previous AMD GPU/system resets. Other
GPUs, the fullscreen mouse-capture test and the extended render probe remain
unverified.

## Renderer

SDL3's [GPU API](https://wiki.libsdl.org/SDL3/CategoryGPU) is initialized with
SPIR-V shaders and the driver name `vulkan`. The selected driver is checked.
The game executable no longer creates an OpenGL context or links SDL2/OpenGL.

The existing scene construction code issues game-local drawing commands.
`draw_commands.cpp` records vertex attributes, matrices, lighting, material and
fog state in their existing order. The geometry builders retain the same hill,
athlete, shadow, weather, scenery and interface calculations. Cached terrain
meshes keep the existing two-venue limit. Cached commands replay against the
current camera and lights, and temporary CPU vertices are released after upload.

`vulkan_backend.cpp` submits these packets through SDL3's Vulkan backend. The
embedded vertex/fragment shaders reproduce smooth vertex colors, two directional
lights, material highlights/emission, linear fog and modulated textures. The
existing projection is converted to Vulkan-compatible depth through SDL's GPU
coordinate convention. Lines are expanded to triangles so HUD widths do not
depend on optional Vulkan wide-line support.

The renderer retains 4× MSAA when supported by both color and depth formats,
with a reported fallback to one sample. It prefers 24-bit depth and falls back
to 32-bit float depth when necessary. Rendering resolves to an opaque RGBA8 target;
presentation and screenshot readback use that same image. Screenshots remain
top-down PPM files.

One frame may be in flight. Its fence completes before dynamic buffers, staging
data or frame targets are reused. Static resources are retained by pending
packets; SDL defers releasing GPU resources still referenced by submitted work.
Resize recreates frame targets after the previous frame completes. Construction
failure, partial uploads, command cancellation and device/window teardown have
explicit cleanup paths. Normal play retains its independent 60 FPS limit.

## SDL3 and original gameplay

Window creation, pixel dimensions, fullscreen, keyboard/mouse events, text
input, file drops, relative mouse mode and initialization return values use
native SDL3 APIs. Fractional relative motion is accumulated and converted to
the original controller's integer counts. Sensitivity remains in physics, with
system acceleration disabled as before.

Audio now uses an SDL3 audio stream. The original mono 48 kHz noise/tone
generator, cue lengths, waveform, amplitude and atomic cue protection are
retained; fixed blocks avoid allocation in the audio callback.

The protected baseline contains 29 source/data files covering physics, original
hill/pose/control/weather/AI/scoring calculations, competition and storage.
Their SHA-256 values were unchanged at the migration checkpoint, which retained
the previous save namespace. The current default groups resources and saves
under `the-perfect-v`, with a one-time import of previous saves; see
[save compatibility](DEVELOPMENT.md#save-compatibility).

A static comparison of the athlete, effects, environment and batching sources
matches the checkpoint after normalizing the API renaming, includes and
comments. The linked executable depends on SDL3 and FreeType, with no direct
SDL2/OpenGL dependency. These are source/build checks, not runtime validation.

The old CPU rendering audit is adapted to the same drawing interface and still
builds without initializing a GPU. Its coverage concerns scene construction,
not shader execution or Vulkan synchronization. The fidelity suites are retained
for a later validation phase.

## Build and launch

Build tools: C++17, CMake, pkg-config, SDL3 3.4+, FreeType and `glslc`.
The compiler embeds both SPIR-V shaders in the executable. A Vulkan driver is
required at runtime, and a shader compiler is not.

```sh
cmake -S . -B build-vulkan -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-vulkan --parallel 4
```

`play-vulkan.sh` selects this separate build. The existing `play.sh` continues
to use its existing `build/the-perfect-v` while that binary is present.
`package.sh` builds the Vulkan version in `build-vulkan` and includes the
embedded shaders, normal launcher, assets and documentation.

Vulkan is the new build's default renderer. `--graphics-compat` and
`--sync-rendering` wait for each submitted frame immediately; LLVM/ACO OpenGL
shader selection is no longer part of these switches. `--no-msaa` and
`--gpu-validation` remain explicit diagnostic choices. The optional native
render probe also builds, but is never run by CMake or registered with CTest.

The locally installed Vulkan version has its own desktop entry. Remaining
validation includes image comparisons against the checkpoint, the fullscreen
mouse-capture test and longer stability/performance sessions with kernel logging.
