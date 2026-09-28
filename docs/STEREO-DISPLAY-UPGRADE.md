# Stereo display upgrade

User-requested scope (2026-09-27), from the attached tester feedback:

- Independent separation and convergence controls with wide adjustment ranges,
  persistent configuration, and consistent geometry/reflection/shadow projections.
- Original and enhanced 2D backgrounds at distant/infinite stereo depth rather
  than screen depth, without seams, clipped margins, or moving the HUD.
- First-person crosshair depth adjustment, with a static setting and investigation
  of dynamic targeting depth; preserve aim alignment in both eyes.
- Half/full SBS plus top/bottom, interlaced and anaglyph output.
- Investigate native Leia SR integration; external conversion alone is not native
  support and must not be advertised as such.
- Desktop menu render scales through 6x; explicit pregame.cfg override beyond the
  menu range with allocation/size validation and graceful failure. Preserve iOS
  2x safety limit and platform-specific constraints.

Verification requires CPU/GPU projection and packing tests, config round trips,
original/enhanced background and crosshair captures, retained HUD alignment,
allocation-failure checks, and tester validation on a stereoscopic display.
Do not claim monitor-specific depth comfort from flat screenshots.

Initial audit: stereo output currently supports OFF/HALF SBS/FULL SBS only.
Separation (16 world units) and convergence (1024) are hardcoded across geometry,
fog, particles and reflection paths. Render scale has additional 4x assumptions
in geometry, span layouts and state validation: changing just menu labels is not
sufficient. Existing uncommitted effects work must be preserved.

## Current status

Rechecked the development binaries: stereo layout/projection and runtime input
tests pass. The stereo GPU checker passes on Vulkan and D3D12, including exact
output packing, physical-row parity, retained eye images, geometry disparity
and per-eye motion guides. These checks do not close the remaining original
background, high-scale allocation recovery or physical-display validation items.

Implemented in the development build: adjustable separation/convergence,
static cockpit-reticle depth, BG2/enhanced-sky infinity placement, half/full
SBS and top-bottom, physical-row interlacing with both eye orders, red/cyan
anaglyph, desktop menu scales through 6x and config scales through 10x.
iOS retains its 2x cap. Configuration, projection, packing and selected live
captures are verified; this is not physical stereoscopic-display validation.

Still open: native Leia SR (SDK access needed), remaining original background
paths and transitions, broader reflection/occlusion combinations and high-scale
allocation-failure/device testing. Dynamic reticle targeting is investigated
but not implemented; static depth is available. `build/current` is unchanged.

The entries below are chronological implementation notes, not the current
feature list.

Initial implementation: shared stereo layout and resident GPU texture packing
now support half/full top-bottom (left eye on top), preserving the original
scene aspect and rejecting odd half-height or overflowing full-height extents.
CPU layout/projection tests pass; the GPU stereo check verifies exact red/blue
eye placement for all five packing modes and retains the existing geometry
disparity checks.

Desktop integration: Options -> 3D OUTPUT now cycles OFF, HALF SBS, FULL SBS,
HALF TOP/BOTTOM, FULL TOP/BOTTOM. `STEREO_OUTPUT` values 3 and 4 persist the
new modes (existing 0-2 unchanged). Runtime config tests cover all five modes
and reject out-of-range values. The presentation target tracks both dimensions,
and full top/bottom doubles logical height while half keeps the mono aspect.
Original-background half capture `tmp/stereo-top-bottom-half` is 400x224;
enhanced-background full capture `tmp/stereo-top-bottom-full` is 400x448.
Both were visually inspected, with eight successful stereo frames each.
App build passes; these flat captures do not prove viewing comfort on a 3D TV.
The background still needs infinity-depth placement. build/current is unchanged.

Adjustable rig plumbing: `STEREO_SEPARATION` (1-512 world units, default 16)
and `STEREO_CONVERGENCE` (16-65535 world units, default 1024) now round-trip in
pregame.cfg. Geometry, dust, depth/scene effects, volumetric fog, reflection
cameras and liquid eye offsets use the session rig instead of constants.
Save-state readers accept older archives without the appended rig fields and
preserve the current output-device rig on restore. Runtime input tests cover
12 boundary/default combinations and reject out-of-range values transactionally.
Wide allowed ranges are not recommendations for comfortable viewing on every
display.

Stereo submenu: Options -> Stereoscopic 3D exposes output, separation,
convergence, reset depth and Back. Separation changes by one world unit;
convergence by 64, clamped to the config ranges. Reset restores 16/1024 without
changing output format. The complete Original simulation suite passes, including
submenu entry, format wrap, both depth controls, reset and return selection.
Projection tests cover 16 separation/convergence combinations and confirm zero
disparity at convergence plus near/far sign and infinity limit. Full-SBS captures
`tmp/stereo-rig-normal` (16/1024) and `tmp/stereo-rig-wide` (64/2048) were inspected:
the altered rig changes model disparity while HUD stays fixed. These are smoke
checks, not physical-display calibration. App build passes. The other requested
stereo features, background depth and render-scale expansion remain outstanding.

Original BG2 infinity depth: the GPU BG2 decoder now applies an independent
per-eye source-X offset to distant artwork, leaving authored terrain source rows
and tunnel walls unchanged. The runtime derives the offset from the shared rig
and focal length before composing models/HUD. Enhanced skies explicitly remain
unshifted until panorama and unique-object sampling are wired consistently.
`tmp/stereo-sky-original` uses 64/2048: 14,000 sky pixels match between eyes at
exactly +8 pixels, the expected infinity disparity; zero-offset no longer wins.
Final image inspected. GPU tests cover +/-4 offsets at 1x/2x/4x with multiple
vertical scrolls and unchanged tunnel output. The full background checker passes
432 legacy cases/183,997,440 packed samples, plus its other composition checks;
Vulkan temporal/compositor regressions pass.

This testing also exposed loss of the authored-terrain bit in the compositor's
final output mask. Background, native and late ownership now retain that bit
where visible, while foreground model/HUD replacement clears it. The failing
terrain ownership fixture now passes. Original BG1/CPU-only sky cases, enhanced
backdrops, fractional source sampling and physical-display validation remain
unverified; do not call the complete background-depth requirement finished.

Enhanced sky integration: the shared CPU/GPU backdrop sampler now
accepts a separate stereo source-X coordinate offset in scroll_fraction.z.
Panorama coordinates, unique-object coverage and moon-atlas lookup all use the
shifted coordinate; ground ownership's horizon fallback keeps screen coordinates.
The runtime supplies the same offset as the indexed BG2 layer. Portable shader
payloads have been regenerated. Added resident compositor comparisons for six
projection modes at both signs of a fractional offset, plus unchanged foreground
and HUD checks. The app build and complete resident compositor suite pass.
`tmp/stereo-sky-enhanced` matches 14,000 sky pixels exactly at the expected
+8-pixel disparity for 64/2048. `tmp/stereo-fortuna-enhanced` was also inspected,
including the separate moon; its 3,240-pixel moon region matches exactly at +8.
Original/enhanced captures retain the HUD at screen depth. Remaining validation
includes banked scenes, BG1/CPU-only originals, other authored atlas modes,
reflection combinations and physical-display testing. This does not complete
the full stereo request or the overall effects goal; build/current is unchanged.

Banked-scene audit: added capture-helper hold duration (`-PressFrames`, preserving
the previous three-frame default), then captured real left/right input turns.
Enhanced Corneria's sky matches 9,900 pixels at +8 X / 0 Y while banking;
Fortuna's banked panorama/moon image was inspected as well. Original BG2 exposed
a one-pixel vertical stereo mismatch: source U moved but per-column V still came
from the unshifted eye coordinate. The decoder now samples the shifted column
for sky V too, clamping out ground ink at the unchanged authored horizon.
`tmp/stereo-sky-bank-original-fixed` now matches all 9,900 checked sky pixels
at +8 X / 0 Y. Regression cases cover positive/negative column slopes and eye
offsets at 1x/2x/4x, alongside the passing full GPU background suite. Native and
enhanced turning images were inspected for horizon gaps. These checks cover
the tested BG2 landscape paths, not all stage transitions or physical displays.

Crosshair work started: retail uses a complete four-quadrant tile-$61/$e1 OAM
group, whereas EX can use its 3D TEST_ISTRAT reticle. Added exact retail-group
extraction for a separate eye pass, with signed horizontal translation and
9-bit source-coordinate wrap. Tests check both offset directions, source OAM
immutability, unrelated HUD exclusion, tile/Y/attribute and sprite-size
preservation, and rejection of incomplete groups. The Original simulation suite
passes. This is preparation only: per-eye composition, menu/config depth,
occlusion/aim validation and dynamic-depth investigation remain unfinished.

Render-scale expansion: menu now includes 5x/6x; configuration preserves 7x-10x
instead of silently clamping them to 4x. RENDER_SCALE remains zero-based (5=6x,
9=10x). iOS setter/menu retain the 2x limit. State validation and GPU scene,
projection, clip/span and model guards accept the new range; wireframe thickness
scales consistently. Existing viewport and 256 MiB span/mask bounds remain.
Runtime config round trips all ten values and rejects 11x; Original simulation
tests pass including menu wrap to 6x. App builds successfully. Short full-SBS
GPU captures at 6x and 10x present 4800x1344 and 8000x2240 respectively without
fallback in the tested intro scene (`tmp/stereo-scale-6x`, `tmp/stereo-scale-10x`).
The 6x image was inspected. This is not broad high-resolution validation:
extreme aspect ratios, allocation-failure recovery, expensive effects, other
devices and sustained gameplay remain to test. build/current is unchanged.

Interlaced/anaglyph implementation: output modes 5/6 are alternating physical
rows with selectable eye order; mode 7 selects left red plus right green/blue.
Both eyes are fully composed before packing so alpha model overlays and bloom
remain intact. Interlacing is packed at final renderer output dimensions and
presented without a subsequent scale; letterbox row parity is absolute, not
relative to the content's top edge. Two batched geometry calls avoid one GPU
pass per scanline. Menu/config supports all eight output values. Native texture
blit packing intentionally rejects these overlay modes (they use the separate
renderer compositor). Software and GPU exact-pixel checks pass for channels,
both row orders, odd dimensions, letterbox offsets and texture-state restoration.
Existing five-format GPU packing and scene disparity checks pass; runtime config,
layout and Original simulation tests pass. Gameplay captures were inspected:
`tmp/stereo-anaglyph` (400x224), `tmp/stereo-interlaced` (1200x672 physical output
despite a 2x internal render). Physical stereoscopic-display testing remains.

LeiaSR research (2026-09-27): the vendor's developer forum says SDK access
requires requesting an agreement; linked current Windows requirements and SDK
overview redirect to visitor authentication. No SDK agreement was accepted and
no proprietary library was added. User/tester SDK availability was requested.
Source: https://forums.leialoft.com/t/leiasr-sdk-for-windows-no-longer-available/6166
Native LeiaSR remains unimplemented; existing SBS conversion is not a substitute
for claiming native calibrated weaving/head-tracking support. Crosshair depth,
remaining sky paths, resource-failure recovery and broad device validation also
remain unfinished. No release or build/current update was performed.

Reticle continuation: complete retail groups now carry an OBJ-command metadata
bit (ignored by CPU/GPU sampling). Per-eye translation changes only their X
bounds and texture origin, preserving batch priority, flips, Y, and unrelated
HUD commands. Translation validates the entire batch before mutation, supports
stored-pixel sub-logical offsets at higher render scales, and rejects invalid
scale/non-finite/overflow inputs. Simulation tests verify exactly four marked
commands plus an unmarked unrelated sprite; projection tests verify positive,
negative and fractional translations and transactional failure. Both suites
pass, including the Original cartridge simulation suite. App build passes.

The per-eye deferred-layer path is wired through a diagnostic-only
STARFOX_TEST_STEREO_RETICLE_DEPTH (capture helper -StereoReticleDepth). It copies
only batches containing reticle commands; original mono buffers are untouched.
This is NOT a completed user-facing crosshair feature: initial asteroid/Armada
captures stayed third-person or reached player death, so they did not prove
reticle disparity. Actual cockpit capture, fallback/aim/occlusion checks, menu
and config controls, other sprite paths and dynamic targeting remain outstanding.

Retail cockpit validation and control: the LEVEL1_2 fixture is already inside
the cockpit without scripted input; Select was switching it out. Added camera
mode/gate words to scripted-input diagnostics to establish that state. At
64 separation / 2048 convergence / 4096 reticle depth, capture
`tmp/stereo-reticle-untouched` moves the 160-pixel reticle from X centroid 199.5
to 197.5 left / 201.5 right, exactly the predicted +/-2 logical pixels. The
`tmp/stereo-reticle-control` comparison has identical screen-depth reticles.
Across 7,676 checked lives/shield/bombs HUD-region pixels, changes are zero.

Added persistent RETICLE DEPTH submenu control (0=Screen, otherwise 16–65535),
128-unit menu increments, reset-to-Screen, settings validation and backward
save-state loading. Restoring a state retains the current viewing-device depth;
legacy Reset/Back submenu rows migrate past the inserted row. Tests cover
boundary/default config round trips, invalid-input nonmutation, and submenu
enable/increment/reset. App and runtime config tests pass. Dynamic targeting,
fallback/occlusion behavior and other cartridge sprite paths still need work;
this does not finish the whole stereo request. build/current remains unchanged.

Fallback and scaled-reticle verification: added a failure injection after the
left eye has actually rendered/retained its image, plus a capture-helper guard
requiring that injection to be reached. `tmp/reticle-fallback-after-left` reaches
it 120 times; its final BMP is byte-identical to `tmp/reticle-fallback-mono`
(SHA256 40EE91831AC3B8EE549789C4ABDFD8AB0BADD8507147988AFEE245E97A4B2EBE).
The pre-eye failure fixture is identical too. This verifies the tested mono
fallback, not every internal compositor failure or reflection combination.

`tools/check_stereo_reticle.py` compares complete reticle pixel masks, not just
centroids, and rejects an unchanged Screen-depth image as a negative control.
The 1x fixture passes 160 reticle pixels per eye at -2/+2 displacement;
`tmp/reticle-2x-control` versus `tmp/reticle-2x-depth` passes 640 at -4/+4.
Protected HUD checks pass 6,948 pixels at 1x and 27,792 at 2x. An initial 2x
failure was asteroid pixels in the far-right world margin, outside bombs/boost
ink; that margin was removed from the HUD test rectangle. No runtime HUD
behavior was changed to satisfy that test. App build passes.

Near-depth follow-up: `tmp/reticle-near-depth` uses depth 512 with the same
64/2048 rig. The complete 160-pixel reticle mask moves +12 left-eye / -12
right-eye, as predicted for a point in front of convergence; all 6,948 protected
HUD pixels remain exact. The verifier accepts this via `--left-offset 12`.
Unit regressions now also exercise positive/negative int32 overflow rejection
without partial batch mutation, plus stored-pixel translation at all ten render
scales while leaving unrelated OBJ commands unchanged. Stereo tests pass.

Dynamic-depth investigation: the existing camera-space shadow Scene exposes
nearest-hit queries, but is not populated on every non-RT rendering path.
A dynamic aiming plane cannot simply query whichever optional shadow scene
happens to exist: it needs a consistently built targeting scene or resident
depth sampling, cockpit/player exclusion, aim-ray alignment and stable hit/no-hit
transitions. Static depth is implemented; dynamic target tracking is not.
