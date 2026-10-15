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

Without external fixtures, CTest registers ten standalone suites:

- `renderer-commands`: CPU-only validation of finite rendering arguments, stack balance, current attributes, terrain cache eviction, zero-size drawables, graphics resource lifetime and camera response at different frame cadences. It does not use a GPU or emulate rasterization.
- `environment`: manual/random choices, atmospheric lighting, preference/replay compatibility and fixed conditions through both rounds and all participants in World/Team Cup events.
- `presentation-timing`: frame deadlines without accumulated timer overshoot, recovery after a stall and camera smoothing independent of frame cadence.
- `camera-paths`: finished camera 2 rails on all 32 hills, continuous spline velocity, snow clearance and sightlines during normal jumps and falls with varying wind and frame cadence.
- `render-interpolation`: presentation interpolation between simulation ticks.
- `fall-presentation`: deterministic display contact, unchanged physics/poses and no GL calls in the contact calculation.
- `physics-and-storage`: native jumps on all 32 hills, controls, scoring and storage.
- `storage-migration`: stable hill identifiers and compatibility with older saves.
- `crowd-audio`: approach/flight envelopes on all hills, distant and nearby falls, replay seeking/speed, one-shot reactions, smooth fades, valid sample mixing and the sound toggle through a dummy SDL audio device.
- `mesh-geometry`: exact attribute sharing, preserved triangle order, bounded ranges, camera transforms and conservative near/side/far clipping without initializing a GPU.

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

## Inrun structures and lighting

The structural assembly surrounds the original inrun: continuous steel
stringers, transverse girders, terrain-embedded footings and braced piers.
Pier height follows the existing snow triangles, with no reversed columns on
earth-supported sections. Service stairs, close-spaced rail posts, takeoff
access openings and supported cabin landings remain outside the ski grooves,
on the -Z side only. The +Z side facing camera 1 has no service deck, stairs,
railings or inrun lamps; the underlying load-bearing structure remains.
The original snow surface, groove vertices, start seat and physics coordinates
are retained. The command suite freezes the complete terrain/inrun vertex
streams from before this change on all 32 venues.

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

The main menu's Ski Jumper screen uses the existing panels, buttons and two native
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
Both skins wrap the exact same original skeleton, including interpolated joints
and fall poses; skis and bindings retain their geometry. Both wear the same
light boot shells with red closures, metal buckles and dark flat soles, making
the boots distinct from the trousers and turquoise bindings. Boot cuffs follow
the lower legs, with trouser hems tucked inside instead of covering the shells.
The appearance
enum stays outside simulation, AI, input, scoring and replay physics. Native
replays and ghosts use the selected skin as a presentation preference.

The optional `JUMPER 1` preferences tag preserves the choice while retaining the
existing records format; older saves default to Pro. The fall-contact display
correction measures the selected skin and only changes its temporary render copy.
Preview lights and transforms are scoped to the menu draw and restored afterward.

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

Static meshes share vertices only when all 64 attribute bytes are identical.
Their original triangle stream becomes a 32-bit index buffer with unchanged
triangle, material and transparency ordering; no coordinates are rounded.
Vertex/index upload occurs once, and both temporary CPU streams are released.

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
The original valley mesh's final two lateral rows (180–200 scaled metres from the centre)
fade into the background through cached vertex alpha. Snow beneath all tree
bases and the entire playable corridor stay opaque; this material blend retains
positions, depth writes and snow texture pixels. This adds no vertices, draw
calls, shaders, textures or per-frame terrain work. Existing distance fog
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

The result card adds three compact assessments: takeoff timing, flight balance
and landing. `jump_feedback.h` reads the finished simulation once: timing comes
from the recorded takeoff input and frame position, balance from the original
style accumulator, and fall causes from the original landing classification.
Timing uses a practical 0.14–0.25 second band before the edge, not an exact
prediction of the best possible jump. Scores, physics and replay/save formats
are unaffected. Missing stages display `--`; inrun falls and start timeouts do
not receive a flight or landing assessment for a jump that did not happen.

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
Random times are equally weighted. Random weather uses 45% sunny, 15% overcast,
30% snowfall and 10% fog. Native replays store the resolved conditions and cosmetic
seed, so seeking, camera changes and changed preferences cannot reselect weather.
Existing replay files and version 1/2 cup saves remain readable, defaulting their
missing cosmetic metadata to day and sun. Original .RPL exports retain their
original bytes and cannot carry this modern atmosphere metadata.

The replay screen offers `Export .SJR` beside `Export .RPL`. Native export saves
an independent, uniquely named file in the game's save directory and displays
its full path. It preserves the displayed time/weather, recorded wind samples
and any attached original input recording. The automatically saved latest
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
