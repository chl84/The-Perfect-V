# The Perfect V — Development

## Standalone source checkout

The game requires CMake, a C++17 compiler, pkg-config, SDL3 3.4+, FreeType and either glslc or glslangValidator.
The runtime requires a Vulkan driver. Compiled SPIR-V shaders are embedded in the executable.
The compiled hill and pose tables are included in `src/`; gameplay does not read
external reference data or analysis captures.

```sh
cmake -S . -B build-vulkan -DCMAKE_BUILD_TYPE=Release
cmake --build build-vulkan --parallel 4
ctest --test-dir build-vulkan --output-on-failure
cmake --install build-vulkan --prefix "$HOME/.local"
```

For local development on this machine, always complete a game build by installing
it to `~/.local`. The desktop shortcut runs `~/.local/bin/the-perfect-v-vulkan`,
which points to `~/.local/bin/the-perfect-v`. Check that this executable matches
`build-vulkan/the-perfect-v` before reporting the build as updated. Installation
updates game resources without replacing personal saves.

Without external fixtures, CTest registers fifteen standalone suites, including:

- `renderer-commands`: CPU-only validation of finite rendering arguments, stack balance, current attributes, terrain cache eviction, zero-size drawables, graphics resource lifetime and camera response at different frame cadences. It does not use a GPU or emulate rasterization.
- `environment`: manual/random choices, atmospheric lighting, preference/replay compatibility and fixed conditions through both rounds and all participants in World/Team Cup events.
- `presentation-timing`: frame deadlines without accumulated timer overshoot, recovery after a stall and camera smoothing independent of frame cadence.
- `camera-paths`: finished camera 2 rails on all 32 hills, continuous spline velocity, snow clearance and sightlines during normal jumps and falls with varying wind and frame cadence.
- `render-interpolation`: presentation interpolation between simulation ticks.
- `fall-presentation`: deterministic display contact, unchanged physics/poses and no GL calls in the contact calculation.
- `physics-and-storage`: native jumps on all 32 hills, controls, scoring and storage.
- `storage-migration`: stable hill identifiers and compatibility with older saves.
- `platform-files`: replacing existing saves and preserving data after a failed replacement, including Unicode paths on Windows.
- `crowd-audio`: approach/flight envelopes on all hills, distant and nearby falls, replay seeking/speed, one-shot reactions, smooth fades, valid sample mixing and the sound toggle through a dummy SDL audio device.
- `mesh-geometry`: exact attribute sharing, preserved triangle order, bounded ranges, camera transforms and conservative near/side/far clipping without initializing a GPU.
- `gpu-frames`: the real recorder and Vulkan submission code against a deterministic CPU queue. Checks two-frame resource reuse, resize, capture/present, unavailable swapchains, opaque/transparent pipelines, asynchronous mesh installation, preview/shadow invalidation and resource cleanup without a GPU.

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
relative mouse mode and Ready/Next button hit testing. The standalone suites
and the standard Vulkan smoke test passed on 2026-10-13; the additional fullscreen
mouse-capture test has not been run. See [migration status](vulkan-sdl3-migration.md).

## Windows builds

`.github/workflows/windows.yml` compiles portable Vulkan shaders on Ubuntu, then
builds and tests the Windows x86_64 game with Visual Studio 2022. The pinned SDL3
3.4.16 and FreeType 2.14.1 sources are SHA-256 checked by CMake. FreeType is static
with optional compression/shaping libraries disabled; SDL3 is the only shipped
DLL. The MSVC runtime is static in the executable and SDL3 DLL.

`package-windows.ps1` performs the build, standalone tests and installation; it
accepts `-ShaderDirectory` pointing at the `Windows-shaders` Actions artifact.
The game can also be built directly with CMake, an SDL3/FreeType prefix and an
installed glslc or glslangValidator. `TPV_PRECOMPILED_SHADER_DIR` optionally supplies
the two generated C++ shader initializers instead of a local shader compiler.

The ZIP contains `ThePerfectV/the-perfect-v.exe`, SDL3.dll, assets, licenses and
matching project/dependency source under `docs/source/`. Extract the entire ZIP.
The game uses `%APPDATA%/the-perfect-v/saves/`; `--data-dir` overrides this.
Its manifest enables UTF-8 paths on supported Windows 10/11 versions. Completed
save files use Windows replacement semantics without deleting the old save first.

A second Windows runner downloads and extracts the ZIP, checks its checksum and
runs the full game smoke test in a directory with spaces and Unicode. Chrome's
software Vulkan driver is used only by CI and is excluded from the game package.
Actual GPU drivers still require desktop testing. No workflow publishes releases
automatically.

## macOS builds

`.github/workflows/macos.yml` builds a universal `arm64` + `x86_64` app on macOS
15 with Apple Clang. The minimum deployment target is macOS 12.0, matching the
bundled MoltenVK 1.4.2 requirement. SDL3 3.4.16 and FreeType 2.14.1 are built from
SHA-256 checked sources for both architectures; FreeType is static. The release
MoltenVK archive is SHA-256 checked and its universal dylib is bundled alongside
SDL3. The existing SPIR-V shaders and Vulkan renderer are used through Metal;
there is no separate macOS rendering implementation.
Apple Clang uses `-ffp-contract=off` to preserve the original separate-operation
rounding: fused grid increments otherwise add an extra snow row on England K50.
The Intel game defaults `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0` before SDL
initialization. MoltenVK 1.4.2's indirect argument encoder query aborts inside
Metal on the Intel runner's paravirtual GPU; conventional descriptor binding
passes the full game test. An explicitly supplied environment value takes
precedence. Apple Silicon retains MoltenVK's default binding path.

On a Mac with Xcode command-line tools, CMake 3.24+, Python 3.11+ and a shader
compiler, run `./package-macos.sh`. Alternatively, pass the directory containing
the two generated shader initializers as its first argument. The workflow
compiles these portable shaders on Ubuntu, so no Vulkan SDK is needed on the Mac.

The package contains `The Perfect V.app`, with the executable under
`Contents/MacOS`, dylibs under `Contents/Frameworks`, and assets, documentation,
licenses and source under `Contents/Resources`. SDL's bundle resource path finds
assets independently of the working directory. An executable-relative rpath
finds SDL3, and SDL loads the bundled MoltenVK directly without a Vulkan loader.
Packaging checks both architectures and rejects unbundled dylib dependencies.
All binaries and the finished bundle are ad-hoc signed; the signature is verified
again after extraction. Developer ID signing and Apple notarization are not
configured. Gatekeeper may require first-launch approval in Privacy & Security.

Fresh macOS runners execute the fifteen standalone suites on both architectures,
verify the package in a path with spaces and Unicode, load MoltenVK, and run the
executable's help command. The full packaged graphics smoke test runs when Metal
is available; runners without a GPU explicitly report the graphics check as
untested. These checks do not establish performance or hardware compatibility
on a physical Mac. The default save path is
`~/Library/Application Support/the-perfect-v/saves/`; `--data-dir` overrides it.
For workflow dispatch, `package_run_id` optionally retests a previous run's
artifacts without rebuilding. Normal PR, main and tag builds always compile the
current source. A graphics failure reruns under LLDB to record all thread stacks.

## AppImage builds

The distributable is a single x86_64 AppImage. GitHub Actions
(`.github/workflows/appimage.yml`) builds on Ubuntu 22.04 with GCC 11, runs the
standalone suites and the packaged game's full Vulkan smoke test, then runs the
AppImage on fresh Ubuntu 22.04 and 24.04 runners. GPU tests use Xvfb and Mesa
Lavapipe. These verify packaging and software Vulkan; hardware/Wayland driver
coverage still requires testing on actual desktops. Workflow dispatch, pull
requests, main-branch pushes and `v*` tags can build artifacts; none publish a
GitHub release automatically.

The workflow installs build prerequisites and calls `./package-appimage.sh`
(`./package.sh` is an alias). It builds pinned SDL 3.4.16 from source and uses
appimagetool 1.9.1 with the static 20251108 type-2 runtime. Download URLs and SHA-256
checksums are in `packaging/appimage/dependencies.json`. The builder verifies all
downloads, packages SDL/FreeType and X11/Wayland client libraries with their
transitive dependencies and license notices, and rejects ELF symbol requirements
above glibc 2.35 or GLIBCXX 3.4.29. Host glibc, the GCC C++ runtime, Vulkan
loader/GPU drivers and audio services are used rather than shipping host-specific
drivers. X11 and Wayland are enabled; SDL libdecor is disabled to avoid tying
window decorations to a bundled GTK stack.

Outputs in `dist/` are `ThePerfectV-<version>-x86_64.AppImage`, its SHA-256 checksum,
and `AppImage-build.json` (actual compiler, required symbol versions and bundled
libraries). `APP_VERSION` overrides the default Git description. Actions uploads
these files for 30 days. There is no separate tar/zip game distribution.

`AppRun` resolves assets inside the image and preserves `--data-dir` and the normal
`the-perfect-v/saves` location. The runtime supports `--appimage-extract-and-run`
for systems where FUSE mounting is unavailable. `--license` shows GPL-3.0;
`--export-source FILE` copies the matching project source archive out of the image.
The SDL source archive and third-party license notices are embedded beside it.
The source snapshot includes working-tree changes and the build/workflow recipes;
ignored reference data, captures, credentials and personal saves are excluded.

The normal CMake installation remains available and installs the license, README
and screenshots alongside the resources. It leaves saves intact.

## Repository scope

Track source code, required assets with their license, tests, reusable native
verification tools, portable documentation and build/launch/package scripts.
Do not track reference programs, recordings, capture exports, build directories,
packages, personal saves, machine-specific shortcuts, caches or local credentials.

## Prepared snow appearance

The prepared snow strip fades into ordinary snow after the bottom of each
hill's outrun. Only its material, grooming pattern and color change. The
original analytic profiles, collisions and simulation remain unchanged.
Ordinary scenery snow has grain without grooming stripes.
Both textures have averaged mip levels; the material blend reuses the existing
terrain triangles with depth writes disabled and polygon offset.

`visual_snow_mesh.h` defines a fixed, four-way longitudinal refinement of the
ski corridor. The central terrain rows at Z ±10×hill scale use 0.75×scale
spacing instead of 3×scale, from the cell crossing the takeoff through the
outrun and its eight-scaled-metre margin. Transition fans join this strip to
the original coarse rows at Z ±20×scale with matching edges and no T-junctions.
The inrun retains its ±2.3 m width, with 0.5×scale spacing instead of 2×scale;
snow, grooves, fascia and underlying stringers follow the same profile samples.
Fine inrun panels retain the coarse material groups to avoid multiplying draw
calls. All geometry is still built once in the existing bounded venue cache.
The height evaluators in `visual_terrain_profile.h` and the mesh helpers are
CPU-only; they do not include SDL or graphics-command headers. The camera and
mesh test targets therefore build without graphics include paths.

Snow layers, camera clearance, fall contact, scenery decals, athlete shadows
and distance/record markers share this receiver topology. No skier positions,
joint angles, Hill functions, inputs, RNG or replay data are changed.
Camera 2 retains its established rail, built from the previous conservative
coarse envelope and its spare clearance, so refinement does not reframe a jump.
`snow-mesh` checks all 32 hills for stitched edges, receiver agreement, geometry
bounds and a maximum 8.6 mm curve error in the refined landing corridor.

## Inrun structures and lighting

The structural assembly surrounds the original inrun: continuous steel
stringers, transverse girders, terrain-embedded footings and braced piers.
Pier height follows the existing snow triangles, with no reversed columns on
earth-supported sections. Service stairs, close-spaced rail posts, takeoff
access openings and supported cabin landings remain outside the ski grooves,
on the -Z side only. The +Z side facing camera 1 has no service deck, stairs,
railings or inrun lamps; the underlying load-bearing structure remains.
The original analytic snow profile, groove placement, start seat and physics
coordinates are retained. The command suite checks snow nodes against these
profiles and freezes the refined terrain/inrun vertex streams on all 32 venues.

A narrow 16 cm transverse marker follows the existing snow mesh at the takeoff
edge. Red K signs retain their original X/Z coordinates; their upright panels
clear the uphill snow by at least 35 cm, with posts embedded in the snow mesh.

Every takeoff has one shielded lamp on the far service side. Longer or tall
structures receive extra lamps along that side, up to five in total. Lamp faces
share their positions/aim with the real vertex lights, with lens normals facing
the spotlight direction. Illumination fades by cone and finite range.
Day disables illumination, with low morning/evening power and fuller night light.
The two adjacent columns require at most two inrun or four arena local-source
evaluations per vertex; the fields have disjoint bounds. Far forest/terrain
vertices skip the inrun field entirely. Cached meshes and light layouts remain
bounded and reuse the existing pass, without extra textures or shadow maps.

## Grandstand terraces

Spectator landings are horizontal in both directions. Steep sections use
shorter landings; concrete bases embed in the existing snow mesh without
altering terrain or advertising boards. Guards follow landing heights at the
rear, exposed front edges and substantial level changes.

Each venue's layout is built once. Static bodies and animated arms use the
same cached landing and keep both shoe soles inside its wooden surface.
Adult heights vary from 1.60 to 1.90 m while preserving shoe width and crowd
density. The command suite checks actual floor and shoe vertices on all venues.

Concrete uses one shared 128×128 luminance texture with mipmaps (about 85 KiB
of GPU texels), providing restrained pores, mottling, runoff and formwork joints.
Stable cosmetic hashes add slight differences between slabs; vertex colors
darken their lower portions. All concrete is grouped by material, with section
boundaries retained for culling, rather than switching textures per seat row.
This adds a texture sample on concrete but no triangles, extra surface layers
or changes to landing/spectator positions. Terrain and physics remain unchanged.
Native Vulkan captures cover five hill sizes in day, snowfall, night and evening
at 1920×1080/MSAA4; the environment and renderer command suites pass.

## Crowd audio

The spectator bed and cheering loop follow the athlete's proximity to the actual
stand positions, with a smooth arrival envelope and increasing excitement up to
a long jump. The stand coordinates are shared with the visual layout. They are
cached per venue; no per-person sources or new simulation behavior are involved.

A fall hushes cheers to a quiet background with a 25 ms smoothing time and triggers
a restrained sequence of gasps and concerned voices. Subdued crowd conversation
continues underneath. There is no minimum reaction volume: early falls
outside hearing range are silent, and intermediate distances attenuate the
reaction. The general crowd bed remains audible at reduced volume after the fall.

Recorded crash/landing times restore the correct reaction position after replay
seeks. Pausing fades the audio, resuming synchronizes it with the athlete, and
replay speed controls sample playback. New jumps/menu changes clear reactions;
the result screen allows an unfinished reaction to finish and fades ambience.

Three WAV assets based on CC0 recordings occupy about 1.5 MiB on disk and 6 MiB as mono 48 kHz float
samples. Loading/conversion happens once at startup. The audio callback mixes
fixed blocks without allocation, disk access or application locks. Source
credits and the reproducible preparation script accompany the assets.

## Fall model visibility

The main menu's Ski Jumper screen uses the existing panels, buttons and four native
3D previews. Pro keeps its competition suit and helmet. President adds a navy suit,
white shirt, red tie, bare hands and a helmet-free face with swept golden hair.
President's reference-based face uses a continuous cheek/jaw mesh, a tapering
chin, narrow blue-grey eyes, heavy lids, pale brows and an integrated broad nose.
Thin closed lips form a restrained asymmetric smile; surface ribbons add fine
forehead/cheek folds without floating cylindrical strokes.
A rounded jacket mesh adds a prominent belly and wider waist; shirt,
lapels and tie follow its surface through the original poses. The belly and hem
hang below the hips without reducing the jacket's cross-sections. The longer
fringe pivots upright just after takeoff and settles as outrun speed drops;
its position comes directly from the displayed frame, so replay pause/seek is
deterministic and the menu preview keeps the swept fringe.
Actor adds a lean bare torso/limbs, short body-hair ribbons, a bright green
one-piece swimsuit, a dark curly cap, thick moustache, smile, golden aviator
sunglasses and a wristwatch. The opaque swimsuit covers the pelvis and wraps
over both shoulders; its straps and body hair follow the original poses.
The hairstyle uses a wavy shell and a few coarse curls instead of individual hairs.

Cartoon adds a purple embroidered dress, striped puff sleeves, a laced bodice,
large green eyes and a golden side-parted hairstyle. A continuous golden bundle
reaches the ski plane and coils along the ground while standing. Speed lifts
the lengths behind the head along the airflow, with travelling waves driven by
recorded elapsed time and wind. The original/interpolated Frame determines the
complete pose, so pause and replay seeking reproduce the same flutter. The menu
uses its presentation clock for a gentle breeze. The fall-contact height cache
also covers the extended hair, without changing terrain or physical collision.

All skins wrap the exact same original skeleton, including interpolated joints
and fall poses; ski outlines and binding positions retain their geometry. All wear the same
light boot shells with red closures, metal buckles and dark flat soles, making
the boots distinct from the trousers and colored bindings. Boot cuffs follow
the lower legs, with trouser hems tucked inside instead of covering the shells.
The appearance
enum stays outside simulation, AI, input, scoring and replay physics. Native
replays preserve their recorded skin independently of the current preference;
ghosts use the currently selected skin.

The optional `JUMPER 1` preferences tag preserves the choice while retaining the
existing records format; older saves default to Pro. The fall-contact display
correction measures the selected skin and only changes its temporary render copy.
Preview lights and transforms are scoped to the menu draw and restored afterward.

Skis use saturated decks and dark rails for contrast against snow: Pro has orange/navy
racing bands, President blue with red bands and a gold pinstripe, Actor green/lime
chevrons, and Cartoon purple/pink with gold diamonds. Both deck and underside use
adjacent colored panels on the original surface, without layered decals or textures.
Selection previews tilt toward the viewer to expose the decks and leave space above
the buttons. These cosmetic changes leave equipment dimensions and physics intact.

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

The terrain mesh cache uses a 128 MiB budget for vertex/index buffers, culling
ranges and command records, evicting the least recently used venues. One active
venue may exceed the budget by itself. A single CPU worker prepares missing
scenery and that venue's camera/arena layouts in an isolated command recorder;
it creates no GPU objects. While preparation runs, the renderer continues drawing
the last complete venue and menus keep their controls visible and interactive.
Before the first venue is ready, the background uses the UI's dark color instead
of exposing a bright, terrain-free sky. Gameplay shows a loading card; simulation
and replay clocks wait for the requested scene, even when an older venue is
visible. Only
explicit smoke tests/captures wait synchronously for preparation. Startup no
longer builds the camera rails for all 32 venues. Static batching finishes before
the outer attribute restoration so cached scenery preserves the caller's current
color, normal and texture coordinates. Drawable dimensions are clamped to at least
one pixel to avoid invalid projections and UI scaling when a backend reports zero.

Static meshes share vertices only when all 64 attribute bytes are identical.
Their original triangle stream becomes a 32-bit index buffer with unchanged
triangle, material and transparency ordering; no coordinates are rounded.
Vertex/index upload occurs once, and both temporary CPU streams are released.
Commands with alpha=1 at every vertex use an opaque pipeline only if the bound
texture also has alpha=255 throughout its mip levels. Transparent snow edges,
markers, shadows, ghosts and UI keep their original blending and draw order.

Character previews cache fixed geometry while Cartoon's dress and long hair
continue to animate. Repeated presentation frames reuse fall contact results;
the shadow is promoted to an indexed GPU mesh on repetition. Pose, interpolated
skeleton, wind, animation time, hill, character and lighting changes invalidate
the relevant caches. Moving frames avoid rebuilding indexed shadow meshes.

Ordered ranges of at most 128 triangles retain conservative local-space bounds.
The current projection/model-view frustum rejects only ranges wholly outside
one clip plane; surviving adjacent ranges merge into the existing draw packets.
Wide lines remain unconditionally visible because the shader expands them in
screen space. Trees and their contact shadows retain individual batch boundaries.
Animated tree boughs and audience arms/flags use generous object bounds before
geometry generation. None of these operations modifies terrain, track, camera
paths, object placement, physics or MSAA quality.

The explicitly invoked render probe reports cached/submitted/culled geometry
and draw calls. `--no-culling` supplies an unculled reference using the same
indexed meshes; it is a diagnostic option, not a gameplay setting.

Each tree has one stable foliage tint from five evergreen colors: deep forest,
green, blue-green, warm olive-green and lighter green, with ±6% brightness
variation. The wider palette keeps neighboring trees distinguishable at normal
playing distances under daylight and overcast lighting. All bough tiers, including
the animated top, share that tree's tint and retain their existing face shading.
Snow caps keep their original material. Cosmetic tree hashes select the tint without
consuming gameplay/wind randomness. This uses existing vertex colors, adding no
vertices, textures, shaders or draw calls.
The palette was reviewed in 18 native 1920×1080 captures across three hills,
in panorama and flight views under sunny daylight, overcast daylight and night.
The environment and renderer command suites cover the existing protected
snow-coordinate checks.

Distant mountains use a prepared, transparent CC0 photographic panorama on a
world-direction background ring. Only the decorative ridgelines were replaced;
playable valley vertices, hill/inrun profiles, starts, physics and camera paths
remain unchanged. The photograph's sky is removed so the existing sky, sun,
moon and clouds stay separate. Morning/evening retain a cool upper/opposite sky,
with warm colors restricted to the low horizon toward the sun. Mountain tint
uses the natural snow material and the same ambient, directional and fill
light palette as the model, including the weather response. Its lower haze
blends toward illuminated snow instead of forming a gray band. Photographic
fine detail is softened offline with a 1.6-pixel Gaussian blur at the
2048×768 preparation size. Contrast is compressed to 82% around the .88 snow
white point, and residual saturation is reduced to 12% so the scene's light
palette supplies most of its color. The skyline alpha is applied separately
and stays identical. These adjustments affect only the prepared photograph,
with no lens blur, extra runtime processing or change in texture size.
Only the valley mesh's final five scaled metres (195–200 from the centre)
fade into the background. The existing ten-metre grid is retained; the snow
material evaluates this smooth alpha transition per pixel from world-Z UVs,
so interpolation cannot widen it to an entire grid row. The decorative upstream
cap is also softened, at least sixty scaled metres before the original inrun,
so the narrower lateral fade cannot expose its straight silhouette against sky.
Snow beneath all tree
bases and the entire playable corridor stay opaque; this material blend retains
positions, depth writes and snow texture pixels. This adds no vertices, draw
calls, textures or per-frame terrain construction. Existing distance fog
controls weather visibility and the stronger haze toward the foothills.

The decorative terrain flanks after `outrunBottom()` descend outside a protected
apron of max(50 hill scales, 24 metres), blending over 60 scales. After both the
prepared strip (bottom + 47 metres) and the arena fixtures (190 scales), the
whole decorative continuation rolls into a shallow downslope with continuous
endpoint height and tangent. This replaces the unbounded uphill extrapolation
that formed a background snow wall. Hill profile functions, simulation, starts,
inrun/landing/prepared snow, stands, signs and mast elevations are unchanged.
Background trees and their contacts read the same decorative surface so they
remain grounded. Vertex counts, grid coordinates and draw-pass budgets stay fixed.
The mesh audit retains its original golden hashes: only the explicitly bounded
backdrop elevations are normalized to their old values when hashing; actual
protected elevations and every X/Z coordinate/count must still match exactly.
Separate tests check the backdrop joins and downward slope across all 32 hills.
Photographed shading is baked, not dynamic
lighting or image-based illumination. A reflected wrap closes the source photo;
it is decorative, not a geographic 360-degree capture of a venue.
The panorama adds one small 32 by 20 textured background mesh and one 4096 by
768 RGBA texture (12 MiB before driver overhead), loaded once and shared by all
venues. It needs no new shaders, shadows, image processing or per-frame uploads.

Source/license and offline preparation instructions are in
`assets/environment/CREDITS.md` and `tools/prepare_mountain_panorama.py`.
Low inrun decking receives baked ground darkening; exposed foundations use
small cached contact patches on the existing ground receiver. Athlete fabric,
rubber, ski coating, helmet, visor and fittings have separate inexpensive vertex
materials. Highlights follow the existing direct light and are restored after
drawing the athlete; geometry and fall-contact measurements remain unchanged.
Static contacts under trees, supports, stands and light masts share one 64×64
alpha mask with averaged mip levels (about 21 KiB of RGBA texels before driver
overhead). The mask retains the quadratic soft falloff; a 12-sided footprint
avoids spending fragment work on a rectangle's transparent corners. Receiver
triangles are clipped to that footprint while compiling the cached scenery,
so the decal follows the existing snow cells and their diagonals without
changing terrain or adding per-frame projection work. The existing blended
contact pass, colors, opacity and depth settings are retained; no new shader or
offscreen pass is needed. The athlete's moving silhouette is unchanged.
On 2026-10-15, 70 before/final native 1920×1080, 4× MSAA images were compared
across five hills and seven scenes. All 13 standalone suites and the full-game
Vulkan smoke test passed; receiver clipping, soft-mask coverage and the original
snow hashes are checked. Contact-only triangles fell from 57,856 to 16,773 on
K50, 59,136 to 11,868 on K120, and 58,880 to 9,384 on K250 (71–84%).
Controlled Iris Xe benchmarks used 15 scenes, 40 warmup frames and 240 measured
frames per scene, with three runs per variant in before/after/after/before/before/
after order. Median daylight camera-1 flight times were 10.636→10.373 ms on K50,
8.083→7.787 ms on K120 and 6.469→6.458 ms on K250. The sum of the 15 scene medians
fell 3.6%; K250 was mostly unchanged. These are CPU command/submission plus
explicit GPU-completion wall times in hidden windows, excluding desktop/VSync
pacing; they do not establish a sustained gameplay FPS gain. Local detailed
results and method are in `captures/contact-shadow-review/benchmark-summary.csv`
and `benchmark-method.json` in that directory.
The sky combines its gradient and two soft cloud scales in one 32 by 20 mesh.
World-space directions and a cosmetic seed keep clouds stable through cameras,
pause and replay seeking, with no textures, extra render passes or per-pixel noise.
Clear weather has no cloud layer. Clear daylight uses a blue sky and reduced
distance haze so distant ridgelines remain distinct from overcast conditions.
On 2026-10-14 the photographic backdrop and cool twilight sky were reviewed
in 256 native 1920×1080, 4× MSAA captures: four venues, four views, all 16
time/weather combinations. A subsequent 64-capture fog check confirmed that
fully obscured mountains disappear rather than leave a fog-colored silhouette.
All 13 standalone suites passed, including frozen snow-coordinate hashes across
32 hills. Full-game Vulkan validation completed all venues, screens, replay and
a 64-jump cup. Photograph illumination is baked; these checks do not establish
physically changing shadows within the photo or a sustained frame-rate guarantee.
The seam/palette refinement was subsequently checked in 320 native captures
(four venues, five views including all three gameplay cameras, and all 16
time/weather combinations), with 13 passing suites and full-game Vulkan
validation. Frozen snow-coordinate counts/hashes remain identical on all 32
venues; the material blend changes neither terrain nor start positions.

On 2026-10-15 the downward decorative continuation was reviewed in 320 native
1920×1080, 4× MSAA captures across four venues, five views and all 16 time/weather
combinations. All 13 standalone suites and the full-game Vulkan smoke test passed.
An additional 192 original-physics runs across all 32 hills, headwind/calm/tailwind
and normal jumps/falls stayed within the protected original surface. The geometry
change affects only the explicitly bounded backdrop described above.

The subsequent photograph softening/color treatment was checked in 80 native
1920×1080, 4× MSAA captures (four venues, flight/panorama, all four clear times
and day/night for the other weather types). Environment and renderer command
suites passed. BMP dimensions/header and every skyline alpha byte match the
previous asset; only its RGB pixels changed.

On 2026-10-14, 224 before/after captures at 1920×1080 with 4× MSAA were pixel
identical across all 32 hills, three cameras, panorama and day/snow/night scenes.
All ten standalone suites and the installed full-game Vulkan smoke test passed.
Short sequential Iris Xe benchmarks (300 measured frames after 40 warmup frames,
hidden window with explicit GPU completion) reduced daytime flight medians from
14.20 to 13.22 ms on K50, 11.39 to 10.33 ms on K120, and 9.07 to 7.55 ms on K250.
Cached vertex/index buffer storage fell approximately 45%, while these camera-1
flight views omitted 54–75% of static triangles. These are raw rendering timings;
desktop/VSync presentation and sustained thermal behavior are not included.

The CPU command test covers all 32 venues, three cameras, panorama, ghosts, HUD,
falls and autopilot trajectories. These checks do not establish GPU driver stability
or prove that a previously observed AMD graphics timeout is resolved.

The jumper casts one silhouette shadow from the displayed body and ski joints.
A bounded 96 by 64 CPU coverage mask combines their shapes before softening;
overlapping parts never accumulate opacity. Gaussian softening increases with
height above the snow and preserves the silhouette's coverage rather than adding
an oval underneath it. One non-overlapping mesh draws the result onto the existing
snow triangles, without new textures, shadow maps or offscreen rendering passes.
Near-snow contrast and sharpness remain readable under clouds and grazing
sunlight. A broad sky projection takes over smoothly when direct light cannot
reach the receiver; floodlights contribute one blended projection at night.
The CPU command suite checks overlap, ski gaps, height-dependent softening and
landing contrast in all sixteen time/weather combinations.

A low backdrop mist softens the join between the terrain and photographic
mountains. The outer valley flanks and decorative tail fade toward the same
light-dependent air color as the photo foot; foreground distance gating keeps
the jumping corridor clear. The photographic veil follows world bearing and
altitude, leaving peaks distinct. Dense weather that already hides the photo
keeps its previous fog. The small vertex calculation and final color blend
share the existing passes; there are no fog planes, additional textures, or
terrain/physics changes. UI overlays explicitly reset the veil, including 3D
jumper previews.

`build-vulkan/snow-glitter-probe /tmp/backdrop-check assets --backdrop-mist`
writes paired mist on/off captures for all 32 venues, panorama/gameplay views,
all three cameras and sixteen light/weather presets. Its CSV checks the near
snow and opaque UI/preview remain unchanged. These explicit Vulkan diagnostics
use the same `TPV_BUILD_RENDER_PROBE` option as the snow-crystal checks below.

Snow crystals use a fixed world-X/Z cell pattern in the existing scene fragment
shader, with randomized microfacet normals and camera-dependent solar glints.
Only terrain/prepared/inrun snow is tagged; other materials retain the previous
shader output. Daylight strength disables glints at night and under the existing
cloud/fog presets. Screen-space derivatives soften each highlight and fade
unresolved distant cells before texture-scale shimmer can develop. There is no
frame-clock randomization, additional geometry, texture, or render pass. Terrain
positions, normals, physics, input and original simulation RNG are unchanged.

The explicit visual probe is built with `-DTPV_BUILD_RENDER_PROBE=ON`:
`build-vulkan/snow-glitter-probe /tmp/snow-glitter-check assets`.
It writes paired captures and CSV measurements for tagged/untagged materials,
960/1920-wide images, supported MSAA levels, fixed/moving viewpoints, distant
snow, all sixteen time/weather presets and a 24-frame slow camera traverse.
Its on/off timing comparison explicitly renders and completes the offscreen GPU
work, since hidden windows may have no drawable swapchain. No personal saves
are read or written. Software-GPU timing does not predict hardware frame rates.

Settings contains only category links: Graphics, Audio, Controls, Gameplay and
Environment. Audio has separate Sound and Music on/off controls. Music defaults
on and persists in the optional `MUSIC 1` records tag (absent/invalid tags default
on without affecting records or other preferences). The stereo menu theme is
loaded and converted once at startup, with a separate fixed-block SDL audio
stream, no callback allocations, and a 400 ms fade. Menu navigation preserves
the playback cursor. Hops, pause, results, standings, jump-ready and replays fade
music out; a return to the main menus resumes it. Sound affects gameplay audio
independently. Controls includes mouse sensitivity and the existing bindings;
Gameplay contains camera, practice wind and ghost; Environment contains time
and weather. Each category returns to Settings through Back or Escape.

Settings → Graphics provides Fullscreen, Resolution, Antialiasing (off/2×/4×/8× MSAA), and
VSync (on/off) on one page. VSync defaults to on and follows the display, without
a separate FPS cap. Off requests immediate presentation; unsupported immediate
presentation falls back to VSync and the UI shows the supported mode.
Preferences are saved in an optional `GRAPHICS 5` records tag;
resolution defaults to Auto (desktop fullscreen or a window fitted to the desktop).
Supported common resolutions up to 3840×2160 and the native desktop mode are offered;
unavailable saved modes fall back to Auto. Manual window sizes account for pixel density.
Older saves retain their records and display preferences, including saves from
the former manual frame-rate menu. Graphics changes apply between submissions.
Both GPU frame slots finish before sample-count-specific attachments and
pipelines are replaced; cached scenery and textures remain available.
The fixed sky, scenery, shadow, overlay and transparent-effect pipeline states
are compiled during renderer setup and again when MSAA changes. Takeoff powder
and air streaks therefore reuse a ready pipeline on their first flight frame,
including with an empty driver shader cache. Other states retain lazy creation.
Only levels supported by both color and depth targets are offered. Saved levels
unsupported on another GPU fall back to the nearest lower supported level.
The default is 4× when available. F uses and saves the same fullscreen
setting.

Jump lengths and personal records display two decimal places. Result/record
presentation uses the existing centimeter measurement retained for original
replay recording, while the simulation's half-meter scoring distance and cup
arithmetic remain unchanged. New native replays store the centimeter length;
original replay headers use their recorded distance. Existing saved records
retain the precision with which they were originally stored.

Camera smoothing uses elapsed presentation
time, retaining the previous 16 percent response per frame at 60 FPS. The
original 70 Hz physics and terrain geometry remain unchanged. The renderer uses
SDL3's GPU API with Vulkan selected explicitly, embedded SPIR-V shaders and at
most two frames in flight. Each slot owns its transfer/vertex buffers, color/depth
targets and fence, and retains referenced meshes/textures until completion. CPU
scene building overlaps the previous submission; waiting happens only before
reusing a GPU slot. Minimized windows wait for events without rendering, and an
unavailable swapchain skips offscreen drawing and uploads. `--graphics-compat`
and `--sync-rendering` wait for each
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
  --replay /path/to/latest-dsj2.pvr --expect-renderer Vulkan \
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

The result card adds three compact assessments: takeoff timing, flight balance
and landing. `jump_feedback.h` reads the finished simulation once: timing comes
from the recorded takeoff input and frame position, balance from the original
style accumulator, and fall causes from the original landing classification.
Timing uses a practical 0.14–0.25 second band before the edge, not an exact
prediction of the best possible jump. The adjacent 0.25–0.32 second and
0.07–0.14 second bands show `Slightly early` and `Slightly too late`;
larger deviations show `Early` or `Too late`. Scores, physics and replay/save formats
are unaffected. Missing stages display `--`; inrun falls and start timeouts do
not receive a flight or landing assessment for a jump that did not happen.

## Save compatibility

The SDL user-data root is `the-perfect-v` without an organization subdirectory.
On Linux, the default save directory is `~/.local/share/the-perfect-v/saves/`,
respecting `XDG_DATA_HOME`. A local `~/.local` installation places resources in
`~/.local/share/the-perfect-v/assets/`; installation only updates resources and
the executable, leaving saves intact. The first default launch imports regular
files (including nested replays) from the previous save directory, skipping
temporary files, obsolete import markers and existing destinations. It prefers
the most recent previous namespace and only falls back to the original namespace
when the more recent directory is absent. Copies are staged before rename; originals remain intact. A marker
prevents subsequent launches from resurrecting deleted saves. Import failures
stop startup before default records can overwrite missing imported preferences.
The executable prints the actual save directory at startup; `--data-dir PATH`
overrides it and bypasses import. Native replays use `.PVR`; there is no
migration of files with the previous replay extension. The replay payload and
`.SJC` competition identifiers are unchanged.

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
Random times are equally weighted. Random weather uses 45% sunny, 15% overcast,
30% snowfall and 10% fog. Native replays store the resolved conditions and cosmetic
seed, so seeking, camera changes and changed preferences cannot reselect weather.
PVR replay payloads without cosmetic metadata and version 1/2 cup saves default
their missing time/weather metadata to day and sun. Original .RPL exports retain their
original bytes and cannot carry this modern atmosphere metadata.

The replay screen offers `Export .PVR` and, when supported, `Export .RPL` in the
right-hand button column. Hover hints explain that PVR preserves the jump, time
of day, weather and character for The Perfect V, while RPL targets the original Deluxe Ski
Jump 2 without modern time/weather settings. Native export saves
an independent, uniquely named file in the game's save directory and displays
its full path. It preserves the displayed time/weather, recorded wind samples
and any attached original input recording. A validated `JUMPER 1` tag stores the
cosmetic character; files without it default to Pro. Both automatically saved
and manually exported PVR files retain the recorded choice. Playback never changes
the player's character preference. RPL imports use the current choice because
that format has no character metadata. The automatically saved latest
replay can therefore be archived before another jump replaces it.

Four compact lighting presets set sun/moon direction, sky gradient, ambient and
directional light, fog color and visibility. Overcast, snow and fog reduce direct
light and soften the single silhouette shadow. Shadow projection uses the light
field and the local receiver tangent, retaining diffuse near-snow visibility.
Arena floodlights use three paired columns on K50–K60, four on K70–K95 and five
on K100 and larger hills (six/eight/ten masts total). Fewer columns spread over
the same first/last positions rather than crowding short hills; height and
fixture dimensions retain their existing scaling. Fixtures, contact patches,
live illumination and the athlete's shadow all use the same placement.
Each concrete mast foundation is 2.2 × 2.2 metres in plan and 1.2 metres deep.
Its top stays 0.6 metres above the original ground anchor; doubling the depth
extends the bottom downward to 0.6 metres below that anchor, into the snow.
The row on the camera-1 side has a fixed minimum setback of 30 metres from the
track centre; camera 1 stays at 26 metres. The whole fixture, including its
foundation and lamp head, therefore stays outside the camera's travel corridor.
Only this row is moved where the old setback was smaller; the opposite row and
rows already farther out keep their placement. The rule is part of the cached
layout, independent of the active camera, with no fading or per-frame sight tests.
Inactive source slots are zeroed, and the shader receives the active column count in an
existing uniform component. Wider short-hill beams can overlap the inrun light
near the takeoff instead of being cut off by the old fixed field boundary.
Each mast supplies one aggregate spotlight from its lamp faces, aimed at the
landing snow. Arena vertex lighting evaluates at most four local
sources (the two adjacent mast columns on both sides), with smooth longitudinal,
cone and finite-range attenuation. Source selection uses world coordinates,
including translated athlete geometry, and fades continuously between columns.
Day disables the local lights; morning/evening use weaker lamps and night uses
full strength with a reduced directional fill. Cached terrain retains live light
uniforms. Lamp glass uses warm emission scaled by that same live strength,
without bloom textures or extra passes. The sun/moon disc and a small fixed star field are projected as
background primitives when in view; clouds suppress them. All lighting and fog
share the existing rendering pass, without shadow maps, volumetric
clouds or new textures. Time and weather do not alter terrain, camera rails or
collision geometry, and changes reuse the cached terrain meshes.

The reduced mast layout was validated on 2026-10-15 with all 13 standalone
suites, including unchanged protected snow hashes on all 32 hills, actual
fixture counts, inactive source slots, short-hill coverage and shadow continuity.
84 native 1920×1080, 4× MSAA captures covered seven hills, four views and
day/evening/night; full-game Vulkan validation passed all venues, replay and
a 64-jump cup. No terrain/profile, start or physics change is involved.

The subsequent camera-side setback correction passed all 13 suites and the full
Vulkan smoke test. A 192-run sightline probe (all 32 hills, normal jumps/falls and
headwind/calm/tailwind) found no mast obstruction. 56 native 1920×1080 captures
checked the four identified problem venues on day/night and all three cameras,
including the exact flight moments that had been obscured before the correction.

The start signal uses green/red tinted glass with fixed electrical emission
and a small specular reflection. Its warning blink sets emission to zero in the
off phase, using the frame clock so pausing freezes it. Signal emission is
independent of time-of-day lamp strength; normal depth occlusion and fog still
apply. The material is scoped to the lens, with no added local light, texture or
rendering pass.

The windsock, flakes and moving boughs still use the same smoothed x/y wind angle
as the HUD arrow. Snowfall uses the existing compact world-space volume: at most
225 candidate flakes, with only visible flakes drawn. Sizes and fall speeds vary,
with weak wind drift and a mostly downward fall. Soft volume and lifetime fades
prevent popping. Flake animation uses the frame/replay clock, so pause freezes it
and seeking is repeatable. Menus preview lighting/fog without snowfall particles.
The fixed snow volume sits ahead of the actual camera rather than halfway to
its subject, so the distant camera 3 retains coverage at the top and edges.
Ultrawide views bring the volume closer instead of adding cells; all cameras
still consider at most 225 candidates and submit at most 1800 powder vertices.

Successful outrun braking adds low outward powder fans along the front working
edge of each ski. The displayed skeleton controls the inward wedge, independent
snow contact and speed fade; parallel skis, airborne skiers, falls and stopped
skiers do not generate this effect. At most 64 analytic particles use the replay
clock and existing snow receiver, without changing physics, poses or save data.

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
