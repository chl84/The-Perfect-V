# Vulkan and SDL3 migration

The previous game state was committed and pushed to `origin/main` as `7cd871e`
before migration began. Implementation is on `vulkan-sdl3`. The new executable
builds in `build-vulkan`; the existing executable in `build` and its desktop
shortcut remain available. A local binary copy and source hashes are retained
in the ignored `captures/vulkan-migration/` directory.

The migration has been implemented and compiled. No game sessions, GPU probes,
CTest suites or visual comparisons have been run after the request to stop
testing. Compilation does not establish image equivalence, performance or a
fix for the previous GPU/system resets.

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
Their SHA-256 values are unchanged. Save directories and identifiers remain
`SkiJumpPro/SkiJumpPro`; record, replay and cup formats are unchanged.

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

Before replacing the normal desktop launch, the next validation phase needs
image comparisons against the checkpoint, normal controls and replay, a full
World/Team Cup and a bounded stability/performance session with kernel logging.
That phase awaits the user's decision to resume testing.
