# Star Fox Enhanced — Original 3DS hardware-test candidate

This is an experimental native player, not a verified release. It contains
the real pre-game menu, cartridge simulation, SPC audio, native 3D geometry,
slider-controlled stereoscopic top screen and configurable lower-screen HUD.
It does not require a New 3DS. Original 3DS/XL performance and device behavior
have not been established; source scenery/effect transitions still need testing.

For EX menu testing, also try Background choices 21, 25 and 35 (orbital),
19/27/28/31 (unique space) and 2 (stars). The 3D slider should move their
background into depth while the native menu text remains at screen depth.
Other menu/map/Controls routing remains unchanged. Source checks pass; the
physical LCD result still needs testing.

The source follow-up also fixes final-room panorama depth when the cartridge
retains its tunnel flag. Check final-tunnel exits and near-wall stereo at slider
maximum, including camera banks. Host canonical/resource tests do not replace
physical LCD checks. Older R3 packages predate this production follow-up and
must not be relabeled as containing it.

## Install

Use a candidate after R9: the first isolated native emulator boot uncovered a
32 KiB main-stack overflow during cartridge audio loading in R7. Its earlier
host/link/package checks did not establish successful boot. The source follow-up
reserves a bounded native stack, validated in the R8 ARM build. R8 then exposed
a null secondary-texture binding in the native presenter. R9 fixes both and
reaches the native menu/intro, but actual GPU-window inspection exposed vertically
flipped uploaded artwork. R10's colour/A8 upload follow-up passes the actual ARM
build/package gates and isolated native pre-game/title/Controls/Training visual
check, including live lower-LCD radio/meters. A normal campaign recording also
reaches the Original map/travel briefing and initial gameplay geometry, with
distinct upper-screen eyes at maximum emulated slider and an unchanged lower
HUD. R10's travel map repeats artwork in the outer columns; the next source
follow-up restricts its Mode-3 menu panel, and needs a fresh native check.
Full Original/EX stage-flow and physical-device checks remain in progress. Do not treat an
earlier package's host/link checks as proof that its displayed artwork is correct.

1. Use a 3DS/3DS XL with an existing homebrew setup and Homebrew Launcher.
   This package does not modify firmware or install a CIA.
2. Extract the ZIP to the SD card root. The program is
   `/3ds/starfox-enhanced/starfox-enhanced.3dsx`.
3. Copy your own current `Starfox-Assets.BIN` into that same directory:
   `/3ds/starfox-enhanced/Starfox-Assets.BIN`.
   Use the asset builder from the current PC package with your own game data.
   ROMs, BIN data, patches and music are not bundled in this test ZIP.
4. Launch **Star Fox Enhanced - TEST** in Homebrew Launcher. It opens the
   pre-game setup, not a forced direct-to-stage diagnostic.

Audio requires your console's DSP firmware at `/3ds/dspfirm.cdc`, as with
other NDSP homebrew. If initialization reports it missing, provide your own
console's dump through your existing homebrew setup; firmware is not bundled.

If assets are missing or incompatible, the error stays on screen. Correct the
SD file and press A to retry. Y on this error screen starts a direct Corneria
test; it is not the normal boot route. X selects Original/EX for the retry.
Back up existing `starfox-enhanced` settings/save files before testing.

## Controls

- Face buttons and L/R follow the SNES/Nintendo physical arrangement.
- Circle Pad and D-pad steer. Start pauses the game.
- The 3D slider changes stereo strength without advancing the game twice.
  Slider-off and 2DS use one mono eye. Begin testing at modest separation.
- Select + Y opens the native quick menu (resume/options/save/load).
- Select + Start exits. Home/sleep should suspend and resume safely.
- Pre-game Options includes controller remapping and **CUSTOMIZE SCREEN** for
  the lower HUD. Hold the mapped in-game L+R in setup for five seconds to reset
  settings; cartridge saves are retained.

## Check on an Original 3DS/XL

Please report model, build commit from `BUILD-INFO.json`, Original/EX,
stage/scene, render FPS setting and stereo separation/convergence.

- Preview OFF should keep the plain menu responsive; preview ON should show
  RENDERING during preparation. Start Game, experience switching and restart
  must retain the real pre-game options.
- Play Corneria and Training, then an Armada tunnel and Titania water section.
  Check both slider extremes, LCD edges, sprite/model overlap and palette fades.
- In source builds after R4, check the Original/EX colony's open left side and
  EX Gekkou's entry/inner tunnels. The colony must not acquire a left wall;
  Gekkou's inside camera must have tunnel depth. The signed-face follow-up also
  handles outside/on-wall source cameras without clamping their view or dividing
  by zero. Check entry/exit occlusion and disoccluded artwork particularly closely;
  their full visual/device acceptance is still pending. R4/R5 predate the signed-
  exterior follow-up (R5 does include the inside Gekkou/open-colony receivers).
- Check pause/resume, portrait/dialogue and meters on the lower LCD, fades,
  explosions/death, stage results, map, Controls, game over and end/credits.
- Check audio, remapping, HUD editing, save/load, clean exit, SD persistence,
  Home and sleep. Note sustained FPS, slowdowns, crashes or memory errors.
- In builds after R6, repeatedly switch preview, game scenes and the quick menu.
  Texture replacement now frees all obsolete colour/ownership allocations before
  creating new ones; inactive pixel caches are also freed. Host tests prove the
  bounded replacement sequence, not total device RAM or sustained performance.

Some EX-specific/orbital/exterior scenery-depth policies and full-flow resource
limits are still under validation. Advanced desktop effects and MSU-1 are not
advertised in this lean native port. This package is not proof of hardware
performance, completed rendering coverage or a finished 3DS port.
