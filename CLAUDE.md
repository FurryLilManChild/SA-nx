# SA-nx: notes for Claude

Native port of **Sonic Adventure** (Dreamcast, PAL v1.003, MK-5100050) built with
[dream-recomp](https://github.com/phobos665/dream-recomp) static recompilation. PC first, then
Nintendo Switch homebrew (libnx). Owner: Mark (GitHub FurryLilManChild). Load the
`switch-port-base` skill before working here; the PC-then-Switch loop follows `x360-switch-port`.

## Who does what

- Claude writes all code, scripts and configs, reads logs, and commits.
- Mark runs the PowerShell scripts on his Windows PC, boot tests and play tests, and moves Switch
  builds to the SD card himself. He writes no code.
- Claude reaches `C:\SA-nx` through the device bridge (a Linux shell with the folder mounted at
  `$HOME/mnt/SA-nx`). That shell cannot run Windows programs, cannot delete files there, and stops
  anything still running when each call returns (about 3 minutes per call).

## Hard rules

- Never use em dashes anywhere (chat, docs, commits). No decorative emoji.
- Never commit game data: `assets/`, `games/*/extracted/`, saves, captures. Run `git status`
  before every commit.
- Commit as `FurryLilManChild <346934328+FurryLilManChild@users.noreply.github.com>`.
- dream-recomp stays pinned in the `dream-recomp` submodule. Our changes to it go in
  `patches/dream-recomp/*.patch`, applied idempotently by the build scripts, until they are offered
  upstream. Follow its own rules for anything inside it (its `.claude/skills/dream-recomp`): never
  paste Sega SDK text, every emitter change gets a differential test.

## Upstreams and licences

| Upstream | Use | Licence |
|---|---|---|
| phobos665/dream-recomp @ 51a148c | recompiler, runtime, renderer, disc tools | GPL-2.0 |
| sonicfreak1337/SARecomp | reference only (widescreen, camera, scene fixes); its KatanaRecomp SDK is not public | no licence posted; credit and link it |
| libsdl-org/SDL 3.4.16 (VC package) | window, input, audio on PC | zlib |

This repository is GPL-2.0 (dream-recomp's licence).

## Layout

```
dream-recomp/          submodule, pinned
games/sonicadv/        sonicadv.toml (config, committed), CMakeLists.txt, extracted/ (ignored)
assets/                Mark's GDI and tracks (ignored)
tools/extract_resumable.py   disc extraction in passes (the bridge's time limit)
tools/win/             setup_pc, build_pc, boot_test, play (logs to out\logs, UTF-8)
docs/progress-log.md   newest first, one entry per session with evidence
```

## The loop (PC phase)

1. Claude changes config or code, commits, pushes.
2. Mark: `git pull`, `tools\win\build_pc.ps1`, then `tools\win\boot_test.ps1` or `play.ps1`.
   Always give him the commands in exactly this form (his PowerShell blocks bare `.ps1` scripts):
   ```
   git pull
   powershell -ExecutionPolicy Bypass -File tools\win\build_pc.ps1
   powershell -ExecutionPolicy Bypass -File tools\win\play.ps1
   ```
3. Claude reads `out\logs\*` through the bridge (boot.log, boot-report.txt, suggested.toml),
   merges suggested seeds into `sonicadv.toml` with evidence in the progress log, repeats.

Switch phase starts once the PC build plays: libnx, NVK Vulkan (switch-port-base), same config.

## Disc facts

- Tracks match SARecomp's published SHA-256s. `1ST_READ.BIN` is 6,735,296 bytes, stored plain,
  SHA-1 `7ca46c7d78f9bd869fdf5d8b6fa93da516dffded`.
- Areas, stages, bosses, the Chao garden and minigames are PRS-compressed code modules loaded to
  0x8C900000 (37 found in `SONICAD/`). `tools/prs_extract.py` writes them decompressed to
  `games/sonicadv/extracted/modules/`. Each is configured as an overlay `[[relocations]]` with
  `file =`, sized to its code region only (data follows the code). ADV00 = Station Square.
- `2_DP.BIN` is not loaded in normal play (probably Dream Passport); not configured.
