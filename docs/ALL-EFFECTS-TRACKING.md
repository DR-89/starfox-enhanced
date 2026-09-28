# All proposed effects — completion audit

Scope: the effects named in the two global-effects proposals, including the
geometry-, depth-, velocity- and event-driven versions explicitly distinguished
from simpler post-processes. Existing screen filters do not satisfy those items.

Implemented and desktop-tested: anamorphic flares, grain/vignette, CRT scanlines,
phosphor mask and curvature, explosion shockwaves, surface weapon lighting,
world-space exhaust particles, stage-aware snow/rain/ash, geometric screen-space
ambient occlusion, surface-depth-based depth of field, damage/destruction sparks
and debris, localized exhaust heat distortion, independent CRT phosphor
persistence, and adaptive exposure. Contrast is the user's
requested rename of HDR Effect; it is not an HDR-output claim.

Remaining requirements:

- Stereo tester follow-up: complete scope and verification checklist in
  `STEREO-DISPLAY-UPGRADE.md` (depth controls/backgrounds/crosshair, more output
  formats, Leia SR investigation and higher desktop render scales).
- Volumetric fog and lighting with geometry occlusion (not radial light shafts).
- Velocity-based motion blur (not radial blur).
- Contact-hardening shadows (not merely more fixed-radius samples).
- Water caustics.
- Optional impact shake, recoil and banking camera response.

## Velocity motion blur reference (2026-09-27)

Liquid reflection validation: water capture `tmp/water-exposure-pause` on
D3D12 passes 50 paused/48 resumed frames; EX 6-6 lava capture
`tmp/lava-exposure-pause` on Windows Vulkan/DXR interop passes 53/45. Both use
2x native resolution, particles, bloom, ray tracing and enhanced sky. Added
`-CheckGroundReflection` to require a ready independent reflection buffer and
reject declined exposure frames. The lava run passes it. Final images inspected.
Paused BMPs at frames 33/41/49/57 are byte-identical within each capture, proving
the liquid image freezes for that sampled pause interval (not just history
flags). These checks do not cover TAA/FSR, stereo, Linux-native/Apple hardware
or per-shutter-sample secondary tracing. `build/current` unchanged.

Fixture correction and reflection validation: the earlier `motion-mirror-underlay`
capture actually selected GOLD METAL (ground index 7), not MIRROR (index 6).
Fixed-step gold captures with and without exposure both show the golden bands,
so those bands were not introduced by motion blur. Corrected mirror captures
`tmp/true-mirror-fixed-baseline` and `tmp/true-mirror-fixed-exposure` were inspected
and have no gold bands. Added named `-GroundMaterial` choices to the helper,
retaining the legacy numeric argument. `tmp/mirror-reflection-pause` uses the
named Mirror choice and passes particle/ground-shadow/pause guards (54 paused,
45 resumed frames) with mirror models, enhanced sky and bloom. These are tested
D3D12 cases, not broad backend/temporal-upscale/stereo completion. Current build
unchanged.

Diagnostic live reflection integration: mono native-resolution motion blur now
uses an independent same-backend ground-reflection producer, preserving the
foreground buffer. Nonreflective/no-ground underlays use no reflection buffer.
The extra dispatch is diagnostic-motion-blur-only. TAA/FSR and stereo reflection
combinations remain gated. Development PC rebuild passes. Capture
`tmp/motion-mirror-underlay` passes at 2x with mirror ground, mirror models,
hardware shadows, enhanced sky, exhaust and medium bloom; ground-reflection
logs report ready and joint exposure has valid history. Final image inspected:
conspicuous reflected bands around pillars require a matched no-motion-blur
baseline comparison before visual sign-off. Secondary rays are same-frame,
not retraced per shutter sample. Broader pause/backend/device validation remains
open. `build/current` unchanged.

Metal reflected-underlay source implementation added. The optional flag uses
the spare ground-point W component without changing the 272-byte reflection
uniform layout. Ground-only mode skips primary model traversal while keeping
secondary reflection/transmission queries unchanged. Missing ground/material
inputs invalidate output before submission. Exact runtime shader extraction
passes into `tmp/metal-reflected-underlay`; Apple compilation and device tests
are still unverified (no Apple SDK on this host). Live reflected-underlay
compositor wiring remains open; `build/current` unchanged.

Linux reflected-underlay source path added: Vulkan reflection uniforms select
ground-only primary hits without changing secondary scene traversal. Invalid
ground/material requests clear the output. Regenerated SPIR-V successfully;
Linux core and the updated hardware probe compile. Probe adds covered-ground
model/ground marker and invalidation checks followed by ordinary reflection
recovery. These new runtime assertions remain UNVERIFIED because local WSL
cannot create a Vulkan device. Metal equivalent and live compositor wiring
remain open. `build/current` unchanged.

Resident reflection transport: SDL DXR reflection submission now exposes the
ground-only flag and rejects missing ground/material parameters without stale
output. Added comparisons using SDL-produced geometry against the independent
DXR reference, verifying separate producers preserve foreground reflections.
Full GPU ray-geometry checker passes on D3D12 and Windows Vulkan/DXR interop
(three scene variants, 2,925 expanded vertices and existing mutation cases).
The run exposed a stale composition expectation: ordinary dielectric surfaces
were expected to reflect at mirror strength. Updated the checker to separately
verify face-on dielectric Fresnel (.08) and mirror strength at all existing
intensities/offsets; no production reflection behavior was changed for this.
Linux-native Vulkan/Metal equivalents and live reflection exposure integration
remain pending. `build/current` unchanged.

Reflected-underlay prerequisite: DXR ReflectionInput now supports ground-only
primary receivers while keeping scene geometry in secondary reflection,
transmission and shadow queries. Requests require the physical ground and
liquid/material parameters. A covered mirror-plane fixture verifies the normal
model marker versus the ground-only marker, changed offscreen reflected colour,
invalid-request output clearing and normal-render recovery. The complete DXR
checker passes, including pre-existing water/transmission/caustic and shadow
cases. Runtime SDL exposure wiring and Vulkan/Metal equivalents remain open;
this does not yet remove the live motion-blur reflection gate. `build/current`
is unchanged.

Pause and baseline follow-up: native 2x hardware-shadow/particle/bloom captures
pass pause/resume guards on D3D12 (49 paused/45 resumed frames) and Windows
Vulkan presentation with DXR interop (54/45). The latter final image was
inspected; this does not validate Linux Vulkan hardware tracing. Added
`-CheckGroundShadow` to require a ready hardware underlay with resident caster
geometry and reject declined exposure frames. Baseline testing exposed missing
driver diagnostics outside temporal captures; explicit backend requests now
enable tracing and the native pipeline logs its actual SDL driver. Rebuilt PC
and reran `tmp/shadow-no-motion-baseline`: backend verified and no extra ground
shadow dispatch without diagnostic motion blur. `build/current` unchanged.

Live native-resolution shadow integration: diagnostic motion-blur runs now
dispatch an independent same-backend ground-only mask, retaining foreground
mask storage and GPU-resident caster geometry. The revealed underlay uses this
mask instead of foreground receiver shadows; space uses no underlay mask.
Ordinary runs do not dispatch the extra pass. The native mono resident-shadow
gate is lifted only when the underlay is ready; CPU-mask, stereo, TAA/FSR shadow
combinations and reflections remain gated. This uses same-frame shadowing,
not caster retracing at every shutter sample. Build passes. Live capture
`tmp/motion-shadow-underlay-dxr` passes at 2x, hardware DXR, exhaust particles,
medium bloom and enhanced sky/ground: logs confirm ready hardware underlay
with resident geometry and joint exposure with valid history. Final image
inspected. Broader backend/pause/occlusion validation remains; `build/current`
is unchanged.

Metal source integration: shadow uniforms now expose the matching ground-only
receiver flag, retaining the 320-byte host/shader layout and all secondary
model casters. No-plane calls invalidate the borrowed result. Added optional
Apple hardware comparisons to the portable shadow checker: nine tilted-plane/
quality cases against the CPU reference plus no-plane invalidation; unavailable
hardware prints an explicit skip. Exact runtime shader extraction succeeds,
but this Windows host has not compiled or executed the Metal changes. Apple SDK
compilation and hardware validation remain required. Runtime motion-blur
integration remains gated and `build/current` unchanged.

Vulkan hardware underlay path added: resident shadow submission can select the
ground plane as the primary receiver while preserving full-scene secondary
ray queries. No-plane requests clear the output. Regenerated SPIR-V with glslc
and compiled the Linux core plus `starfox_vulkan_ray_support_check` in WSL.
The checker now contains nine CPU-reference comparisons (quality/tilted plane)
and repeated no-plane invalidation/recovery checks. These new GPU comparisons
are NOT executed here: the local WSL runtime cannot create an SDL Vulkan
device (`SDL_HINT_GPU_DRIVER vulkan unsupported`), including the fallback.
Hardware execution remains to validate on a Vulkan-capable Linux device.
Metal hardware support and live motion-blur wiring remain pending. No device
installation or `build/current` update was made.

Hardware underlay prerequisite: DXR now supports the separate ground-only
receiver through both native resident output and the SDL sharing bridge.
Primary receiver selection and texture-alpha coverage use independent flags;
all model casters remain in the acceleration structure. Nine tilted-plane and
quality cases match the CPU ground reference exactly. Transparent/opaque texel
cases, no-plane invalidation, deferred submission and recovery pass. The full
DXR checker passes, including its existing reflection/water/caustics cases.
The SDL D3D12 interop checker also passes the new underlay comparison and its
12 existing animated/resized mask transfers and effect blends. This adds no
normal-frame readback. Vulkan hardware RT, Metal hardware RT and runtime
motion-blur underlay wiring remain open; `build/current` is unchanged.

Revealed-ground shadow prerequisite: added optional ground-only primary
receivers to the CPU reference and portable resident shadow pass. All model
geometry remains in secondary shadow traversal; removing the models from the
scene would incorrectly remove their shadows. A lit foreground/shadowed rear
plane regression verifies that distinction. Nine tilted-plane/quality cases
match CPU output exactly on Vulkan and D3D12, with separate no-plane output
invalidation and subsequent recovery checks. The complete existing portable
shadow checker and CPU geometry tests pass on the tested desktop paths.
Portable shader payload regenerated and freshness check passes; the development
PC executable rebuild succeeds. This is not yet runtime underlay wiring,
hardware-RT underlay support or shutter-time caster animation; live RT/motion
blur gates remain unchanged. `build/current` is unchanged.

Reflection/exposure composition tests cover dielectric and conductor surfaces,
0/35/100 intensity, model/ground/miss markers, odd reflection extents and Y
offset, particles and protected HUD. Vulkan and D3D12 passed in the prior
integration work. These verify composition of an already-rendered reflection
image, not fresh secondary ray traversal at each shutter sample.

Shadow integration prerequisites: added CPU/u32/packed-byte shadow-mask
exposure comparisons, including non-four-aligned mask width, Y offset, model
motion, particles, exact HUD preservation and rejection of late shadow order.
Vulkan and D3D12 pass. These use a fixed-time mask; they do not establish
animated caster shadows across the shutter interval or finish live RT support.

Underlay inspection found a real metadata leak: native model colour respected
the clip rectangle, but model normals could survive outside it. Added a
regression that failed before the fix (`Clipped model leaked lighting metadata
at 1,0`), then made surface metadata obey the same clip bounds. Empty-clip
underlays now explicitly verify unchanged background colour and no valid model
surfaces across the existing scale matrix. This prevents same-palette hidden
models from supplying lighting/reflection normals to the revealed background.
Regenerated portable composite shaders and rebuilt the development executable.
Full composition checks pass on Vulkan and D3D12, along with both temporal
regression suites. Live shadow/reflection gates are unchanged until temporal
shadow and reflected-underlay handling are completed. `build/current` unchanged.

Shared AO validation complete for the tested desktop paths: full development
PC build and pixel-filter tests pass, including all 16 shared/repeated AO/DOF
equivalence cases and lookup-count assertions. Vulkan and D3D12 temporal
regressions pass. The broader `starfox_gpu_effects_check` also passes on both
backends, directly comparing CPU/GPU depth modes 1/2/3/4/8/12/15 (at most one
byte difference), plus its other scene, global, backdrop and bloom cases.
Live `tmp/ao-shared-live-pause` passes at 2x with TAA, high AO/DOF, exhaust,
medium bloom and enhanced sky/ground: 48 paused and 46 resumed valid-history
frames. Final image inspected. This verifies the shared computation without
claiming a measured frame-rate gain or resolving the remaining AO banding.
Metal/mobile validation and diagnostic motion-blur menu rollout remain open.
`build/current` is unchanged.

AO cost reduction in progress: extracted shared `ambient_occlusion.inc` and
compute its factor once per pixel before RGB/DOF processing in CPU and GPU
paths. The independent compiled shared-kernel check confirms bit-identical
RGB across all 16 AO/DOF settings, with high-AO-only surface queries reduced
from 111 to 39 and high AO+DOF from 186 to 114. AO-off has no extra surface
queries. These are operation counts, not an FPS claim. Added the equivalence
and query-count guards to pixel-filter tests and updated both shader freshness
hash inputs and legacy build dependencies. Portable payloads regenerate/check
successfully. Full build and GPU regression validation are still running;
`build/current` remains unchanged.

AO follow-up validation: replacement build finished successfully, including
legacy FXC, development PC executable, pixel-filter tests and GPU checker.
Pixel-filter regression suite and Vulkan/D3D12 temporal/effect checks pass.
The isolated compiled shared-kernel diagnostic (`tmp/check_ao_kernel.cpp`)
measures squared error 0.0109868 versus old 0.0429426 and maximum adjacent step
0.0867853 versus old 0.117419 for the analytic depth-edge fixture (about 74%
and 26% reductions respectively). These are fixture metrics, not game FPS.
`tmp/taa-ao-disk-updated` passes the live 90-frame 2x TAA/AO/exhaust exposure
guards. Its final image was inspected: shading is less visibly spoke-shaped,
but some banding remains. The pillar pose differs from the earlier capture,
so this is not a pixel-matched image comparison. Performance characterization
and further visual smoothing remain open. `build/current` is unchanged.

AO banding investigation: separate 90-frame 2x TAA/exhaust captures with bloom
off isolate the stepped pillars to AO (`tmp/taa-step-ao-only`); DOF alone is
visually smooth (`tmp/taa-step-dof-only`). Replaced the shared sparse 8-spoke,
two-radius AO kernel with a deterministic golden-angle disk: 16/24/32 samples
for low/medium/high. A rotation recurrence avoids large indexed constant arrays
and changing random noise. Added an analytic depth-step regression comparing
error and maximum adjacent-pixel jump against a dense 2048-sample reference and
the old kernel. Portable shader payloads regenerated. Build/test execution and
the updated live capture are pending; do not yet claim the artifact is fixed.
The first legacy FXC compile was deliberately stopped to simplify the kernel;
the replacement build is running normally. `build/current` is unchanged.

TAA depth-effect follow-up: the diagnostic live caller now requests aligned
surface metadata for active AO/DOF, including frames without weapon lights,
and admits depth effects only after that alignment succeeds. Added 90 signed-
jitter AO/DOF/light reference combinations per backend; Vulkan and D3D12 pass,
including unchanged HUD. The capture guard is now `-CheckTemporalSurfaces`
(`-CheckTemporalLighting` remains an alias), since the pass supports both.

Development app rebuilt. `tmp/taa-depth-light-joint-vulkan` combines TAA, high
AO/DOF, weapon lights, exhaust, medium bloom and enhanced sky/ground at 2x over
90 frames, with 89 aligned frames and no declined particle passes.
`tmp/taa-depth-joint-d3d12-pause` verifies the no-weapon-light case over 120
frames, including 53 paused and 45 resumed valid-history frames. Final Vulkan
image inspected: strong depth/blur produces visibly stepped shading on pillar
edges, still a visual-quality issue to investigate. These captures do not prove
all combinations are visually finished. Metal/device checks and menu rollout
remain outstanding; `build/current` is unchanged.

Latest TAA weapon-light integration: added `GpuTemporalSurfaces`, a resident
metadata resolve using TAA's current projection offset. It chooses a contributing
foreground surface instead of blending normals across a silhouette, validates
palette ownership and finite positive surface depth, and restores HUD ownership
independently. The diagnostic live path enables light/TAA motion exposure only
after this resolve succeeds. Additional buffers are allocated only when this
diagnostic combination actually has weapon lights; normal rendering is unchanged.
Vulkan/D3D12 tests cover signed/zero jitter, foreground selection, stale palette
owners, same-palette HUD exclusion, resize, alias rejection and non-finite jitter.
Generated SPIR-V/DXIL/MSL payloads are current; Metal execution remains untested.

The rebuilt application passes 90-frame 2x TAA/weapon-light/exhaust/bloom captures
on both backends (`tmp/taa-light-particle-aligned-vulkan` and
`tmp/taa-light-particle-aligned-d3d12`). Runtime checks confirm 58 and 41 aligned
lit frames respectively, with successful particle exposure and no rejected
combination. Vulkan's final image was inspected; pillar bloom remains very bright.
This establishes execution and metadata correctness in the tested fixtures, not
complete temporal visual quality, device performance, or normal menu rollout.
The earlier light/TAA gate notes below describe the prerequisite before this work.
`build/current` is unchanged.

TAA/particle integration verification: the diagnostic live path now accepts
particle-only scene exposure with TAA, forwarding the original jittered depth
and velocity guides separately from resolved colour. Surface weapon lighting
with TAA remains gated; this does not enable every temporal combination.
Signed-jitter joint CPU/GPU comparisons and compositor guide forwarding pass
on Vulkan and D3D12. The rebuilt development executable passes a 60-frame 2x
Original 1-1 capture with TAA, exhaust, medium bloom and enhanced sky/ground
(`tmp/joint-particle-taa-live`), with the final image inspected. A separate
120-frame Start-pause/resume capture (`tmp/joint-particle-taa-pause`) passes
51 paused frames and 45 resumed valid-history frames, including the particle
history reset checks. This is desktop Vulkan smoke coverage, not Metal/mobile
validation or proof that all visual combinations are ready for menu rollout.
`build/current` remains unchanged.

D3D12 live follow-up: `tmp/joint-particle-taa-d3d12-verified` passes the same
2x TAA/exhaust/bloom/enhanced-background Start-pause sequence, with 50 paused
and 46 resumed valid-history frames. The capture helper now accepts an explicit
`-GpuBackend` and checks the runtime backend evidence, restoring the caller's
environment afterward. The first backend guard incorrectly required a startup
`driver=` log absent in this capture; it now also accepts the actual resident
compute status. The repeated capture passes the corrected guard.

Weapon-light/TAA prerequisite: `temporal_composite.rgba` receives resolved
colour while its surface metadata remains from native composition; the
separate jittered world guides are forwarded only into motion exposure.
Stage 34 weapon lighting uses `surfaceAt` at the current pixel. Before lifting
the live light/TAA gate, align surface normals/depth/ownership to resolved
colour, or shade the temporal input before resolving it, with equivalent
underlay ordering and edge/HUD regression coverage. No gate was removed merely
to make this unsupported combination execute.

Reset-frame depth fallback: joint exposure now uses resident surface depth when
the dedicated geometry-depth guide is absent, with packed ownership/palette
validation before accepting the surface. This preserves model occlusion on
reset frames without inventing motion. Vulkan/D3D12 regressions pass all three
reset modes: no guides, dedicated depth only, and surface depth only. Missing
velocity remains rejected for valid-history exposure. The application has not
yet been relinked with this latest fallback change.

Application relinked with the fallback and current joint optimizations. A 90-frame
2x Original 1-1 stereo capture combines exhaust, weapon lights, AO/DOF mode 15,
medium bloom and enhanced sky/ground (`tmp/joint-exposure-light-depth-stereo`):
180 successful particle eye passes, 43 lit frames and no declined/failed passes.
Final image inspected; bright pillar faces remain heavily bloomed, so this is
execution evidence rather than a blanket visual-quality approval. Added optional
`-CheckParticleShutter` capture guard requiring nonempty valid-history particle
execution and rejecting failures; a separate 24-frame combined capture passes.

Current particle status: resident Vulkan/D3D12 reference comparisons, combined
model/particle/bloom ordering and live native mono/stereo pause/resume checks
pass. The implementation remains diagnostic-only. Moving-occluder exposure,
optical distortions, temporal-upscale combinations, Metal/device validation,
performance tuning and normal menu rollout remain incomplete. Historical notes
below describe incremental milestones and may predate this status.

Particle performance baseline: `starfox_gpu_temporal_aa_check <backend>
--benchmark-particles` submits 48 moving additive/debris particles, without
readback or upload inside timed iterations. Two warmup and eight measured
iterations per case; these are host submit-to-fence times, not GPU timestamps
or game FPS. At nine samples the measured medians were 0.349/1.087/2.616 ms
on Vulkan and 0.406/1.191/2.719 ms on D3D12 for 400x224, 800x448 and 1280x720.
Seventeen samples at 720p reached 5.374/5.594 ms respectively. This establishes
a desktop baseline only; it does not establish acceptable handheld performance.

Swept-coverage optimization: the particle shader now computes a conservative
48-bit candidate mask once per pixel, outside the exposure loop, and returns
the original pixel immediately when no particle can reach it. Bounds include
the travel cap, perspective expansion down to the near-depth limit and rain
stretch; retained candidates preserve compositing order. Vulkan/D3D12 reference
and combined-path suites pass, now exercising all 48 slots including slot 47.
On the same Vulkan benchmark the 720p nine-sample median fell from 2.616 to
0.533 ms, and 17 samples from 5.374 to 0.800 ms. These are workload-specific
submit-to-fence timings, not a general game-performance guarantee.

Expanded swept-bound checks pass on Vulkan and D3D12: 54 cases/backend now
include near-plane perspective expansion, far-plane crossing, offscreen centres
whose trails enter the image, very large travel with a four-pixel cap, and a
65-sample exposure twice the presentation interval. CPU/GPU tolerance remains
one byte with exact HUD/alpha. This strengthens coverage of the optimization;
it does not address the separate moving-occluder limitation.

Moving-occluder reference groundwork: `scene_shutter_occlusion` reconstructs
nearest foreground depth and fractional coverage at a shutter instant using
the model velocity/travel cap. Tests pass moving silhouette displacement,
half-covered edges, nearer stationary depth priority, HUD exclusion, pause
and invalid-input nonmutation. It is not yet consumed by the particle renderer:
joint per-sample model/particle colour integration and the GPU implementation
remain required to fix the actual runtime moving-occluder limitation.

The CPU particle reference now optionally consumes those moving-occluder guides
per sample. It blends covered/uncovered particle radiance by reconstructed
fractional coverage in linear light. A moving foreground strip test verifies
that a stationary particle is revealed for only part of the exposure, unlike
both fully hidden and fully visible controls; alpha remains exact. Reference
tests pass. This still uses a fixed input colour image: fully joint sampled
model/particle colour and the GPU path remain unfinished, so it is not claimed
as a runtime fix yet.

Foreground reconstruction now optionally gathers source colour in linear light
alongside nearest depth/coverage, retaining conditional foreground colour
separately from coverage. Tests verify nearest blue foreground rejects farther
red radiance, a half-covered red sample stays red without double premultiplication,
and fully transparent donors do not occlude. Reference tests pass. Joint
sample compositing and GPU consumption are still pending.

Joint CPU sample compositing is now available through an explicit world-only
underlay argument. Each shutter instant reconstructs foreground colour/depth,
renders particles separately over covered foreground and uncovered scenery,
then integrates their coverage-weighted radiance. Tests verify foreground
colour moves into adjacent pixels, scenery is revealed behind it, a particle
appears only during the uncovered portion, and HUD remains exact. The caller
must supply complete world guides/underlay; unknown-depth foreground and GPU
joint-exposure integration remain unresolved. Reference tests pass.

Joint reference now preserves unknown-depth world pixels in the uncovered
layer instead of replacing them globally with the underlay. Explicitly
ineligible guides and fully transparent source pixels remain byte-exact even
without a HUD tag. Added tests pass depthless-sprite preservation and protected
guide handling. This preserves content but does not invent missing geometry:
true depth ordering for unknown-depth sprites still needs authoritative depth.
The GPU joint-exposure path remains outstanding.

Joint-reference integration checks now verify that with no particles it agrees
with existing model-only reconstruction within one byte, and that the moving
occluder fixture explicitly differs from the old sequential model-blur then
particle-blur result. Both pass. GPU audit identifies stage 3 resolve, before
integral accumulation, as the required insertion point: it already has nearest
depth and conditional foreground colour/coverage. Its untouched-pixel fast path
must also account for particle coverage; a post-resolve hook alone is insufficient.

Joint GPU exposure is now implemented in `GpuMotionBlur` behind its optional
particle argument. Resolve evaluates particle colour separately over sampled
foreground and uncovered scenery, blends by foreground coverage, and only then
accumulates exposure. No-particle identity bypass remains; particle identity
frames still draw their current appearance. Vulkan/D3D12 comparisons against
the joint CPU reference pass within two bytes for valid-history and paused/reset
cases, alongside the existing regression suites. Generated SPIR-V/DXIL/Metal
source is updated. The live compositor still uses the earlier sequential path;
switching it, broader joint-path fixtures and performance work remain pending.

The resident effects compositor now routes particle frames through joint
model/particle reconstruction instead of the sequential particle pass. Updated
combined-path tests compare against joint CPU exposure followed by bloom for
all four bloom levels and reset/valid history; Vulkan and D3D12 pass. Existing
non-particle regression suites pass as well. The application executable has
not yet been rebuilt for this switch, and joint-path live/performance checks
remain pending. `build/current` is unchanged.

Joint live validation exposed missing motion/depth handles on reset frames.
Fixed identity-with-particles handling to use explicit depth-availability flags,
skip unavailable guide reads and still render current particles. Rebuilt PC;
Vulkan/D3D12 regression suites pass. The fixed stereo gameplay capture
`tmp/motion-joint-stereo-pause-fixed` passes 53 paused pairs and 46 resumed valid
pairs with enhanced scenery and bloom; final image inspected.

Added `--benchmark-joint`: 48 particles plus moving geometry, nine samples,
Vulkan median submit-to-fence times 0.887 ms at 400x224, 2.458 ms at 800x448,
7.031 ms at 1280x720. Seventeen samples at 720p costs 13.190 ms. This joint
path is substantially costlier than the separately optimized particle pass;
joint coverage optimization and handheld validation remain required.

Joint coverage optimization now stores two conservative candidate-mask words per
pixel at initialization, reusing them across shutter samples and foreground/
background evaluations. Unaffected pixels retain the model-only fast path.
This adds eight scratch bytes per pixel. Vulkan/D3D12 regression suites pass.
Same-workload Vulkan nine-sample medians improve to 0.469/0.920/2.897 ms at
400x224/800x448/1280x720; 720p seventeen-sample median is 5.384 ms. These remain
desktop submit-to-fence measurements, not handheld or overall FPS guarantees.

Candidate-mask allocation is now conditional: model-only reconstruction keeps
its original eight-byte nearest/affected buffer rather than the sixteen-byte
joint buffer. Switching modes reallocates to the required size. Same-instance
joint→model-only→joint tests at fixed extent pass against independent references
on Vulkan and D3D12, confirming no stale mask data survives either transition.

Missing-guide reset regression is now automated on Vulkan/D3D12: current
particles render correctly with neither motion nor depth, and with depth but
no motion, matching the paused CPU reference. The same incomplete input is
rejected with a cleared output handle when valid-history exposure is requested.
This directly covers the previously observed live stereo-reset failure.

Particle-presence transitions at fixed resolution now replace only the
nearest/mask buffer, retaining the output texture and colour/integral buffers.
Replacement is allocated before releasing the old buffer so a failed allocation
does not discard usable resources. Existing transition tests now also assert
stable output texture identity; Vulkan/D3D12 suites pass. Allocation-failure
injection itself has not yet been tested.

Particle evaluation now iterates only set candidate bits in ascending order,
instead of scanning all 48 slots for every sample. Both standalone and joint
shaders retain ordering. Joint fixtures now place visible debris in slot 47
with intervening offscreen emitters; Vulkan/D3D12 suites pass. Vulkan nine-tap
medians measured 0.364 ms (400x224), 0.675 ms (800x448), 2.851 ms (720p).
The 720p difference versus the previous 2.897 ms is too small to treat as a
meaningful improvement without longer repeated measurements.

Particle shutter reference: `scene_motion_blur.hpp` samples each supported
particle's own prior/current projected centre and depth across a centred
exposure, adjusts perspective radius, caps screen travel, and integrates
rendered samples in linear light. Each sample evaluates foreground occlusion;
debris rotation advances across exposure. Surface lights and optical distortions
are not assigned particle velocity. Tests pass moving-versus-static appearance,
pause/new-particle identity, HUD preservation, invalid-input nonmutation,
fully hidden particles, depth-varying partial occlusion and the travel cap.
This CPU implementation is a correctness reference, not a production full-frame
loop. GPU sampling/parity and live particle composition remain outstanding.

Live scene-history commits: the diagnostic motion path now prepares effect
correspondence before presentation and commits projected points only alongside
a successful native/stereo presentation. Pauses, cuts, rig changes and partial
stereo failures clear history. Scene frames carry previous projected samples;
each eye transforms those samples at their previous depth, separately from
current depth. Reusing an effect slot clears prior correspondence. App and
motion-reference builds/tests pass. `tmp/scene-motion-committed` exercises
weapon emitters, stereo and scripted Start pause/resume: 44 frames have matched
effect identities, all paused frames have zero matches, and the first resumed
frame has zero matches. The existing pair verifier also passes (49 paused,
46 subsequent valid blur pairs). This verifies live history lifecycle using
surface-light emitters; the particle shutter shader and moving-particle visual
integration are still outstanding. No particle blur is advertised as enabled.

Emitter identity integration: generated scene frames now carry host-side
projected motion points alongside the unchanged shader appearance payload.
Sparks/debris, shockwaves and exhaust nodes have monotonically assigned birth
IDs; heat and exhaust from one node retain distinct kind keys. Weapon lights
use emitter identity, while weather retains full signed grid coordinates.
The per-eye transform keeps motion-point positions aligned with visible effect
centres. Tests exercise actual tracker emissions: retained moving sparks/debris,
fresh identities after reset, exhaust between emissions, separate heat/exhaust
keys, and retained/new weather cells while the camera crosses a grid boundary.
Motion reference and pixel-filter suites pass; the application builds. This
metadata is not yet committed by the live presentation path or consumed by a
particle shutter shader. Particle blur remains unfinished and disabled.

Particle-motion preparation: added an independent projected-point history with
stable full identities (including signed weather-cell coordinates), rather than
matching compact render-list indices. Preparation does not commit history;
failed presentation, pause, epoch/extent changes and missing frames invalidate
correspondence. Duplicate identities are rejected on either side. Per-eye
projection transforms previous/current points at their own depths, preserving
zero motion for stationary particles and correct disparity changes for depth
travel. CPU tests pass reorder/spawn/duplicate/cut/resize/pause/failure cases,
both stereo eyes and transactional rejection of invalid previous depth.
This is reference groundwork only: emitter identities, live presentation
commit wiring, particle shutter integration and GPU parity remain unfinished.
No particle-blur runtime gate was removed and `build/current` is unchanged.

Weapon-light integration: `SceneFxFrame::surface_lighting_only` distinguishes
surface radiance from coverage-changing effects. Native-resolution mono/stereo
blur now accepts weapon lights, including simultaneous AO/DOF. Shockwaves,
particles and heat distortion remain gated rather than reusing model velocity
for their unrelated pixels; temporal-upscaled surface lighting also still needs
aligned surface metadata. Classification tests reject mixed/spatial effects and
invalid counts. The resident reconstruction fixture now covers 47 combinations
of blue/orange lighting and AO/DOF, requiring a real appearance change, exact
HUD preservation and unchanged depthless underlay. Vulkan and D3D12 pass.
The app builds; `tmp/motion-weapon-light` and `tmp/motion-weapon-light-stereo`
exercise actual sustained firing with lighting, AO/DOF and motion blur for 60
presentations. Both report nonzero lights and no declined combinations. The
mono capture was inspected; this is execution evidence, not full visual or
performance validation of every light interaction. Menu rollout remains open.

Depth-effect combination: native-resolution mono and stereo now allow ambient
occlusion and depth of field before velocity exposure. The reveal underlay
receives the same appearance settings but does not invent foreground surfaces.
A resident fixture with real normal/depth ownership, depth discontinuities,
checker colour, moving foreground and protected HUD exercises all 15 nonzero
AO/DOF quality combinations. Each actually changes the scene; combined output
matches CPU blur of the styled scene within two channel values. HUD is exact,
and the depthless background remains unchanged. Vulkan and D3D12 pass the
fixture and broader temporal suite. `tmp/motion-depth-mono` and
`tmp/motion-depth-stereo` run mode 15 with enhanced sky/ground and blur;
both-eye execution is logged and the stereo image was inspected. The app
build passes. TAA/FSR depth-effect combinations remain gated until their
surface metadata alignment is handled, not silently treated as finished.

Stereo development integration: the application now supplies committed model
history before the per-eye transform. Both current and previous poses use that
eye's off-axis camera. History commits only after successful complete stereo
presentation; all early returns invalidate the partial pair before mono
fallback. Rig changes reset history. The outer frame loop previously treated
successful stereo output as an unsuccessful temporal frame; live testing caught
and fixed that handoff. Underlay fog now uses each eye's translated scene,
projection and ground plane. Resident effects/underlay work executes in queue
order, and each finished eye is retained before scratch resources are reused.

GPU stereo checks verify lateral velocities (-5.12/-2.56 pixels) in both eyes
and zero stationary velocity, alongside existing coverage/disparity/packing
checks. App build and stereo unit tests pass. `tmp/motion-stereo-basic-fixed`
and `tmp/motion-stereo-scenery-fog` execute blur for both eyes; the enhanced
2x sky/ground/fog capture was visually inspected. `tmp/motion-stereo-pause`
verifies 45 paused pairs with no exposure, fresh history on resume, and 49
subsequent valid pairs. The pause checker now verifies both eye records.
`tmp/motion-stereo-failed-left` injects 16 partial-pair failures; every mono
fallback uses history=0 and commits no previous-model correspondence. The
capture helper checks this failure invariant. This remains diagnostic-only;
physical stereo viewing, other formats/combinations, reflections and menu
rollout still require work. `build/current` is unchanged.

Start-button pause/resume validation: actual Original 1-1 gameplay captures
`tmp/motion-taa-pause-resume` and `tmp/motion-native-pause-resume` run 120
presentations with scripted Start presses. TAA has 51 paused frames and 44
subsequent valid blur frames; native has 49 and 45. Every paused frame uses
history=0. The first resumed frame has zero previous-model correspondence and
invalid motion (TAA uses the existing unjittered fallback); subsequent blur
uses fresh history. `tools/check_motion_blur_pause.ps1` checks these transitions,
rejects missing frame records and failed/declined blur, and requires sustained
working blur before/after pause. Its self-test rejects paused exposure, stale
resume motion, no resume and skipped-frame fixtures. Capture helper accepts
`-CheckMotionPause` with `-LiveMotionBlur` for repeatable verification. These
are history/lifecycle checks, not pixel-level proof of every paused effect.

TAA development integration: the GPU blur pass now aligns geometry guides with
resolved colour using the tested nearest-contributing-surface rule. The effects
compositor can borrow the TAA world's depth/motion buffers independently while
retaining final-frame HUD ownership. A separate unjittered background recording
supplies the foreground-free underlay; enhanced appearance and optional fog
remain applied to that underlay. Neither exposure nor camera response enters
TAA history. The combination remains diagnostic-only, not menu rollout.

Vulkan and D3D12 checks pass actual TAA resolve -> aligned blur versus CPU
reconstruction, signed/zero jitter, separate guide forwarding and protected HUD.
App build and CPU blur tests pass. `tmp/motion-live-taa-basic` and
`tmp/motion-live-taa-scenery-fog` exercise the intro at 1x/2x;
`tmp/motion-live-taa-landscape` exercises enhanced outdoor sky/ground, fog,
banking and blur at 2x. The landscape has 15 resolved/blurred frames after one
first-frame unjittered fallback while motion history initializes. Images were
inspected. Five HUD ink rectangles (5,332 pixels) match the no-blur control
exactly. A broader initial rectangle included 87 changing world pixels in the
left banking margin; narrowing to the actual HUD ink required no runtime change.
This is not sustained performance/device or all-combinations validation.
DLSS, stereo, remaining effects combinations and user-facing rollout remain.

Earlier TAA combination preparation: source audit confirms TAA resolves color from the
jittered raster to the stable presentation grid; velocity guides cannot simply
be reused at the same array index after that operation. Added a CPU reference
guide resolver matching that sampling location, choosing a complete nearest
contributing depth/velocity pair instead of blending across silhouettes. It
retains destination HUD ownership and never adds sample jitter to velocity.
Tests cover signed/diagonal jitter, edges, nearest-surface invalid correspondence,
non-finite depth/vector rejection, stationary zero velocity, in-place operation
and invalid-input nonmutation. These CPU cases now underpin the GPU and live
development integration described above.

FSR1 development combination: the world-only blur underlay now uses an
independent reduced-resolution composition and FSR1 EASU/RCAS owner with the
same extent and sharpness as the scene. Full-resolution ownership/depth/motion
metadata remain attached to the resolved color. Owners release with the device
or when live blur is disabled. The capture helper requires FSR1 execution when
requested with live blur. `tmp/motion-live-fsr1` passes at 267x150 -> 400x224;
`tmp/motion-live-fsr1-camera-fog` passes at 534x299 -> 800x448 with 16 FSR1,
blur, background-fog and camera world/HUD frames each. Both final images were
inspected. The app build, CPU blur reference and Vulkan temporal/compositor
suite pass. These are integration smoke checks, not pixel-level proof of every
upscaled disocclusion or a performance claim. TAA/DLSS, stereo, other effect
combinations and menu rollout remain outstanding; build/current is unchanged.

Camera-response combination: native temporal history no longer prevents the
world/HUD camera pass when no temporal upscaler is active. Live blur is allowed
through that path: unwarped styled scene -> velocity exposure -> camera
reprojection -> HUD restoration. Vulkan and D3D12 fixtures match a separately
rendered sequence byte-for-byte with pitch/yaw/bank reprojection. The enhanced
sky/ground + fog + bank + blur capture (`tmp/motion-live-camera-fog`) records
16 successful camera frames and 16 live blur frames. All 546 checked HUD-meter
pixels match the no-blur control exactly, and the final image was inspected.
The capture helper now checks camera and underlay-fog execution when requested
with live blur. Event-driven shake/recoil variants and other combinations still
need validation; this is not full camera/blur completion or menu rollout.

Fog combination: the live motion-blur underlay now has a separate fog owner.
Its view ray continues through foreground models to the ground/sky, while
light-visibility rays still test the original geometry. CPU guide acquisition
and portable GPU fog expose this background-only view option (default remains
normal foreground depth). Vulkan and D3D12 pass 96 reference cases each,
including both view modes, eye offsets, ground and geometry, with maximum
integral error 1.34493e-6. CPU fog tests and the development app build pass.
The enhanced sky/ground + fog + blur gameplay capture (`tmp/motion-live-fog`)
records 16 successful underlay fog passes and 16 live blur frames with zero
fallbacks; its final image was inspected. Duplicate fog scene preparation and
combined performance/device validation remain work, not a performance claim.

Live development integration now exists (`capture_lava.ps1 -LiveMotionBlur`,
`STARFOX_TEST_MOTION_BLUR_LIVE`). The app creates an owned background-only
resident composite, applies matching scene appearance without presentation
overlays/history, and supplies it to the normal effects compositor every frame.
It reuses its texture until resize and releases it with the renderer/disabled
path. Blur radius scales with the render scale. This is deliberately opt-in;
there is still no finished user-facing menu control.

Original 1-1 basic, enhanced sky/ground and opening-comms captures each record
16/16 successful live frames with no unavailable-input messages
(`tmp/motion-live-basic-fixed`, `tmp/motion-live-enhanced`,
`tmp/motion-live-portrait`). The 2x smoke capture also passes. Enhanced scenery
and the comms capture were visually inspected; the checked 36x36 portrait
region matches the unblurred control exactly. Different simulation progress
between captures prevents a whole-scene pixel comparison. First-frame missing
motion originally poisoned the compositor; the identity bypass now permits
missing guides/underlay when history is invalid, with a passing regression on
Vulkan and D3D12. Both backend suites and the app build pass.

Known unsupported live combinations are explicitly reported: stereo, temporal
upscalers/AA, ray reflection/shadow inputs, geometry-driven
scene/depth effects, spatial manipulations and global distortions. These need
correct underlay/guide integration, not silently substituted raw scenery. They
remain in scope, along with software fallback, menu/settings and device testing.
`build/current` is unchanged.

Compositor integration: `GpuEffectSettings::MotionBlurPass` now accepts a
consistently styled resident underlay, settings and history validity. The SDL
effects path invokes blur after scene styling/shadows and before deferred
isolated artwork, fades, cartridge flashes/windows and host overlays. Disabled
presentation retains legacy ordering and releases the blur owner. D3D11
explicitly declines the SDL-resource option instead of silently ignoring it.
Vulkan/D3D12 tests compare the combined pass byte-for-byte with separately
rendered contrast -> blur -> circle/colour math/HUD, and test disabling it.
The broad compositor suite also passes (including 1,944 two-layer artwork,
3,456 circle, 6,912 fade and 432 shutter cases). These existing cases cover
legacy behavior, not every new blur combination. Application construction of
the styled underlay, menu controls and remaining combination/device validation
are still required; `build/current` is unchanged.

Presentation timing now commits alongside native temporal geometry history,
not whenever composition happens to succeed. `MotionBlurTimeline` invalidates
failed/uncommitted presentation, pause/resume, epoch changes, skipped serials,
nonpositive time and long gaps. App diagnostics consume this shared clock;
the old compose-time timestamp fields are removed. Clock/reference tests and
the development app build pass. The rebuilt ground capture
(`tmp/motion-committed-timing`) records 1,620 moving pixels, a valid committed
interval, GPU/reference maximum channel difference of one and zero protected
pixel changes. This also exercises the latest static-pixel shader optimization
in gameplay. Styled-world/presentation integration remains unfinished.

Static-pixel optimization: stationary surfaces seed their own depth and add
their colour locally during resolve, avoiding repeated scatter atomics.
Pixels not reached by moving silhouettes skip colour reconstruction; when a
later shutter sample first reaches one, preceding exposure is initialized from
its stationary surface or underlay. This adds no buffers and does not reduce
sample count. Both backend suites pass, including an added green stationary
surface over a blue underlay reached late by a red moving silhouette.
The latest nine-sample 1280x720 medians were 2.14 ms Vulkan / 2.40 ms D3D12;
17 samples measured 3.85 / 4.37 ms. These retain the submit-to-completion
benchmark caveats below; lower-resolution timings showed run-to-run variation.
Runtime/menu integration and full-game performance validation remain pending.

Repeatable performance probe: `starfox_gpu_temporal_aa_check <backend>
--benchmark-motion` uploads a stationary background and moving foreground,
warms two iterations and reports median/max wall time over eight submissions.
Shader compilation/uploads are excluded; command encoding, submission and
completion waits are included. GPU debug validation is disabled for this mode
and retained for correctness tests. This is not GPU timestamp timing, gameplay
FPS or a mobile-performance claim. On this development PC, the nine-sample
medians were 0.43/0.83/3.36 ms on Vulkan and 0.63/1.44/2.93 ms on Direct3D 12
at 400x224 / 800x448 / 1280x720 respectively. Seventeen samples at 1280x720
cost 6.26/5.43 ms. CPU and Vulkan correctness suites also passed during this
work. Further reduction of static-pixel work is warranted before menu rollout.
Runtime placement must consume consistently styled foreground/underlay before
cartridge wipes and host overlays; inserting raw blur before lighting would
incorrectly apply the original geometry guides to spread silhouettes.

Dispatch optimization: resolve now clears its per-pixel scratch values for the
next shutter sample, eliminating redundant full-screen clear passes. Dispatch
count is `3 * samples + 1` (28 instead of 36 at nine samples). Pause, invalid
history, long gaps and other identity settings bypass allocation/copy/dispatch
and return the input texture. Vulkan and Direct3D 12 reference suites pass,
with explicit dispatch-count and identity-texture assertions. These are work
reductions, not measured FPS improvements; frame-time benchmarking remains.

Resident implementation: `GpuMotionBlur` now reconstructs centred-shutter
silhouettes using native colour, ownership, depth, motion and a world-only
underlay. Per-sample nearest-depth and fixed-point colour atomics avoid float
atomic requirements and order-dependent sums. The enqueue path has no CPU
downloads or fence waits, preserves protected pixels/alpha, clamps radius and
handles pause/history cuts. Generated SPIR-V, DXIL and MSL are current; Metal
has only been compiled offline, not device-tested. The initial implementation
used four dispatches per shutter sample; the optimization above supersedes it.

Vulkan and Direct3D 12 suites pass CPU/GPU comparisons for 3/9/17/65 samples,
fractional/vertical/extreme/zero motion, a near occluder, even sample counts,
HUD, pause/cut/long-gap identity, invalid radius and release/reacquire. A finite
extreme-vector normalization issue found on Vulkan was fixed. The gameplay
diagnostic now also executes this resident pass and downloads its result only
for comparison. Opening-flight and ground captures (`tmp/motion-gpu-intro`,
`tmp/motion-gpu-ground-fixed`) both differ from the reference by at most one
8-bit channel value, with zero protected-pixel changes. Opening output was
visually inspected. Gameplay comparison also exposed and fixed a reference
bug that erased unrelated unknown-depth ground dots; a regression test passes.

Not yet a menu/presentation effect. Runtime ordering, quality controls,
performance, combinations, stereo/upscalers and mobile validation remain;
hidden-model reconstruction is still limited by the available underlay.
`build/current` has not been replaced.

Live native gameplay reference capture is now available through
`capture_lava.ps1 -MotionBlur`. It enables native temporal motion, captures
the final requested frame and builds a separate background-only composite
without models or late HUD. It saves source, underlay and reconstruction;
normal presentation never performs these diagnostic downloads. Elapsed
presentation time, history epoch/serial and pause state gate reconstruction.
The native mono diagnostic rejects reduced temporal raster paths rather than
silently using incorrectly scaled guides.

Development PC build and motion-blur reference tests pass. Original 1-1
captures cover the opening flight and ground/obstacles. The final ground
capture (`tmp/motion-gameplay-ground-verified`) records 1,560 moving guide
pixels, 1,038 changed pixels and zero changes to protected pixels. Source and
underlay were visually inspected; the latter excludes models/HUD. This proves
live input/reference wiring, not final GPU blur quality or performance.
Resident GPU implementation, menu integration, hidden-model reconstruction,
temporal upscaler/stereo compatibility and device validation remain pending.

The temporal GPU fixture now uploads motion/depth and visible ownership,
composes them through `GpuComposite`, reads decoded guides and reconstructs a
moving silhouette against a supplied underlay. It verifies outward spread,
background reveal, HUD preservation and history-cut invalidation. Vulkan and
Direct3D 12 temporal/composition suites pass with this test. This is resident-buffer
fixture validation, not a gameplay capture or a production resident blur pass.

Native-data bridge: a tested adapter decodes compositor ownership, axial depth
and unjittered float4 motion. It rejects stale depth/motion pairs, invalid history
and HUD, while retaining stationary occluder depth and admitting explicitly
flagged world sprites. Added an opt-in compositor readback for reference and
diagnostics; it is not called by the production frame loop. Motion tests and
the existing GPU composition suite pass, including missing-motion rejection
without output mutation. Successful live native-buffer reconstruction and a
resident GPU blur still require integration/validation.

Added a forward silhouette reconstruction reference with a required world-only
underlay. Each shutter sample resolves nearest depth before accumulating linear
colour; uncovered pixels reveal the underlay rather than inventing hidden
scenery. Tests pass for outward silhouette spread, centre coverage reduction,
near-occluder rejection, HUD protection, 60/120 FPS parity, in-place output,
scene-cut identity and transactional rejection of a missing underlay. This is
not yet runtime/GPU code. The underlay contract and native depth/motion sources
still need wiring; geometry hidden behind another model is not reconstructed
by a backdrop-only underlay and remains a known integration limitation.

Added a linear-colour, depth-aware centred-shutter surface kernel driven by
unjittered previous-minus-current pixel vectors and their elapsed interval.
Exposure is measured in seconds, with bounded radius and sample count. Tests
cover equal velocity at 60/120 FPS, vector direction, HUD/alpha preservation,
occluder rejection, pause/cut/long-gap identity, in-place output and invalid
settings. This is a reference only: foreground silhouette dilation, native
motion-buffer acquisition, GPU/runtime/menu integration and performance remain
required. It does not claim the full motion-blur feature is finished.

## Volumetric integration core (2026-09-27)

Stereo dispatch cleanup: fog is now generated lazily for mono presentation,
not precomputed before both eye passes. Four-frame capture
`tmp/fog-sbs-no-mono` records eight eye fog dispatches and zero mono fog
dispatches, with distinct SBS eyes. Injected stereo-presentation failure
(`tmp/fog-sbs-fallback`) records four successful mono fog passes. A failed
native fog allocation/dispatch returns to the CPU path instead of silently
dropping fog. This removes a redundant pass, not a measured overall FPS claim.

Interaction checks: six-frame Original 1-1 captures retain fog with FSR1
(267x150 to 400x224) and TAA (five resolved frames after the initial unjittered
fallback). SBS now creates independent per-eye fog scenes, off-axis projection
centres and translated ground planes, rather than reusing mono depth.
Half/full SBS captures pass distinct-eye checks (`tmp/fog-sbs-per-eye`,
`tmp/fog-sbs-full-per-eye`). Expanded GPU reference checks pass 48 cases on
Vulkan and Direct3D 12, including translated eye geometry; maximum measured
error remains 0.00000135. Axial GPU depth rays plus a small dimensionless edge
tolerance prevent float-rounding holes at triangle boundaries. These are
desktop captures, not 3D-TV/headset or mobile validation. SBS currently also
computes a redundant mono fallback fog pass; removing that cost remains open.

Menu/config integration: Off/Low/Medium/High is now action 76 in Global
Enhancements, with `VOLUMETRIC_FOG` config and backward-compatible trailing
save-state storage. Defaults Off; invalid settings are rejected. Runtime input,
simulation and fog tests pass. Menu captures `tmp/fog-menu-controls` and
`tmp/fog-menu-enabled` verify the row and actual Low activation without the fog
diagnostic override; the latter logs successful Direct3D 12 fog passes.
Turning the option off releases GPU fog resources. Full interaction/performance
and platform coverage are still pending; this is development-only.

GPU progress: added a resident linear scattering/transmittance buffer pass
with scene BVH occlusion, nearest-surface/ground termination and bounded sky
integration. Vulkan and Direct3D 12 numerical checks compare 16 cases against
the CPU integrator (empty/occluded geometry, tilted ground, 1/16/64/128 samples),
plus zero density, invalidation, resize and device release/reacquisition.
Vulkan maximum measured error is 0.00000135. Shader generation also produces
Metal, but it has not run on an Apple device. The GPU buffer is not yet wired
into gameplay composition or the menu; no release-build deployment is claimed.
CPU guide rays now avoid normalization/reconversion rounding at triangle edges.

Resident composition now decodes scene RGB to linear, applies scattering and
transmittance, then encodes back to display RGB. The pass runs before source
flash/fade/window overlays and excludes HUD and zero-alpha pixels. Vulkan and
Direct3D 12 tests pass within one RGB byte of the CPU reference, with exact
protected pixels/alpha and a clean fog-on-to-off transition. Resource dimensions
and device identity are checked before dispatch. Diagnostic gameplay wiring is
under validation; menu, upscalers/stereo and platform validation remain open.

The rebuilt development executable now captures four Original 1-1 frames with
`volumetric-fog: gpu=1 GPU fog: vulkan` and resident composition/presentation
(`tmp/fog-gpu-gameplay`). Visual inspection retains sky/model colour and readable
HUD. The diagnostic still collects CPU caster geometry before uploading its BVH;
this is not a zero-upload/performance-complete implementation. User-facing
`build/current` is unchanged.

Added reference guide construction from nearest camera-space scene geometry
and an optional ground plane. Guides retain axial depth, identify sky without
inventing ground in space, and use explicit world coverage to exclude HUD.
Tests pass for a model in front of ground, off-axis plane depth, absent ground,
HUD exclusion and transactional rejection of an invalid plane. Runtime wiring
and GPU guide/lighting integration remain pending.

Prepared directional-light triangle queries are now shared across the reference
frame. Ambient-only and empty-scene fog use the exact homogeneous solution
instead of marching samples. Transmittance uses its closed form in both paths,
avoiding sample-count-dependent rounding. Tests cover prepared-query parity
and rejection of stale geometry after a scene rebuild. These are reference
optimizations; no runtime performance improvement is claimed yet.

Added a depth-aware linear-RGBA reference compositor. Explicit eligibility
protects HUD/portrait pixels, alpha is preserved, axial camera depth is converted
to ray distance off-axis, and sky uses the bounded integration distance.
Tests pass for surface termination, off-axis projection, sky, exact HUD/alpha
preservation, in-place operation and transactional rejection of invalid depth.
This remains a reference pass: runtime depth inputs, GPU acceleration and
in-game visual verification are not yet connected.

Added a homogeneous single-scattering reference integrator in linear RGB.
It integrates Beer attenuation along a surface-bounded view segment, with
directional phase response and an actual scene-geometry occlusion query at
every sample. Ambient scattering remains present in shadow. Sampling is
deterministic and not frame-rate driven; sky integration is distance bounded.
Dedicated tests verify analytic attenuation/scattering across 1–128 samples,
full and partial geometric shadows, ambient light, zero density, zero surface
distance, sky bounds and invalid density. This is a tested core, not yet a
visible/menu-accessible effect: depth integration, accelerated rendering,
controls and scene/device validation remain required.

Verify each through implementation inspection, targeted tests, and actual scene
captures before marking complete. Mobile/console/VR device validation must not
be inferred from desktop shader tests. The goal remains active until every
agreed feature has corresponding evidence, not merely a menu entry.

## Water-caustics work in progress

### Camera-response core (2026-09-27)

Added an isolated presentation-only response tracker for independent impact,
recoil and banking strengths. Damage decreases and shot identities trigger
bounded angular impulses; banking uses an exact critically damped spring.
No simulation state/RNG is changed. Unit checks cover identical event sequences
at 60/120/240 Hz, pause/resume, repeated presentations, scene cuts, rewinds,
disabled settings, nonfinite inputs, damage versus healing and angular bounds.
The dedicated test executable passes. This is not exposed or applied to the
game yet: authoritative shot/damage wiring, common sky/ground/model/reflection
camera composition, config/menu and visual testing remain required. Headset
tracking must not be disturbed by this optional flat-screen effect.

Fixed long-pause handling: the discontinuity guard no longer resets a paused
response after one second; five-second pause/resume tests pass. Added the
common pure-rotation perspective reprojection helper, with exact identity,
inverse round-trip and render-scale invariance checks at 1x/2x/4x. It is intended
for composed world imagery before HUD, avoiding independent layer offsets.
This helper is not yet used by presentation; overscan/edge coverage and GPU
integration remain necessary before enabling the effect.

Added a world-only software reference reprojection pass, intended before HUD
composition. It solves a bounded perspective crop from transformed frame
corners to prevent exposed borders, then samples bilinearly. Tests pass for
identity, in-place use, multiple aspect ratios, bounded extreme rotations,
opaque edge coverage and leaving output intact on invalid poses. It is still
not called by gameplay: GPU parity and an actual world/HUD composition boundary
must be wired before menu exposure. The crop is deliberate, not a claim of
rendering additional offscreen geometry.

Added portable GPU stage 39 for the world-only perspective reprojection,
sharing the CPU crop calculation and using existing stage-local uniforms.
Portable DXIL/SPIR-V/Metal generation and freshness checks pass; CPU tests pass.
Added three-aspect-ratio CPU/GPU comparison fixtures. Their executable build is
still in progress in legacy FXC shader compilation, so GPU parity is not yet
claimed. Runtime scene/HUD separation and event/menu integration remain pending.

The pending shader/test build finished successfully. Complete effects suites
run with `SDL_GPU_DRIVER=vulkan` and `direct3d12` both pass, including the new
three-aspect-ratio camera-response parity fixtures (maximum allowed rounding
error one byte). CPU composition now has an explicit world/final/coverage
contract, tested with black and same-colour HUD ink and alias-safe output.
Inspection found the existing TAA/FSR world-only compositor and HUD restoration
path can supply the required clean world. Runtime wiring is still outstanding;
these tests do not claim a working gameplay camera effect.

Resident composition validation now exercises the runtime's actual
`GpuTemporalInputs::restore_hud` against the CPU camera composite reference.
Direct3D 12 and Vulkan suites both pass, including opaque black and same-colour
HUD ink. `preserve_artwork=false` is required for this use: sky/tilemap world
pixels must retain the camera transform rather than being copied back in their
unrotated positions. The complete TAA/SMAA/MSAA suites pass alongside it.
This validates the composition component, not gameplay/event/menu wiring.

First end-to-end runtime path added behind `STARFOX_TEST_CAMERA_BANK` (capture
tool `-CameraBank`). It retains a pre-HUD world, renders the same enhancements
into a separate resident world texture, applies the camera pass, restores HUD
from the enhanced original and publishes/captures the final result. PC build
passes. `tmp/camera-response-runtime/lava.bmp` and the `-off` companion show
the camera-bank transform over water/caustics/reflections; runtime logs confirm
resident world/HUD execution. This is a diagnostic integration, not a released
feature: event triggers/menu, CPU runtime, dialogue overlays and temporal-AA/
upscaler combinations still need integration. Extra world rendering also needs
cost review before enabling this outside tests. `build/current` unchanged.

Diagnostic gameplay activation now observes the active player's health and
signed roll via `STARFOX_TEST_CAMERA_RESPONSE` (`-CameraResponse` in capture).
Only impact/banking bits are accepted; recoil is intentionally not connected
to generic beam shapes, which include enemy fire. Scene changes/inactive
player reset through the existing tracker. PC build passes and
`tmp/camera-response-player` confirms bank input follows scripted right input
and produces smoothly changing camera roll through the resident world/HUD path.
Health stayed 255 in that capture, so an actual-hit impact test is still needed.
No menu option is exposed yet; temporal reconstruction, software runtime and
dialogue compatibility remain unfinished.

Corrected impact source after runtime inspection: player model HP stays 255;
the actual shield is the collision-box value copied into `MeterState::damage`.
Observation now uses side-effect-free `peek_meter_state()`, selecting
`damage_two` for EX's second-player view. Capture tool can explicitly disable
God Mode. PC build and `tmp/camera-response-impact` gameplay run pass: a real
hit reduces shield health from 40 to 34 and produces nonzero, decaying impact
pitch/yaw through the resident presentation path. The prior health=255 test
did not validate impacts. Second-player gameplay capture is still outstanding.

### Contact-shadow audit (2026-09-27)

The existing shadow rays sample an angular emitter, not an image-space blur.
Added physical-separation regression fixtures at all three ray qualities:
caster/receiver gaps of 1, 100 and 500 world units must produce zero, then
strictly increasing exposed penumbra widths. Software geometry tests pass.
The DXR and portable Vulkan-compute checks also pass with exact CPU/GPU masks
for these fixtures; their full existing suites pass. Thus distance-dependent
contact hardening already exists in the area-light implementation. This does
not yet close the enhancement: a separate user-facing control, gameplay visual
validation and native Metal/Vulkan hardware verification remain outstanding.
No fixed-radius postprocess was substituted for geometry-dependent shadows.

Backend parameter added: `Camera::shadow_softness` selects Hard/Low/Medium/High
angular radii 0/.0075/.015/.03, independent of ray-count quality. Default Medium
preserves the former emitter exactly; Hard reduces to one central visibility
ray. Software, DXR, portable compute, Vulkan hardware and Metal source all use
the parameter. Additional DXR and Vulkan-compute fixtures verify strictly wider
penumbrae across the four settings and exact agreement with software; full
backend checks and shadow geometry tests pass. Native Apple/Linux hardware
compilation/testing, saved preference and menu/runtime wiring are still pending.

Menu/runtime wiring now complete on desktop: SHADOW SOFTNESS below Ray Tracing,
config key `SHADOW_SOFTNESS`, append-only game-state field, default Medium.
Stereo shadow cameras inherit it from the mono shadow camera. Runtime-input
and simulation suites pass (save/load, invalid config rejection, menu cycling,
reverse wrap, state round trip). PC build passes. Menu visually checked at
`tmp/shadow-softness-menu/menu.bmp`. Original 1-1 Hard versus High captures in
`tmp/shadow-softness-hard` and `tmp/shadow-softness-high` visibly change caster
edges from hard to soft; 3,547 pixels differ, bounded to (147,132)-(615,370).
Native device validation remains pending. `build/current` has not been updated.

- Added a shared scalar reference using the existing ray-water normal field,
  air-to-water Snell refraction, inversion of the light landing map, and its
  area Jacobian to compute focused irradiance below the water.
- World-position calculations use displacement differences to avoid loss of
  precision. Pixel-footprint filtering suppresses distant wave frequencies;
  irradiance is bounded and attenuated with receiver depth.
- Pixel tests pass for dry-surface rejection, moving light concentration,
  deterministic/time-continuous evaluation and distant-frequency filtering.
- Added a CPU receiver pass reconstructing world position and orientation from
  surface depth/normals and camera-to-world projection. Water-footprint and
  light-path visibility callbacks are mandatory rather than assuming the entire
  plane is water. The refracted entry point is available to both callbacks.
- Receiver tests pass for submerged illumination, water coverage, complete
  occlusion, HUD/alpha preservation, stale palette/depth rejection, missing
  surfaces, above-water surfaces, downward normals and disabled behavior.
- Added the actual CPU scene-BVH visibility adapter. It traces both the
  refracted underwater segment and the air segment toward the sun, converting
  world coordinates into the scene's camera space. Geometry tests pass for
  underwater blockers, above-water overhangs, off-path geometry and rotated /
  translated cameras. It is not yet called by the presentation pipeline.
- Still needs menu exposure, remaining backend integration and scene captures;
  the water-caustics requirement remains incomplete.
- Runtime inspection found that the current water surface is opaque. A
  submerged-object-only pass therefore cannot deliver visible caustics in most
  scenes; integration must also address the transmitted/bottom water shading.
- User explicitly approved making the water renderer transparent (2026-09-27).
  Preserve wave-normal Fresnel reflections, add refracted submerged geometry
  and depth-dependent absorption; keep mirror, gold and lava opaque. This is
  required implementation work, not evidence that transparency already works.
- Added the default-off `WATER_CAUSTICS` 0–3 preference and append-only game-state
  serialization, including application save/load plumbing. No inactive menu row
  is exposed.
- DXR transmitted-water shading now applies focused irradiance to submerged
  triangle hits with two geometry visibility queries. Hardware tests verify
  lighting changes, deterministic repeats and blocking by an opaque overhang.
  Vulkan implements the same path and its SPIR-V generation passes; Vulkan
  hardware execution has not yet been verified. CPU reference gain now operates
  in the same approximate linear-light space as both shaders.
- Transparent water transmission is implemented in DXR/Vulkan, preserving
  Fresnel reflection and wavelength-dependent absorption. DXR fixtures verify
  submerged colours remain visible while mirror/gold/lava stay opaque. Software,
  compute and Metal water transport still need integration. A natural submerged
  receiver/bottom and gameplay visual validation are still required, rather than
  treating the synthetic fixture as proof of the complete gameplay effect.
- Gameplay smoke captures: `tmp/caustics-gameplay-high/lava.bmp` and
  `tmp/caustics-gameplay-off/lava.bmp` (Original 1-1, manual water, paced).
  Both execute the resident ray-water path. Visual inspection shows intact
  waves/reflections/HUD but no convincing visible caustic receiver. Next work
  must provide meaningful submerged/bottom shading in gameplay, not simply
  expose the hidden setting. Capture tooling now accepts `-Caustics 0..3`.
- Added an analytic sandy bed 640 world units below water in DXR/Vulkan.
  Refracted rays terminate at the bed; closer submerged geometry retains its
  authored material. Beer absorption and geometry-occluded caustics shade the
  bed. DXR tests verify caustic response and that deeper geometry cannot shine
  through the bed; the full DXR suite passes. Vulkan shader generation passes.
  `tmp/caustics-bottom-high/lava.bmp` shows the resulting underwater lighting
  in gameplay; further off/on visual tuning and remaining backend integration
  are still needed. This has not been deployed to `build/current`.
- The off/on bed captures (`tmp/caustics-bottom-off`, `tmp/caustics-bottom-high`)
  have been visually reviewed. Applied the caustics pixel-footprint filter to
  DXR/Vulkan water normals too, suppressing unresolved distant ripples rather
  than using a differently filtered normal field for transmission/reflection.
  Vulkan generation and the complete DXR suite pass after this change.
- Metal integration gap confirmed in the native call: `render_reflections`
  receives ground but no `RayWater`, so its shader cannot implement these
  water parameters yet. Do not report the desktop implementation as iOS-ready.
- Started closing that gap: Metal's API and 144-byte reflection parameters now
  carry `RayWater`, validate numeric inputs, and shade water separately with
  world-anchored filtered normals, refraction, the sandy bed, absorption and
  Fresnel reflection. The shader preserves the water output tag. This native
  source has NOT been compiled or device-tested on Windows. Caustics, authored
  backdrop parity and transparent-texel traversal remain unfinished in Metal;
  the existing model-only path remains for non-water materials.
- Metal source now also applies the shared caustic kernel, receiver-normal
  weighting and underwater/overhead visibility rays. CMake embeds the scalar
  `.inc` files into the runtime Metal shader source and tracks their changes,
  avoiding a separate copied optics implementation. CPU kernel tests pass;
  native Metal compilation/device validation and backdrop/alpha parity remain
  outstanding. The shader addition alone is not evidence of iOS completion.
- Metal reflection source now traverses palette-transparent hits for primary,
  reflected, transmitted and caustic-visibility rays. Native validation is still
  required. Added `tools/check_metal_rt_shaders.py` to extract the exact runtime
  shaders plus shared kernels and compile them with the selected Apple SDK;
  `tools/build_apple.sh` invokes it before packaging. Windows `--emit-only` ran
  successfully but is explicitly NOT a native compilation result.
- Metal now receives `GpuBackgroundDraw`, renders the authored 1664x224 BG2
  panorama through `GpuBackground` in the reflection command, retains its SDL
  buffer and binds it directly to the Metal shader (no CPU image readback).
  Reflection misses and water body shading sample that panorama and live
  palette. Enhanced photographic sky sampling remains to be ported. Shader
  extraction passes; this is still uncompiled native source on this host.
- Metal enhanced-sky sampling is now ported in source: tilted horizon,
  photographic bilinear sampling, panorama seam overlap, protected authored
  planet/moon ellipses, live palette/brightness and fallback to original BG2.
  Per-in-flight-slot `BackdropUploadCache` avoids unchanged image uploads;
  disabled skies leave the metadata inactive. Runtime shader extraction and
  whitespace checks pass. Native Metal compile and device visual checks remain
  required; no claim of platform parity is based on source extraction alone.
- Added `water_receiver.hpp` CPU submerged transport using the scene BVH,
  captured material colours, sandy-bed fallback, linear absorption and the
  shared caustic/visibility calculation. Pixel tests now verify submerged
  red/green materials, caustic response, upward-ray rejection and overhead
  occlusion. Tests pass. Presentation integration and bounded-resolution
  performance work remain; this helper is not yet a playable software effect.
- Connected the hidden caustics preference to software scene-material capture
  and the CPU environment pass. It reconstructs the water intersection, filtered
  wave normal and refracted ray from the camera, then runs BVH receiver transport.
  Sun visibility preparation is reused across the pass. CPU water executes before
  colour math and is not replaced by the later GPU environment pass. Pixel tests
  and PC build pass. First software smoke capture:
  `tmp/caustics-software-first/lava.bmp` (1x, manual water, RT off). HUD is intact;
  coarse-quality sampling/performance, stereo, and visual parity need further
  checks. The option remains hidden and `build/current` has not been updated.
- Cached/uncached receiver-visibility equivalence tests pass across 32 rays.
  Added opt-in CPU water pass timing via `STARFOX_TRACE_SCENE_COST`. A 60-frame
  1x software/high run (`tmp/caustics-software-timed/runtime.log`) averages
  49.83 ms for the environment pass (32.38–79.43 ms). This FAILS the performance
  requirement. Full-resolution per-pixel tracing must be replaced with bounded
  sampling/reconstruction before exposing or shipping this fallback. Successful
  captures are not performance acceptance evidence.
- Replaced per-pixel software water tracing with a world-projected sample grid
  (High 4x4, Medium 6x6, Low 8x8 logical pixels), scaled with Render Upscale.
  Reconstructs linear radiance with bilinear weights; full-resolution ownership
  still selects affected pixels. Wave filtering uses the grid footprint. Surface
  sampling tests and the full pixel suite pass. Same 60-frame 1x/high capture in
  `tmp/caustics-software-grid` averages 12.85 ms (10.26–27.00), down from 49.83 ms.
  This is an environment-pass measurement, NOT overall frame-rate improvement.
  Further profiling/quality validation remains before release.
- Removed the legacy per-pixel procedural-water evaluation where grid transport
  already replaces it. Clean 60-frame measurement in
  `tmp/caustics-software-no-redundancy-clean` averages 9.66 ms (8.36–17.32).
  Added environment integration tests at 1x/2x/4x for HUD/model/textured-model
  ownership, actual ground shading and alpha preservation; pixel suite passes.
  This remains too expensive to assume weak-device readiness.
- Split timing (`tmp/caustics-software-split`) identifies grid preparation /
  tracing at 7.11 ms out of 9.96 ms total average. Changed worker partitioning
  from short grid rows to independent sample cells, preserving the sampling
  pattern while avoiding the pool's 16-row threshold limiting concurrency.
  Rebuild and post-change performance verification are pending.
- Cell-worker rebuild and pixel tests pass. Added exact single-thread versus
  four-worker image comparisons at 1x/2x/4x; outputs match. Clean 60-frame
  `tmp/caustics-software-cell-workers` averages 2.87 ms grid / 5.47 ms environment
  at 1x High, versus 7.11 / 9.96 before the scheduler change. Low averages
  0.87 / 3.36 ms (`tmp/caustics-software-cell-low`). 2x High averages 2.32 / 7.73 ms
  (`tmp/caustics-software-cell-2x`), confirming the ray grid cost does not grow
  quadratically with Render Upscale. These are this PC's pass timings, not
  whole-frame or weak-device performance guarantees.
- Replaced finite-difference caustic Jacobians with analytic Snell/wave
  derivatives in the shared kernel. Tests compare displacement and all four
  derivative terms with central differences over 384 depth/footprint/position
  combinations. Pixel and complete DXR suites pass, as does Vulkan shader
  generation. Metal source extraction uses the same kernel but still does not
  establish native compilation. Completed 60-frame gameplay timing in
  `tmp/caustics-software-analytic`: 1.58 ms grid / 4.15 ms environment averages,
  compared with 2.87 / 5.47 before this kernel change (same PC/test settings).
- Fixed mono/stereo water submissions using zero ray quality when reflections
  were Off (Vulkan/Metal reject that dispatch). Water now retains at least Low
  dispatch quality while its reflection strength remains zero. PC build and
  gameplay smoke `tmp/caustics-no-reflections/lava.bmp` pass; capture tooling
  confirms the resident water path executes with reflections Off. This does
  not substitute for Vulkan/Metal hardware validation.
- Water Caustics now has a Global Enhancements row below Reflective Surfaces,
  cycling Off/Low/Medium/High. Unsupported GPU hardware displays NEEDS HW RT;
  disabled GPU ray tracing displays RT OFF. Software exposes the independent
  bounded-grid control. PC build and simulation tests pass, including reachability,
  left/right wraparound and state persistence. Remaining compute, stereo and
  native device gaps are not waived by menu exposure. `build/current` unchanged.

- Desktop stereo-water smoke check: `tmp/caustics-stereo-full/lava.bmp` has
  distinct 400x224 eye images; the runtime confirms all 20 presentations used
  the packed 800x224 stereo path, both resident shadow eyes, and ray-water with
  reflections Off. Half-SBS capture also has distinct eyes. Visual inspection
  shows water in both eyes. The broad HUD-only-region check is inapplicable to
  this capture because water continues behind the HUD; the top life-counter
  rectangle matches exactly. This is not headset/3D-TV validation or complete
  per-eye geometric accuracy proof. Pixel-filter tests pass again. Stereo
  captures now enable GPU tracing automatically. `build/current` unchanged.

- Software transmission no longer depends on Caustics being enabled. Auto
  refines the early water candidate against the actual environment classes
  before collecting casters, avoiding that cost on dry terrain. Grid integration
  tests now cover all four caustic levels at 1x/2x/4x, including alpha, ownership
  and threaded equivalence. Pixel tests and PC build pass. Gameplay capture
  `tmp/water-software-caustics-off/lava.bmp` executes the transmission grid with
  both caustics and reflections Off; it appears subdued without those lighting
  terms. `tmp/water-software-dry-auto/runtime.log` confirms no CPU water pass on
  Corneria Auto. These do not establish compute-GPU or native-device parity.

### Transparent-water integration (2026-09-27)

- DXR and Vulkan ray-query water now trace a refracted ray below the analytic
  surface to actual scene geometry, then apply wavelength-dependent absorption
  in linear light. Misses retain water-body colour rather than showing sky
  through the floor. Existing wave normals and Fresnel reflection remain.
- Shared optical-math tests pass for shallow transparency, depth falloff,
  red/green/blue absorption ordering and bounded radiance.
- `starfox_dxr_check` passes, including the new submerged red/green geometry
  fixture and proof that mirror, gold and lava do not transmit those colours.
  Existing reflection and shadow checks pass (`tmp/water-transmission-dxr.log`).
- Vulkan shader regenerated/compiled, but not hardware-tested this turn.
  Software/compute fallback and Metal transmission remain unimplemented.
- Native ray-water gameplay capture inspected:
  `tmp/water-transmission-scene/lava.bmp`; its log confirms resident ray-water.
  This is a visual smoke check, not the submerged-geometry correctness fixture.
- Updated `build/current/starfox_pc.exe`; release/current SHA-256:
  `A3051C7A2B4C1F7A220DFF73C1ECE2C70C821627922CB7410BBEE54EE25CC699`.
- Caustics themselves are still not connected to water shading or presentation.

## Adaptive-exposure verification (2026-09-26)

- Added an allocation-free CPU reference with trimmed log-luminance metering,
  linear-light exposure, bounded gain and time-based asymmetric adaptation.
- Pixel tests pass for 60/120/240 Hz agreement, neutral scene-entry/reset,
  pause stability, HUD/alpha protection, black/transparent frames, and bright
  and dark scene response.
- Menu, configuration/state serialization, CPU fallback and GPU-local tiled
  histogram/reduction/application are wired and tested. The option is distinct
  from Contrast and defaults off. Low/Medium/High bound gain to ±0.5/1/1.5 stops.
- Vulkan and D3D12 full effects checks passed (`tmp/exposure-vulkan.log`,
  `tmp/exposure-dx12.log`): all qualities and presentation rates match the CPU
  within one byte, including paused wall-clock advancement, scene resets,
  intermediate resizes, separate output histories and exact resource cleanup.
- Pixel-filter, runtime-input and simulation tests passed. Isolated highlights
  and HUD do not pump the reference meter; invalid/off input remains a no-op.
- Actual paced native GPU captures: `tmp/exposure-high/lava.bmp` and
  `tmp/exposure-space-high/lava.bmp`, with corresponding `exposure-off` and
  `exposure-space-off` captures. Dynamic geometry differs with capture timing;
  these are runtime/visual smoke checks, not pixel-identical regression fixtures.
- Software and D3D11 fallback captures: `tmp/exposure-software/lava.bmp` and
  `tmp/exposure-legacy/lava.bmp`. Menu layout: `tmp/exposure-menu/menu.bmp`.
- Portable shader generation/check passed; Metal/mobile/VR device validation
  is not implied. Output histories are isolated, not binocularly metered.
- Release and `build/current/starfox_pc.exe` SHA-256 match:
  `00179174A4B9C03D72C651F4BCD50751495D612B6FC574A38A2DC6E1B54978FE`.

## Depth-effects verification (2026-09-26)

- Pixel tests: a flat focused plane is unchanged, an occluder darkens nearby
  surfaces, AO never brightens, and missing depth is a no-op.
- Settings and simulation tests: both menu controls, config and state round trips.
- Vulkan/D3D12 reference tests: AO, DOF and combined output match the CPU within
  one byte; HUD and alpha are unchanged. The last of 48 scene-event slots is
  covered too. SDL uniform blocks are each below Vulkan's 4096-byte binding range.
- Native gameplay captures: `tmp/depth-final-ao/lava.bmp` and
  `tmp/depth-final-dof/lava.bmp`, compared with `tmp/depth-off/lava.bmp`.
  AO changes 10,161 pixels, DOF 7,227; neither changes the lower HUD region.
  Visual inspection confirms a sharp player and softened distant surfaces.
- Global menu capture: `tmp/depth-menu/menu.bmp`.
- Combined-effect gameplay smoke captures: `tmp/depth-d3d11/lava.bmp` and
  `tmp/depth-software/lava.bmp`. Final executable copied to `build/current`;
  SHA-256 `5A1B0E6E99ECCC0CB045DCE4A44501720C008CE4E837D4E60919EA4E918AB4FE`.
- Portable Metal generated, but mobile/console/VR hardware is not validated here.

## Particle/exhaust verification (2026-09-26)

- `starfox_pixel_filter_tests`: health-loss and destruction triggers; all three
  densities; 24-particle cap; motion, expiry, pause, scene-reset and disable
  behavior; independently enabled heat; hull-depth protection and near-camera
  plume bound.
- Runtime config and simulation tests: both controls are reachable, persisted,
  and round-trip through config and game-state serialization.
- Vulkan and D3D12 effects checks: all nine event types agree with the CPU
  reference, including depth-aware heat source taps, with HUD and alpha protected.
- Actual Original 1-1 damage/destruction: `tmp/particle-impact/runtime.log`
  records nine sparks and three debris pieces; corresponding frame captures
  show the emitted debris during the destruction sequence.
- Menu: `tmp/particle-menu/menu.bmp` shows both independent controls.
- Final heat on/off comparison: `tmp/particle-heat-local/lava.bmp` versus
  `tmp/particle-heat-local-off/lava.bmp`. There are 1,897 changed pixels in the
  localized bounds (354,175)-(457,262); HUD lies outside that region. Visual
  inspection confirms a sharp hull. The initial soft-hull and oversized plume
  captures were rejected and corrected, not used as completion evidence.
- Metal shader generation is current; device validation remains separate.
- Legacy D3D11 gameplay smoke capture: `tmp/particle-legacy/lava.bmp`.
  Deployed `build/current/starfox_pc.exe` SHA-256:
  `8534D909D0CF5C655364EB8151881FEFA9B3E5753653527EE9CDC3CBAAA17940`.

## CRT persistence verification (2026-09-26)

- CPU tests check the RGB decay numerically, world/model coverage, HUD/alpha,
  clearing covered history, scene reset, disabled CPU allocation release, and
  equivalent decay at 60/120/240 Hz.
- Vulkan and D3D12 reference tests cover all three quality levels at those
  frame rates, intermediate passes/resizes, scene resets, combination with
  model Trails, and independent mono/left/right history slots.
- Settings and simulation tests cover the new menu control and config/state
  round trips. Old config/state data defaults it Off.
- Paced native GPU gameplay: `tmp/phosphor-high/lava.bmp`; software fallback:
  `tmp/phosphor-software/lava.bmp`. Both visually show short afterglow on moving
  scenery/models and retain sharp HUD text. Menu: `tmp/phosphor-menu/menu.bmp`.
- Portable shader freshness check passes. Mobile/console/VR device testing is
  not implied by desktop tests or generated Metal.
- Deployed `build/current/starfox_pc.exe` SHA-256:
  `4B9C6E287E5D99413359CA3A5FB52B62BA788B45037C22311E52489DBDE82CF2`.

## Camera recoil ownership validation (2026-09-27)

### Live comms and SBS coverage (2026-09-27)

- Inspected overlay assignment: excluded subtractive portrait/text layers are
  planet-briefing layers. Normal flight comms use the tagged gameplay HUD.
- Captured Original 1-1 at preroll 260 with “READY, FOX!” visible. All 1,404
  portrait-box pixels match the camera-Off control on both GPU and software
  (`tmp/camera-comms-260`, `tmp/camera-comms-260-software`,
  `tmp/camera-comms-260-off`). Earlier 160/400 probes did not show comms and
  were not used for that claim.
- Half and Full SBS captures each execute 40 camera passes over 20 frames.
  Packed-eye checker passes with distinct 200x224 and 400x224 eyes. Life-counter
  white ink matches between eyes (6 and 42 checked pixels respectively).
  Artifacts: `tmp/camera-sbs-on`, `tmp/camera-full-sbs`. This is desktop image
  validation, not physical 3D-TV/VR or all-scene stereo validation.

### Software setup overlay (2026-09-27)

- Software camera world rendering omits the setup panel. The common panel
  painter now runs once after reprojection/HUD restoration, or on the normal
  image if reprojection declines. Existing non-camera ordering is preserved.
- PC build and camera unit tests pass. `tmp/camera-menu-software-on` logs 90
  successful camera frames; its Off control confirms all 41,084 menu-ink
  pixels unchanged. Visual inspection confirms stationary text over a banked
  world. Other overlay/transition and stereo coverage remain pending.

### GPU setup overlay (2026-09-27)

- GPU camera composition now applies the setup panel after world reprojection
  and HUD restoration. The initial image and world pass omit that panel so
  its darkening is not applied twice. If camera composition declines, the
  ordinary panel-containing presentation is regenerated instead.
- PC build and camera tests pass. `tmp/camera-menu-on` renders 90 successful
  camera frames with the Global Enhancements panel; the matching Off capture
  is `tmp/camera-menu-off`. Visual inspection confirms fixed text and tilted
  scenery. Software setup, other overlays and transitions remain pending.

### Idle camera work (2026-09-27)

- Identity camera poses no longer schedule the extra styled-world rendering
  and reprojection passes. Event observation remains active; every finite
  nonzero response is retained without an arbitrary visual threshold.
- Unit tests cover identity, tiny pitch, yaw/bank and nonfinite input. PC build
  passes. The 30-frame idle recoil capture `tmp/camera-idle-skip` executes zero
  camera passes; firing in `tmp/camera-idle-shot` executes 28 after the first
  shot on frame 2. This removes redundant rendering, not all camera bookkeeping
  or world-composition preparation. No measured FPS claim or deployment.

### Camera resource lifecycle (2026-09-27)

- Disabling camera response releases its resident compositors/effects, HUD and
  artwork intermediates, presentation texture, optional FSR converted-world
  texture and software temporal histories. Renderer teardown uses the same
  cleanup path, including software history. Normal frames with camera effects
  already Off do not repeatedly execute cleanup.
- PC build and camera unit tests pass. On/off/on captures at frames 15/30 in
  `tmp/camera-cycle-gpu` (FSR) and `tmp/camera-cycle-software` (phosphor/exposure)
  each log one release and 15 successful camera frames before and after it.
  This validates recreation and resumed rendering, not a measured speedup or
  device-memory accounting. Remaining performance and compatibility work is
  unchanged. No deployment.

### DLSS integration (2026-09-27)

- DLSS exposes its borrowed HUD-free reconstructed RGBA8 image only after a
  successful submission; failures and the unsupported-platform stub clear the
  optional output. Camera reprojection is downstream and does not alter SDK
  camera history or input motion. Existing artwork protection is reapplied
  with world-only metadata before rotation, then final HUD is restored.
- PC build passes. Actual SDK captures `tmp/camera-dlss-on` and
  `tmp/camera-dlss-off` use the local native adapter and official runtime.
  Logs confirm evaluation at 464x260 and successful camera composition; visual
  inspection confirms banked scenery with fixed HUD. This does not establish
  completion of DLSS's pre-existing diagnostic world-input limitations.
- Capture script accepts explicit DLSS selection/runtime paths and rejects a
  requested DLSS capture without evaluation evidence. Camera overlay, stereo,
  performance and device validation remain pending; no deployment.

### FSR1 integration (2026-09-27)

- FSR can expose a borrowed full-resolution HUD-free RGBA8 image after its
  float16 EASU/RCAS output. Conversion is GPU-resident and allocated only when
  requested. The camera pass reconstructs full-resolution world metadata using
  the reduced-source mapping, then transforms the upscaled colour image.
- Initial capture `tmp/camera-fsr-on` failed camera composition (world/HUD=0)
  and was rejected. Fixed capture `tmp/camera-fsr-fixed` executes FSR at
  534x299 -> 800x448 and camera composition successfully. Visual inspection
  confirms rotation; all 168 white life-counter pixels match the Off capture.
- PC build and FSR dispatch tests pass; added failed-composition output-pointer
  clearing check. DLSS, overlays, device coverage and performance tuning remain
  unfinished. No current-build deployment.

### TAA integration (2026-09-27)

- Camera rendering now borrows the resolved world texture before HUD restoration
  and applies camera rotation after temporal reconstruction. The rotated image
  is not committed to TAA history. Frames without jitter can use the regular
  world compositor (including pause); unmatched extents are rejected.
- PC build and Vulkan temporal-AA/reference suite pass. The 40-frame gameplay
  capture in `tmp/camera-taa-on` runs TAA and camera composition together;
  `tmp/camera-taa-off` is the matching control. Visual inspection confirms the
  tilted world and stationary HUD. FSR/DLSS are not covered by this change.

### Camera settings integration (2026-09-27)

- Added independent Impact Shake / Weapon Recoil / Camera Banking strengths
  in Global Enhancements, default Off. Config uses `CAMERA_RESPONSE` (0–63);
  old config/state data retains the zero default. State restoration rejects
  out-of-range values. Runtime activation now reads the saved setting, with
  the diagnostic switch only supplying the initial value.
- Config tests and complete simulation tests pass, including channel isolation,
  forward/reverse wrap and state round trips. PC build passes. Menu screenshot
  `tmp/camera-controls/menu.bmp` visually confirms all three rows and scroll
  affordance. `tmp/camera-saved-controls` verifies the settings-driven native
  GPU path with actual recoil events. Not deployed; remaining camera-path
  compatibility and performance requirements are unchanged.

### Software composition boundary (2026-09-27)

Follow-up: diagnostic software presentation now renders the styled world in an
offscreen pass, reprojects it, and restores the tagged HUD and host FPS ink.
World trails/phosphor/exposure use separate histories rather than advancing the
final image's history twice. Native-scale camera activation explicitly enables
layer tags. Later direct world writes are incorporated before presentation.
PC build and camera unit tests pass. Captures at 1x and 2x report successful
software composition on every frame (`tmp/camera-software-native`,
`tmp/camera-software-on`); visual inspection confirms tilted scenery with fixed
HUD. Water/reflections plus exposure/phosphor smoke capture also passes
(`tmp/camera-software-enhanced`). Its CPU reflection log is not GPU water-ray
evidence; the capture script now checks that GPU marker only in GPU mode.
Portrait/setup/wipe integration, settings/menu exposure, performance tuning,
and broader device/weapon validation remain pending. No deployment yet.

- Added explicit tagged world-only cartridge composition, retaining the normal
  clip, offset, render-scale and mosaic rules. HUD pixels and their dither
  provenance are excluded without guessing ownership from colour. Untagged or
  recorded inputs are rejected rather than silently misclassified.
- Diagnostic software gameplay now captures a separate world framebuffer and
  passes it to presentation. Applying styled world reprojection and restoring
  HUD there is still pending; this is not software camera-effect completion.
- Tests pass at 1x/2x/4x with mosaic, clipped negative offsets, same-colour
  ownership and dither metadata. PC build and software/GPU gameplay smoke
  captures pass (`tmp/camera-world-software`, `tmp/camera-world-gpu`).
- Corrected a resident GPU assignment that re-enabled the unwarped separated
  model layer after successful camera composition. No current-build deployment.

- Diagnostic runtime recoil requires a player-laser strategy and an owner link
  to the current player. Object generation distinguishes reused slots;
  simultaneous projectiles count as one volley, and removals do not trigger it.
- Camera response tests pass, including initial roster, duplicate observations,
  volleys, slot reuse, and scene resets. Development PC build succeeds.
- Original 1-1 firing capture (`tmp/camera-recoil-validation/runtime.log`)
  records two shot events with decaying pitch recoil. The matching no-fire
  capture (`tmp/camera-recoil-idle/runtime.log`) stays at zero recoil.
- Still diagnostic-only, not a released menu option. EX/other weapon variants,
  software runtime integration, temporal-AA compatibility, and device validation
  remain pending. `build/current` was not updated for this work.

### Volumetric fog gameplay reference validation

- Follow-up calibration reduces default extinction and uses bounded linear
  sunlight radiance; forward scattering no longer clips default white surfaces.
  The calibrated Original 1-1 capture (`tmp/fog-gameplay-calibrated`) changes
  86,734 world pixels versus its control while all 1,404 comms-portrait pixels
  remain exact. This verifies one software reference scene, not GPU parity.
- Fog ground depth now uses the physical receiver predicate independently of
  reflection settings and excludes tunnel scenes. Explicit host-overlay ink
  masks protect FPS text in addition to cartridge HUD tags; adapter tests cover
  the mask and reject malformed sizes without modifying output.

- Diagnostic software integration now uses the current model shadow scene;
  Original 1-1 capture records 230 triangles and a successful fog pass
  (`tmp/fog-gameplay-reference`). Development PC build succeeds.
- Byte-adapter regression tests pass for zero-density identity, HUD protection,
  alpha preservation, transparent pixels and invalid-input nonmutation.
- The first capture is excessively washed out. Lighting/density calibration,
  ground-depth coverage, compositing order, GPU integration and performance
  remain unverified. This is not a finished user-facing effect, and
  `build/current` remains unchanged.

### Particle shutter GPU shader compilation

- Added a particle-only exposure shader using independent shutter samples,
  per-sample depth testing, linear-light integration and exact HUD bypass.
  Normalized travel limiting avoids overflow for very large finite motion.
- Offline generation now produces SPIR-V, DXIL and Metal source payloads;
  generation and source-digest validation pass. CPU motion-blur reference
  tests still pass.
- This is compilation evidence only, not GPU image parity or runtime support.
  Gameplay activation remains pending. Subsequent wrapper/parity progress is
  recorded below.
  Surface lights and optical distortions require separate stages; the host must
  reject unsupported combinations rather than silently omit them.

- Added the resident `GpuSceneShutter` wrapper: validates extents, finite
  payloads, shutter settings and particle-only types; retains caller ownership
  of inputs, clears failed outputs, and dispatches without CPU downloads.
- Vulkan and D3D12 checks pass for six particle types across unobstructed,
  fully occluded, partially occluded and paused cases (24 combinations/backend).
  Output agrees with CPU reference within one byte; HUD and alpha are exact.
  The existing temporal/AA/depth-light regression checks also pass on both.
- Not yet wired into live gameplay or tested on Metal hardware. This does not
  complete optical-distortion shutter integration or mixed-effect validation.
- Additional Vulkan/D3D12 validation rejects nine malformed/unsupported input
  cases with a cleared output handle, and verifies empty frames return the
  input texture without requiring depth/coverage buffers. Both full GPU
  temporal/AA regression runs pass after these additions.
- Integration audit: the current scene pass precedes bloom/AA and model blur.
  Particle exposure cannot simply replace that pass and then enter model blur:
  that would blur particles twice using underlying model velocity. The live
  pipeline must separate surface lighting from particle coverage and preserve
  post-particle bloom ordering before enabling mixed frames.
- Added an explicit resident particle-exposure input to the effects compositor.
  When supplied, model exposure runs first, particle exposure runs next, and
  bloom/AA follows; the later model-blur point is skipped. Invalid history clears
  particle correspondence. Native guide and surface-light-only prerequisites
  are enforced, and legacy D3D11 rejects the unsupported pass explicitly.
- The compositor builds and existing Vulkan/D3D12 regression suites pass.
  Those existing checks do not yet exercise this new combined ordering; a
  dedicated combined-path comparison and live caller wiring remain required.
- Dedicated combined-path comparison now passes on Vulkan and D3D12: CPU
  model exposure followed by CPU particle exposure, then the ordinary GPU bloom
  pass, agrees with combined resident output within two byte values. Eight
  cases/backend cover history reset/valid history and bloom off/low/medium/high,
  additive particles plus opaque debris, and exact HUD preservation. The
  fixture asserts particles visibly change the image, avoiding a no-op pass.
  Live caller wiring, moving-occluder exposure accuracy and Metal validation
  remain unfinished; these tests do not prove those requirements.
- Added transactional scene-exposure partitioning for live integration. It
  separates surface lights from particles while retaining emitter identities,
  previous projections and stable ordering. Full 48-emitter tests pass, including
  unsupported optical-effect rejection without output mutation. The helper is
  not yet connected to the live diagnostic caller.
- Live diagnostic caller now uses the partition for native particle/light
  frames, builds a particle-free pre-bloom/pre-AA underlay, and activates the
  separate exposure pass only after underlay construction succeeds. Unsupported
  optical/TAA/FSR combinations retain their explicit diagnostic gate.
- Development PC build succeeds. Original 1-1 exhaust capture with medium bloom
  (`tmp/motion-particle-live`, 40 paced frames) records matched particle histories
  and successful resident Vulkan exposure; final image visually inspected.
  This is one mono gameplay smoke test, not complete effect/device validation.
  `build/current` remains unchanged.
- Stereo particle pause/resume capture now passes (`tmp/motion-particle-stereo-pause`):
  51 paused eye pairs and 48 subsequent valid-history pairs, enhanced sky/ground
  and medium bloom, no declined/failed effect frames. Final stereo image inspected.
  The pause verifier now checks particle history agrees across both eyes and
  with model exposure, and forbids particle correspondence on pause/first resume.
  Its positive and seven negative self-test fixtures pass. Physical 3D display
  comfort and moving-occluder exposure accuracy are not established by this test.
- TAA surface resolve now uses the strongest contributing tap when foreground
  depths tie, matching the motion-guide selection rule. Previously a tiny
  first tap could supply a different coplanar normal/owner than the motion
  pass. The regression uses distinct normals on equal-depth neighbours and
  fails against the old shader; after regeneration it passes on D3D12 and
  Windows Vulkan, along with the complete temporal/effects checker on each.
  Development PC build succeeds. This fixes donor consistency, not the full
  TAA/RT integration: ray masks are generated with the unjittered camera and
  their ownership/silhouette alignment still needs verification before lifting
  the combined-effects gate. No deployment to build/current was performed.
- Diagnostic mono TAA now accepts resident ray shadows/reflections with the
  independent ground-only underlay, requiring the aligned surface resolve even
  when AO/DOF and weapon lights are disabled. FSR and stereo ray combinations
  remain gated. Added signed-jitter exposure references for all three shadow
  formats and dielectric/conductor reflections, with particles, HUD protection,
  offsets and marker checks; the full checker passes on D3D12 and Vulkan.
  The PC build succeeds. Live water capture `tmp/taa-rt-exposure-water` passes
  on D3D12; EX 6-6 lava `tmp/taa-rt-exposure-lava` passes on Windows Vulkan/DXR
  interop, including 54 paused and 44 resumed history frames. Final images
  were inspected. Paused captures 33/41/49/57 are byte-identical (SHA256
  4D14D554048CD39821B332B392A6B97F57D84D2C687F4F40D585120F67A0B173).
  These are selected gameplay checks, not exhaustive silhouette/disocclusion
  accuracy or Linux/Metal validation. Secondary rays remain same-frame samples;
  this does not implement their temporal integration across the shutter.
  The feature still requires STARFOX_TEST_MOTION_BLUR_LIVE; build/current is
  unchanged.

FSR diagnostic depth/light integration: FSR composition retains native packed
ownership, surfaces, depth and velocity while replacing colour. Added a real
reduced-resolution FSR fixture that verifies those retained handles, non-no-op
colour reconstruction, exact native HUD and 47 AO/DOF/weapon-light exposure
combinations against the CPU shutter reference. The complete temporal checker
passes on D3D12 and Windows Vulkan. Live diagnostic motion blur now permits
FSR depth effects and surface lights; FSR particles and ray combinations remain
gated. PC build succeeds. `tmp/fsr-depth-exposure` captures Original 1-1 at
2x with FSR Ultra Quality (616x345 -> 800x448), depth modes 15, enhanced sky/ground
and bloom on D3D12. Pause verification passes 45 paused/34 resumed frames;
final image inspected, no unavailable/failed exposure frames. This does not
prove all FSR presets, silhouettes or platforms. `build/current` unchanged.

FSR particle follow-up: six particle types at three receiver depths now compare
against joint CPU model/particle exposure after an actual FSR upscale, including
non-no-op foreground checks and exact native HUD. Both D3D12 and Windows Vulkan
full temporal checkers pass. Enabled FSR particles in the diagnostic caller.
PC rebuild passes; `tmp/fsr-particle-exposure` runs FSR Performance
(400x224 -> 800x448), exhaust/lights, depth modes 15 and medium bloom on Windows
Vulkan. Pause checker passes 49 paused/46 resumed frames; sampled paused BMPs
33/41/49/57 are identical, and final image inspected. Ray combinations remain
gated; this is not all-preset/all-device verification. `build/current` unchanged.

FSR ray follow-up: enabled mono FSR resident shadows/reflections in diagnostic
motion blur, using the independent full-resolution ground-only underlay buffers.
Combined shadow/reflection tests after real FSR reconstruction cover conductor
and dielectric materials, with/without particles, CPU shutter comparison and
HUD protection. Initially the fixture omitted its shadow dimensions and was
corrected; no renderer change was needed for that rejection. Complete temporal
checkers now pass on D3D12 and Windows Vulkan. PC build succeeds. Live water
with FSR Quality (`tmp/fsr-rt-water`, D3D12) passes 47 paused/31 resumed frames;
EX 6-6 lava with FSR Balanced (`tmp/fsr-rt-lava`, Windows Vulkan/DXR) passes
44/33. Both include exhaust, bloom and enhanced sky; independent ground-ray
guards pass, and final images were inspected. Stereo and Linux/Metal device
coverage remain open, as does per-shutter secondary-ray integration. The path
is still diagnostic-only and `build/current` is unchanged.

Stereo underlay preparation: reflection submission now has separate left/right
ground-only producers and retained outputs on DXR, Vulkan and Metal source
paths, including per-eye DXR environment offsets and release/reset handling.
Early rejected submissions clear the selected output instead of retaining a
previous frame. PC rebuild succeeds. The GPU ray checker compares two distinct
camera projections against independent DXR references, verifies separate
buffer handles, preserves the first image after the second submission, and
preserves the second image after first-input rejection. All three geometry
fixtures pass on D3D12 and Windows Vulkan/DXR. Native Linux/Metal source paths
are unverified. Stereo exposure remains gated pending independent eye shadow
underlays and live composition validation; these are prerequisites, not a
completed stereo motion-blur claim. `build/current` unchanged.

Stereo ray exposure integration: added per-eye ground-only shadow producers
for DXR/portable and native Vulkan/Metal source paths. Diagnostic motion blur
selects each eye's independent shadow and reflection output by its presentation
slot, retaining the existing mono selection. CPU mask fallback remains gated.
PC build and stereo layout tests pass. Full-SBS water capture
`tmp/stereo-rt-exposure-water-verified` on D3D12 passes both-eye ray guards,
particle exposure and pause/resume (45 paused/34 resumed pairs). Its hardware
shadow path uses CPU-uploaded casters, not GPU-generated geometry; the helper
now checks this valid stereo path explicitly rather than claiming the latter.
EX 6-6 full-SBS lava `tmp/stereo-rt-exposure-lava` passes on Windows Vulkan/DXR.
Both final images inspected. Physical stereoscopic comfort, fallback-after-eye
failure with rays, and Linux/Metal execution still need validation. These are
same-frame secondary rays, not temporal secondary-ray integration. The feature
remains diagnostic-only; `build/current` unchanged.

Stereo-failure audit found a first-frame shadow-underlay omission: with CPU
caster upload, mono shadows had not been deferred, so the failed stereo pair
skipped rebuilding the mono underlay after submitting mono geometry. The
fallback now rebuilds active shadows in that case too. Initial injected capture
`tmp/stereo-ray-fallback` fails the history guard; after the fix the PC build
and 40-frame `tmp/stereo-ray-fallback-fixed` pass the after-left-eye rejection
and mono identity-history guard on D3D12 with water, particles and bloom.
This is not visual fallback completion: inspection of the caller shows mono
reflections are not regenerated after a stereo failure. That remains open;
no claim of ray-enabled fallback parity yet. `build/current` unchanged.

Mono reflection recovery implemented: the reflection setup retains a safe copy
of its optional backdrop draw and reruns the mono trace after rebuilding mono
geometry on stereo failure. It clears the prior output before attempting the
new trace and restores material/intensity/offset settings on success. The
after-left-eye capture guard now requires a successful mono reflection for
every failed pair. PC rebuild passes. D3D12 water/particles/bloom fixture
`tmp/stereo-ray-fallback-reflected` passes 40 injected failures and identity
history checks. Its final image is byte-identical to the fixed-step ordinary
mono reference `tmp/mono-ray-fallback-reference` (SHA256
B228C92F329C4944F1CAF77684DF03ACED20F93F9DECDB98EA8417265332DF57), and was
visually inspected. This establishes this fixture's parity, not all failure
sites, backends or materials. `build/current` unchanged.

Windows Vulkan fallback follow-up: EX 6-6 lava fixture
`tmp/vulkan-lava-fallback` passes 40 after-left-eye failures, reflection rebuild
and identity-history guards. Its final image differs from ordinary mono
`tmp/vulkan-lava-mono-reference` in 349 pixels within (378,202)-(424,240),
maximum channel difference 56, around exhaust. Control capture
`tmp/vulkan-lava-fallback-no-blur` (same injected failures, no diagnostic blur)
is byte-identical to mono (SHA256
5ABAF38B194D1D80AB98DBBF629E7646A5E5A1F6F1E660F6637A8F388708475C).
This isolates the discrepancy to reset-frame joint particle styling, not mono
reflection regeneration. Keep reset-particle parity open; passing history
guards is insufficient visual proof. No production gate was lifted or build
deployed in this investigation.
