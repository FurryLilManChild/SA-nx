# SA-nx

A native port of **Sonic Adventure** (Dreamcast) to Nintendo Switch homebrew, built with
[dream-recomp](https://github.com/phobos665/dream-recomp) static recompilation: the game's own
SH-4 code is translated ahead of time to C++ and compiled for the Switch, with the Dreamcast's
graphics, sound and drive emulated around it. Graphics are drawn by the Switch's GPU through
Vulkan.

## Status

Version 0.6.0: playable on the PAL v1.003 and US v1.005 discs, in one program that recognises
the disc it is given. Tested through the title, menus, character select, Emerald Coast, Tails's
story and other stages, at full speed at the Switch's stock clocks in play with the default
settings. Not every stage and story has been played through on this version yet; reports are
welcome.

- **Default settings:** translucency per triangle, all clocks at the Switch's own (stock)
  rates. These hold full speed in play.
- **Most accurate picture:** in the port menu set **TRANSLUCENCY** to **ACCURATE** (the
  Dreamcast's own per-pixel sorting) and **CPU CLOCK** to **1224 MHz** or higher. Accurate
  translucency at stock clocks runs below full speed in heavier scenes.
- The port menu also has: in-game resolution (1x, 1.5x, 2x), screen shape (4:3 or stretched),
  volume, frame pacing (smooth or low latency), the FPS counter, and the controls (below).
- The FPS counter shows the frames the game draws and its speed against the Dreamcast. In
  stages the game draws 30 frames a second, so full speed reads **30 FPS 1.00X**; the menus
  draw 60.

### New in 0.6.0

- The US disc (v1.005) is supported alongside PAL.
- **CONTROLS** has new settings for the stick, and new defaults that feel closer to the
  Dreamcast pad:
  - **DEAD ZONE** 0% by default (was 20%). Raise it if your stick drifts. The dead zone is now
    round, so it no longer bends the direction you push near the axes.
  - **STICK SHAPE** SQUARE by default: full diagonals reach full speed on both axes, as the
    Dreamcast's stick reads. ROUND keeps the Switch stick's own circle.
  - **TRIGGER RAMP** off by default: L and R read fully pressed at once.
- Settings saved by 0.5.1 keep your button mapping; the dead zone goes to the new default
  once, because 0.5.1 saved its old default whether or not you changed it.

## What you need

- A Switch running homebrew (Atmosphere), launched through **title takeover** (hold R while
  starting a game), not the Album: the port needs the memory a full application gets.
- Your own dump of one of these discs, as a GDI with all three tracks:
  - **Sonic Adventure PAL v1.003 (1999), MK-5100050**
  - **Sonic Adventure US v1.005 (1999), MK-51000** (the first US release, not Rev A)

  No game data is included in this repository or the release.

The tracks should match these SHA-256 hashes.

PAL v1.003:

| File | SHA-256 |
|---|---|
| track01.bin | `cb45a8de12af5b13abb559fc8acc714744a4e22965fe67158cdbd0037cace093` |
| track02.raw | `57eff24181c0581e969a40ec4972336da0891dcd093f814d7dd577c64e0142a6` |
| track03.bin | `189f89cb695ff81b40878463f326ead759437794d733329ebf3943196ad59b45` |

US v1.005 (the Redump names end in (Track 1).bin, (Track 2).bin, (Track 3).bin):

| File | SHA-256 |
|---|---|
| Track 1 | `c9504edb636dc583edde2eb4fbb9ed2d0401c86792875f1daf8686635d705035` |
| Track 2 | `d1a3ecc0b6f53fec31ca948467247de6b77588fbb9413171a3fc79212158f7ac` |
| Track 3 | `60fc9aa9f0347e0b87ee4ffcfd208f9ebfb02efb251db87866a4c7587ad9775a` |

## Install

1. Download the release zip from the Releases page and extract it to the root of your SD card.
   It creates `switch/sonicadv/`.
2. Copy your disc dump (the `.gdi` file and its three track files) into
   `switch/sonicadv/disc/`. Put only one disc there.
3. Start the homebrew menu through title takeover (hold R while starting any game) and open
   **Sonic Adventure (SA-nx)**.

On the first start the port takes the game's boot program from the disc and saves it next to
the program (`1ST_READ.BIN` for PAL, `1ST_READ_US.BIN` for US). Saves are kept in `switch/sonicadv/save/`.

## Controls

The port menu opens with **-** (Minus). Game controls follow the Dreamcast pad mapped to the
Switch's buttons and can be changed in the port menu under **CONTROLS**, along with the stick's
dead zone and shape and the trigger ramp.

## Reporting problems

Open an issue and attach these files from `switch/sonicadv/` on the SD card, from the run that
went wrong (each start keeps the previous run's logs as `sonicadv.prev.log` and
`sonicadv.prev.err.log`):

- `sonicadv.log` and `sonicadv.err.log`
- any `crash_*.txt`
- `sonicadv_options.txt`

Say what you were doing, and which settings you had (translucency, clocks, resolution), and which disc (PAL or US).

## Build

The Switch build uses devkitPro (devkitA64, libnx) and the Vulkan driver from Mesa's NVK; see
`switch/build.sh` and `CLAUDE.md`. dream-recomp is pinned in the `dream-recomp` submodule and the
port's changes to it are applied from `patches/dream-recomp/`.

## Credits

- [dream-recomp](https://github.com/phobos665/dream-recomp) by phobos665 (GPL-2.0): the
  recompiler, runtime and renderer this port is built on.
- [Sonic Adventure Recompiled](https://github.com/sonicfreak1337/SARecomp) by SoNiCFReaK: the
  first native port of the Dreamcast version, used here as a reference.
- [SDL](https://github.com/libsdl-org/SDL) (zlib), [Mesa](https://www.mesa3d.org/) (MIT),
  [devkitPro](https://devkitpro.org/) and libnx.

Sonic Adventure belongs to SEGA. This is an unofficial fan project, not affiliated with SEGA.

## Licence

GPL-2.0, as dream-recomp.
