# SA-nx

A native port of **Sonic Adventure** (Dreamcast) to Nintendo Switch homebrew, built with
[dream-recomp](https://github.com/phobos665/dream-recomp) static recompilation: the game's own
SH-4 code is translated ahead of time to C++ and compiled for the Switch, with the Dreamcast's
graphics, sound and drive emulated around it. Graphics are drawn by the Switch's GPU through
Vulkan.

## Status

Version 0.5.1: playable. Tested through the title, menus, character select, Emerald Coast and
other stages, at full speed at the Switch's stock clocks in play with the default settings. Not
every stage and story has been played through on this version yet; reports are welcome.

- **Default settings:** translucency per triangle, all clocks at the Switch's own (stock)
  rates. These hold full speed in play.
- **Most accurate picture:** in the port menu set **TRANSLUCENCY** to **ACCURATE** (the
  Dreamcast's own per-pixel sorting) and **CPU CLOCK** to **1224 MHz** or higher. Accurate
  translucency at stock clocks runs below full speed in heavier scenes.
- The port menu also has: in-game resolution (1x, 1.5x, 2x), screen shape (4:3 or stretched),
  volume, frame pacing (smooth or low latency), the FPS counter, and controller remapping.
- The FPS counter shows the frames the game draws and its speed against the Dreamcast. In
  stages the game draws 30 frames a second, so full speed reads **30 FPS 1.00X**; the menus
  draw 60.

## Known issues

- FMV movies play at about 0.85x speed.
- The first time a new effect appears, the game can hitch briefly while the Switch builds the
  graphics program for it. It is kept on the SD card, so it happens once.
- The first visit to an area can be slower for a moment while it loads.
- Only the PAL v1.003 disc is supported (see below).

## What you need

- A Switch running homebrew (Atmosphere), launched through **title takeover** (hold R while
  starting a game), not the Album: the port needs the memory a full application gets.
- Your own dump of **Sonic Adventure PAL v1.003 (1999), MK-5100050**, as a GDI with all three
  tracks. No game data is included in this repository or the release.

The tracks should match these SHA-256 hashes:

| File | SHA-256 |
|---|---|
| track01.bin | `cb45a8de12af5b13abb559fc8acc714744a4e22965fe67158cdbd0037cace093` |
| track02.raw | `57eff24181c0581e969a40ec4972336da0891dcd093f814d7dd577c64e0142a6` |
| track03.bin | `189f89cb695ff81b40878463f326ead759437794d733329ebf3943196ad59b45` |

## Install

1. Download the release zip from the Releases page and extract it to the root of your SD card.
   It creates `switch/sonicadv/`.
2. Copy your disc dump (the `.gdi` file and its three track files) into
   `switch/sonicadv/disc/`.
3. Start the homebrew menu through title takeover (hold R while starting any game) and open
   **Sonic Adventure (SA-nx)**.

On the first start the port takes `1ST_READ.BIN` from the disc and saves it next to the
program. Saves are kept in `switch/sonicadv/save/`.

## Controls

The port menu opens with **+ and - together**. Game controls follow the Dreamcast pad mapped to
the Switch's buttons and can be changed in the port menu under **CONTROLS**.

## Reporting problems

Open an issue and attach these files from `switch/sonicadv/` on the SD card, from the run that
went wrong (each start keeps the previous run's logs as `sonicadv.prev.log` and
`sonicadv.prev.err.log`):

- `sonicadv.log` and `sonicadv.err.log`
- any `crash_*.txt`
- `sonicadv_options.txt`

Say what you were doing, and which settings you had (translucency, clocks, resolution).

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
