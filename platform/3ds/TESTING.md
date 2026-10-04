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
  Gekkou's inside camera must have tunnel depth. Its outside-camera entry still
  needs a dedicated exterior policy. R4 does not include these receiver changes.
- Check pause/resume, portrait/dialogue and meters on the lower LCD, fades,
  explosions/death, stage results, map, Controls, game over and end/credits.
- Check audio, remapping, HUD editing, save/load, clean exit, SD persistence,
  Home and sleep. Note sustained FPS, slowdowns, crashes or memory errors.

Some EX-specific/orbital/exterior scenery-depth policies and full-flow resource
limits are still under validation. Advanced desktop effects and MSU-1 are not
advertised in this lean native port. This package is not proof of hardware
performance, completed rendering coverage or a finished 3DS port.
