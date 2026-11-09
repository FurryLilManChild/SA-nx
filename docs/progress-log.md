# Progress log

Newest first. One entry per session: what broke, the cause, the fix, with evidence.

## 2026-11-09: Test 2.3 (seeds, translucent polygon sources, native movie IDCT)

- Seeds: the US Tails run (sonicadv_report.txt, 36,423 untranslated calls) reached 35 entries in
  the main program discovery had missed, most called through 0x8C.. pointers (0x0C07C17A x9466,
  0x0C0208E0 and 0x0C02090E x4364, 0x0C04F6A0 x3363, 0x0C07CAD0 x139, 0x0C049DA0 x131, ...): each
  is right after an rts or padding, or a return point after a jsr. Mapped back to PAL with
  map_variant's Image (US->PAL, each one checked by mapping forward again), added to
  sonicadv.toml, and the US config regenerated (it keeps the 2,501 PAL-found seeds). The hot module
  targets in the same run (0x0C91201A x9472, 0x0C904A40 x4364) have the same counts as main
  program misses and look like return points of calls into them; they could not be tied to one
  module from the files alone. The report now prints, for each untranslated target outside the
  main program, the overlay file whose bytes were there when it was first reached (on the PC) or
  the bytes themselves (on the Switch, where the files are on the disc).
- Translucent polygon sources (--ta-tags, on in test builds): for every translucent polygon or
  sprite header the TA receives, the game code that wrote it (PR and the first four return
  addresses on the guest stack), whether it came straight from the CPU or by channel 2 DMA from a
  RAM buffer written with store-queue bursts. Top sources to the log every 10 guest seconds and to
  the report; the right stick hides the next source (blend zero/one), its number after the FPS
  counter. Unit test: the TA hook fires for translucent polygons and sprites only, and RAM bursts
  reach on_sq_ram.
- Movie IDCT (MOVIE.BIN 0x0CEB1504, 33% of the game thread in movies): native version in
  games/sonicadv/native/movie_idct.cpp, the same arithmetic in the same order (FTRV in double as
  ops.h, two lanes on GCC), guest memory through one host pointer. Host differential against the
  generated function: 0 of 300 random calls differ (registers, FPU registers, FPUL, T, cycles, all
  16 MB of RAM), 2.1-2.6x faster on x86. Installed with a new runtime hook,
  register_native_override (the table entry keeps its signature and resume function). In the game
  the first 20,000 calls and one in 64 after are run both ways with the stores journalled and
  compared; a difference switches it off and restores the translation's result (checked by
  injecting a fault: caught on the first call, results equal to the translation).
- dream-translate now leaves an unchanged unit's file alone (the custom command has restat), so a
  config change recompiles only the units it changed.

## 2026-11-09: release 0.6.0 (what Test 2.2 played, PAL and US)

- Mark asked for a release of the Test 2.2 build for the US disc and the stick settings, with
  dead zone 0 and square stick as defaults. Versioned 0.6.0 (new features, not only fixes).
- Defaults: `Bindings::deadzone_percent` 0 (was 20), `stick_square` true. A version 1 controls
  file (0.5.x) always wrote `deadzone = 20` as its old default, so its dead zone is ignored like
  its trigger_ramp; version 2 files (Test 2.1 and 2.2) keep theirs. Tests updated and added; the
  render tests pass (84 cases, 1638 assertions). Patch 0022 recut.
- Built from Test 2.2's sources with the US config as tested (90e0971, before the 2,501 seeds of
  d15a309, so nothing retranslated): 11 of 11 steps, SANX_RELEASE=ON, SANX_VERSION 0.6.0. NRO
  514,031,647 bytes, sha256 6f43316ba35960cf95903e9fe3282b8a71205170bf17c643109535bb06358664.
- Zip (NRO, both tomls with SD paths, README.txt, COPYING.txt, disc/PUT_YOUR_DISC_HERE.txt for
  either disc) 153,884,038 bytes, sha256
  07863502436de7867829a8758abe7f6fa56a572348f4c41e1792aa6dd972d29f, in `out\release\` with
  release_notes_0.6.0.md. US track SHA-256s added to the README (hashed on Mark's PC).
- Next: Test 2.3 (report seeds and the mapper's seeds, the CHARACTERS translucency diagnostic,
  a native fn_0ceb1504).

## 2026-11-09: test2.2 built: PAL and US in one NRO

- US disc: Mark's GDI is US v1.005 (MK-51000, 1999-10-05); the (Rev A) CHD beside it is a later
  revision, not supported yet (the launcher refuses it with a message). Extracted on his PC; the
  main program is the PAL code shifted (mostly +192 bytes), every module and cutscene file the same
  code by its own small shifts. tools/map_variant.py carried the config over: 321 addresses, none
  unmapped, 4 weak (6 to 26 bytes equal), 219 from module/cutscene header tables. The US character
  select word in zeroed data (0x8C760DB0) is a guess by the program's end shift (+192): check the
  "screen words" log line on character select.
- dream-recomp (patch 0022): variant units ([game] variant = true) in dream::gen::<id>,
  register_variant_functions / select_variant; boot reads IP.BIN (product, version, boot file)
  and picks between --config and --alt-config; [port] boot_sr, idle_skip, play_modules,
  charsel_words; an unsupported version returns 3 and the Switch shows why. dream_add_game
  VARIANTS. Test: p4 as version "vtest" is live only after select_variant (emitter 17 cases).
- Build: 1,111 steps, two restarts at the 2-hour limit. Linking then failed three ways: the ELF
  passed 4 GB with debug information and devkitPro's linker under Wine wrote it with an empty
  header (also when compressed to 512 MB); fixed by linking generated units from copies without
  debug information (switch/link_rsp.sh, devkitPro objcopy; llvm-objcopy's copies broke
  relocations). Then three US objects (sonicadv_us, _part29, _part30) were broken by the time-limit
  kills (undefined .LANCHOR0): deleted and rebuilt. NRO 514,043,935 bytes, sha256
  65827c8e625574788687be0ab4b950fff6b255c27934b5026705ae017eb81f4e, verified on Mark's PC with
  sonicadv.toml and sonicadv_us.toml; ELF kept as test2.2.elf.

## 2026-11-08: test2.2: radial stick dead zone, optional square stick

- A player claimed the Dreamcast stick's range is square. Not confirmed by any source found. Either
  way a square/round difference only scales both axes alike, so it cannot steer Sonic; Mark's
  own Dreamcast showed the Station Square curve too.
- What the check did find: the port's dead zone was per axis (each half axis rescaled from 20%).
  That bent small steering towards the axes (20 degrees off forward reached the game as about 11)
  and left a full diagonal at about 0.63 per axis (about 90% strength).
- Patch 0022: `stick_axis()` in input.cpp, used by the Switch and PC windows: one radial dead zone
  on the stick's distance from centre, rescaled from its edge, direction kept exactly. New
  `stick_square` binding (CONTROLS > STICK SHAPE, ROUND by default) stretches the circle to the
  square along the same direction. PC triggers keep their own dead zone. Tests: angle kept at 20
  and 30 degrees, full strength on diagonals, square corners, the menu row, save and load (render
  tests 83 cases pass). PC window code syntax-checked against SDL3 headers.
- Build note: recutting the patch re-applies every patch, which touched every file in the
  submodule and started a 584-step rebuild. Stopped; untouched files set back to their old time so
  only the changed input files rebuild (57 steps).
- US disc support requested by Mark (one NRO for both discs); waiting on his US dump.

## 2026-11-03: phase 2 begins; test2.1: cutscene code translated, movie measurement

- Mark: tests are now numbered by phase (this is test 2.1, then 2.2, ...). Goal of phase 2: 1.0.
- Found while scoping 1.0: the story cutscenes' own code was never translated. 236 EV*.PRS
  files on the disc (copied from Mark's PC over the bridge); 207 carry code (word 0 points at
  the entry at 0x8CB80000, word 1 into the file), 29 (EV_*) are PVMH texture archives. Every
  cutscene therefore ran in the dev interpreter.
- Measured before configuring them: with the default overlay signatures (32 instruction words),
  184 function signatures in one file also match a different function in another file at the
  same address, and 14,961 functions are identical to another file's while that file's other
  code differs (shared helpers at fixed offsets). Either way the wrong translation could run.
- dream-recomp (patch 0022):
  - relocation `full_signature`: the signature covers the function's whole extent;
  - relocation `dispatch_calls`: calls and tail jumps to functions of the same unit go through
    `call_indirect`, so every callee checks its own signature;
  - relocation `parts = N` (translator and DreamAddGame.cmake): events are one unit each;
  - `dream-translate emit --overlay --full-signature --dispatch-calls`, and an optional flags field
    in tests/sh4/programs/programs.cmake.
  - Differential tests: p4 at 0x8C060000 through the dispatcher gives the same results as direct
    calls; with the callee's code changed in RAM the call goes to the untranslated path (a
    direct call would have run the stale callee); c1's fib (678 bytes) with a whole-extent
    signature stops matching after a change 0x2A4 bytes in. Emitter 16 cases, translator 11,
    runtime 87 pass.
- Config: the 207 files as overlays at 0x8CB80000 with no_fold, full_signature, dispatch_calls,
  parts = 1. Translated in the cloud: 13,596 functions, 953,831 instructions, 3,550 not lowered,
  about 179 MB of C++. tools/prs_extract.py gains `--events` (PC builds need it once).
- Movie measurement: test2.1 is a test build (SANX_RELEASE off): the profiler with its movie
  sections (pc_movie, stacks_movie) and the sound/movie line in the timing log are back.
- Also in: triggers full at once (below).
- Build: 585 steps (the translation changed, so every unit recompiled; one restart after the
  2-hour command limit). The link then failed with no message: 530 objects made a 42,979-character
  command, past Windows' 32,767 under Wine. switch/link_rsp.sh (the linker launcher) puts the
  objects in a response file. NRO 266,227,743 bytes (180 MB before), sha256
  1353e9a604bb02029d7b509ea537110fb2ca1f820a7eba500059454abb343a3b, verified in
  `out\\sd\\switch\\sonicadv\\` with the new sonicadv.toml; ELF kept as test2.1.elf.

## 2026-11-03: for the next release: triggers full at once (no ramp)

- Mark: a pressed trigger should be fully pressed right away. The Switch's ZL and ZR are digital,
  and the bindings defaulted to ramping them to full over 0.15 s (dream-recomp's choice for
  keyboards driving a car).
- Patch 0022: `Bindings::trigger_ramp` defaults to off. The bindings file is now version 2; a
  version 1 file's `trigger_ramp = 1` is ignored, because version 1 wrote the old default whenever
  any control was rebound, so it was never a player's choice. CONTROLS > TRIGGER RAMP can still
  turn it on. Test: defaults, version 1 and 2 files (render tests 79 cases pass).
- Also reported, not yet changed: the stick "pulling left" in Station Square happens on Mark's
  real Dreamcast too (the game's camera-relative movement), so it is left as it is.
- Not built yet: goes into the next Switch build.

## 2026-11-03: release 0.5.1: the game could run uncapped

- Report from a player (and Mark, once, last night): speed between 0.75x and 1.37x, 1.70x on
  the title, the counter at 60 to 90. No logs available (Mark away from home).
- Cause found in the pacing code: the launcher slept only when the guest was ahead of real time
  by less than one second. Once the guest clock got a second or more ahead, it never slept
  again and the game ran as fast as the Switch could, for the rest of the session. How it first
  got that far ahead is not known without a log (a stretch with no VBlank-out, during which
  nothing paces the guest, is one way).
- Fix: when the guest is ahead by more than two refreshes (33.4 ms), the baseline is moved back so
  it is exactly two refreshes ahead, and that is slept. The log prints `pacing: the guest was N ms
  ahead` for the first 20 times, so the next log shows when and by how much.
- FPS counter: counts frames the game draws (`rendered`), not refreshes: 30 at full speed in
  stages, 60 in the menus (Mark asked for a counter that does not read 60 as 1x).
- Version 0.5.1 (release build). Zip sha256 cd84f30d2464b6686de320ff6886a626203f41919fea1bc5acea38000e79f01b,
  NRO sha256 5ac569631df47541786021cdc582f0a6e6c8c8414cbf310ed6908e4551c3c0dd, in `out\\release\\` with
  release_notes_0.5.1.md. ELF kept as release-0.5.1.elf. Not run on a Switch before release:
  built and packaged only.

## 2026-11-02: release 0.5.0 built

- test28 result: nothing went backwards. Unwinding 3.2% to 2.0% of the game thread below full
  speed (6.5% to 2.6% over the session; the FDE search itself almost gone); SD reads 4.3% to
  3.2% below full speed (11.3% to 4.3% over the session; what is left is first reads of blocks
  during loads, which jump around the disc); boot program load 668 samples to 59. Stock, per
  triangle: Emerald Coast 0.87 to 1.00x, a second stage at 1.00x with time to spare.
- Release changes:
  - `SANX_RELEASE` (CMake option, switch_main.cpp): no `--report` (the minute-long write after
    quitting), no profiler thread, and no CPU_MHZ=1785 written on the first run.
  - `SANX_VERSION` 0.5.0 (the homebrew menu showed 0.1.0).
  - Port menu defaults: translucency per triangle (Mark: defaults per triangle, everything
    stock). The lean (8 layers) choice is gone; an options file naming it gets accurate.
  - The launcher takes 1ST_READ.BIN from the disc's ISO 9660 root when it is missing (checked
    against Mark's dump in Python first: SHA-1 7ca46c7d...), and opens the only GDI or CHD in the
    configured folder when the configured name is not there, so players copy their dump as is.
  - README rewritten for players (status, defaults and how to get the accurate picture, known
    issues, track hashes, install, reporting, credits).
- Package in `out\\release\\`: SA-nx-0.5.0-switch.zip (switch/sonicadv/: sonicadv.nro,
  sonicadv.toml with SD paths, README.txt, COPYING.txt, disc/PUT_YOUR_DISC_HERE.txt), sha256
  5fadfae75ace692e58a6feadb52e711112141652348f692e35b6f9b26ddb0ed9; NRO sha256
  ffc94a45ee09885cc7db1887a86c8e45fabf84bfb4b5150ee759edc8c50f74dd; release_notes.md. ELF kept as
  release-0.5.0.elf. The build-switch cache now has SANX_RELEASE=ON; set it OFF for test builds.
- Clean-install test 1 (Mark): "Missing game files" before anything else. `switch_main.cpp`
  still required 1ST_READ.BIN before starting the launcher, so the extraction never ran. Now it
  requires sonicadv.toml and a .gdi or .chd in disc/. Repackaged: zip sha256
  4e133cfe8cb67a74f26eaaa9f0f439c3b709f6acd050de1e666f2e4d206da9ef, NRO sha256
  5b740790b9b552c798dddd72cf229714038fde5cde90bec4d77684fd58158d6e.
- Clean-install test 2 (Mark): boots, extracts 1ST_READ.BIN, fast to launch; quitting still
  slow. Leaving out `--report` only stopped the file being written: `report_text`,
  `suggest_relocations` and `suggest_entries` still ran on every exit. New `--quick-exit` skips
  them; the release passes it. Repackaged: zip sha256
  525ad6161b30dc3ff989813d601b155a975fbd70858e938f7392523aea4f9823, NRO sha256
  332e7c8c7a22fba657b9375ba3399f473ecb03865cc915637394ef235aafe8b4.

## 2026-11-02: switch-test27 result and switch-test28: disc cache, unwinding cache

- test27: character select recognised (`screen words` 5 and 0 on arrival, translucency switched
  to the menu's choice there, back to per triangle when it left; screen number 4 is the mode
  select, flag 16 there). Mark: at 1224 MHz the game runs at full speed; at stock with per
  triangle it also seems to run at full speed (measured windows 0.93 to 1.00x).
- Mark's release plan: after test28, if nothing went backwards, release. Stock with per triangle
  or per strip, overclocking for accurate translucency.
- switch-test28:
  - `GdiDisc`: 256-sector blocks in a cache (at most 64, about 38 MB, least recently used out),
    with a worker thread reading the next two blocks ahead; its own FILE per track. Reading on
    the game thread was 4.3% of its busy time below full speed and 11.3% over the session
    (test26 profile). Test: the GDI test now reads a 20,000-sector track (79 blocks) in
    sequential, backwards, block-edge and 3,000 random reads, byte for byte; clean under
    ThreadSanitizer.
  - The boot binary is read in one call instead of through `istreambuf_iterator` (seconds of the
    Switch's start).
  - `__wrap__Unwind_Find_FDE` (switch_main.cpp, linked with `--wrap`): a per-thread cache of
    libgcc's FDE lookups. The exceptions come from the game's longjmp (guest 0x0C110820, its
    setjmp at 0x0C110894) through `nonlocal_return`; the lookup was 56% of the unwinding time,
    which was 3.2% of the game thread below full speed (6.5% over the session).
  - The SAVE RAM SNAPSHOT row is hidden again.
- Build: 6 steps. NRO sha256 e2b4e9cd683463fc3259eb59cccf458487a22b634bb7f9654fadced5202a6926,
  verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test28.elf.

## 2026-11-02: switch-test27: character select from the game's own state

- test26's seven RAM snapshots (Mark: title, VMU select, file select, character select with
  Sonic and with Knuckles, Emerald Coast twice): words equal on both character-select dumps
  and different on all five others. Most are display tables (fog, colour ramps) or heap. Two
  read like state: 0x8C760CF0 (and its twin 0x8C760D34) read 1, 2, 3, 5, 5, 1, 1, a per-screen
  number in a table of 0x44-byte entries; 0x8C4C7714, in 1ST_READ's data, read 16 on every
  screen but character select (0, with 0x8C4C7718 going from -63 to 0).
- switch-test27: outside play, character select is `0x8C760CF0 == 5 && 0x8C4C7714 == 0`, read on
  the game thread; there the port menu's translucency applies, as in play. The log prints
  `screen words:` whenever either changes, so the next run shows their values on every screen
  (the mode-select, options and other menus were not snapshotted).
- Build: 3 steps. NRO sha256 90d66775dbebc749d9113597fac50bdc00bf5242ddc39ea1a2743fd849a03a25,
  verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test27.elf. The SAVE RAM SNAPSHOT row stays
  for now in case the words do not hold.

## 2026-11-02: switch-test25 result and switch-test26: character select is not what test25 thought

- test25, stock: Emerald Coast 0.97x average over 13 windows (0.90 to 1.00x), game thread 89%
  busy with 7% asleep; game-thread work per guest second 0.93 on average, 1.06 in the heaviest
  window. No running past 1x reported. Mark: "so close".
- The character-select detector was wrong: the title sequence draws about 1,300 translucent
  polygons a render (it triggered per-pixel there: 6.4 ms of GPU per render, the game waiting
  41%), character select about 170 (it never triggered), the main menus 20 to 100. My test24
  reading of 1,483 was the title, not character select.
- switch-test26: per pixel in play only (title, menus and character select per triangle), and a
  temporary port-menu row SAVE RAM SNAPSHOT that writes main RAM to `ramdump-N.bin` next to the
  options file, with a log line giving the module word at 0x8C900000. Mark saves snapshots on
  the title, the main menu, character select (two characters highlighted) and in play; diffing
  them should find the game's record of the current screen. The dumps are game data: they stay
  on the SD card and Mark's PC, never in the repository.
- Build: 5 steps. NRO sha256 4431641451988bcfdaebb5700cc7eecec82e7da429a9f4bc857495a413fc0d6a,
  verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test26.elf.

## 2026-11-02: switch-test24 result and switch-test25: smooth pacing works; no more catching up

- test24, stock, Emerald Coast with FRAME PACING smooth (12 windows): 0.87x to 1.20x, about 1.02x
  on average, the game thread 73 to 97% busy, render wait gone (0%), about 150 of 300 refreshes
  per window kept the last frame (the game draws 30 a second). Then low latency in the same
  session (9 windows): about 0.86x with the 12 to 17% wait back. Mark: a noticeable improvement
  in play; the title screen only a little quicker (there the render thread is 67 to 81% busy, so
  it is the render thread, not the wait, that limits it at stock).
- Problem: speeds above 1x (1.18x, 1.20x, 1.31x), with the sound running fast. The launcher only
  ever slept while ahead of real time and never wrote off time it fell behind; the wait in every
  present used to hold the guest to the display, so the debt was never paid back by sprinting.
  With smooth pacing nothing held it, and after each slow stretch the game ran fast until it had
  caught up.
- Problem: the on-screen FPS counted presented frames only, so it read about 30 while every
  other refresh kept the last frame.
- switch-test25: lateness beyond one 60 Hz refresh is written off (the pacing baseline moves
  forward), in both pacing modes; the on-screen FPS counts refreshes that kept the last frame.
- Also in switch-test25 (Mark's request): per-pixel translucency only in play and on character
  select; the title and the other menus draw per triangle. `Live::choose_translucency` sets the
  mode on the target just before each render (on the render thread), replacing
  `apply_options`'s writes, which could now race a render in flight. Character select runs in the
  same module (ADVERTISE) as the other menus, so it is recognised by its translucent polygon
  count: per pixel from 400 a render (about 1,500 measured there, the main menus under 150),
  back to per triangle after 60 renders in a row under 200. The log prints `translucency here:`
  at each change. A heuristic: Mark checks character select still looks right.
- Build: 3 steps. NRO sha256 875c2a47db4785fa7e6fc5bead63d7a1b4b520e0006cbbac9d8367bf8889c81a,
  verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test25.elf.

## 2026-11-02: switch-test24: frame pacing, "smooth" by default

- `Live::present` (each VBlank-out): with `PortOptions::pacing` 0 ("smooth", the default), when
  the render thread still has a job in flight the refresh is skipped: the last finished frame
  stays on screen and the game runs on instead of waiting in `render_wait()`. Never two
  refreshes in a row, so a render thread that is always busy still has its frames shown. Window
  events are still polled. The perf log gains a `pacing:` line (refreshes that kept the last
  frame).
- Pacing 1 ("low latency") keeps the old behaviour. Port menu row FRAME PACING, saved as
  `PACING=smooth|latency` in `sonicadv_options.txt`.
- Why: switch-test23 measured the wait at 13% of the game thread in Emerald Coast at stock and
  over half at the title screen. Cost: when a refresh is skipped the picture is one refresh
  (16.7 ms) behind until the next; when the render thread keeps up nothing changes.
- Tests: the port menu test covers the new row and its text form; host suites pass (dcdisc's
  pytest needs pycdlib, missing in this container, unrelated).
- Build: 5 of 5 steps. NRO sha256
  6ff1ced082546262d258977097741f53aed99bd4475c47dc8316209c6d9767a7, verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test24.elf.

## 2026-11-02: switch-test23 result: TA overhead gone, small gain; the game now waits at vblank

- Stock only, Emerald Coast (13 windows, a 7-window pause excluded): 0.88x average (test22
  0.84x, test21 0.86x); game-thread time per guest second 0.885 (test22 0.936), about 5% less.
  Nothing broken (Mark).
- Profile: the TA path fell from 33% of the game thread's busy samples to about 12%; the
  scheduler from 6.6% to 1.4%. What did not move: in gameplay the game thread spends about 18%
  of its time in `Live::present`, about 13% of it in `render_wait()` waiting for the render
  thread to finish the job in flight (finishing the previous GPU frame, decoding, texture work,
  submission) so the window can show the newest image. One offscreen target per scale, so
  present cannot show anything while the render thread draws into it. At the title screen the
  wait is over half of the game thread.
- Arithmetic from the measured windows: game work 77.8% at 0.885 per guest second gives
  0.879x; with the 13% wait given back to the game, about (77.8 + 13) / 0.885 = 1.03x.

## 2026-11-02: switch-test23: the graphics submission path

- Store-queue writes from emitted code go straight into the 64 bytes of the queues
  (`FastMem::sq`, `gst` in ops.h) when nothing watches stores; before, each word took
  `gst_slow` -> `Memory::write32` -> the virtual `do_write32`.
- `sq_flush` into area 4 (TA FIFO, TA texture path, YUV converter) no longer brings the scheduler
  up to date first (`DcMemory::quiet_device_access_`); a display list's bursts do not depend on
  the clock. List-end interrupts still pull the next poll in (`Intc::on_raise`).
- `Scheduler::advance_to` returns at once while the target is before every armed deadline
  (`floor_`, a lower bound kept by `request` and refreshed by each full scan).
- The display list is decoded (`DisplayList::feed_stream`, `add_background`) on the render
  thread; the game thread keeps the stream and a copy of the first 0x200 bytes of PVR registers.
- Tests: `store queue: inline writes from emitted code, flushed to main RAM` (32- and 64-bit
  writes, the journaling fallback) and `scheduler: the fast path never skips an event armed
  after it`; runtime 84 cases, emitter 13 cases pass.
- ops.h changed, so every game unit rebuilds.
- Build: 343 steps (one restart after the 2-hour command limit). NRO sha256
  39b5e2e4c1b2359733b2ec704e289349b10c96fcab2bb346ba241a5d67d0434d (181,915,679 bytes, 10% larger: the
  store-queue check is inline at every 32- and 64-bit store), verified in `out\\sd\\switch\\sonicadv\\`;
  ELF kept as test23.elf.
- Expectation: the path was 33.4% of the game thread's busy time at stock; some of it remains
  (the stream copy, `Ta::feed`, the TA's own work). A projection, not a promise: stock towards the
  mid 0.9s.

## 2026-11-02: switch-test22 result: no measurable gain; the TA path is a third of the game thread

- Stock only (Mark, a few minutes), Emerald Coast, 14 windows: 0.84x average (test21 0.86x),
  game-thread time per guest second 0.936 (test21 0.925): no gain, within the noise. No crash
  or breakage. The reloads after each store were not where the stock time goes.
- Slow-only profile, grouped by stack: 33.4% of the game thread's busy samples are in the path
  that feeds the PowerVR: store-queue writes (`gst_slow<uint32>` 6.9% self, `do_write32` 3.2%:
  the game builds vertices in the SH-4's store queues at 0xE0000000, which are not main RAM and
  so take the slow path through a virtual call), `sq_flush` and the scheduler catch-up under it
  (`resolve` -> `on_device_access` -> `advance_to`), `Ta::feed`, and `DisplayList::feed` /
  `handle_vertex` (the display list is built on the game thread). Translated game code itself is
  45.7%.

## 2026-11-01: switch-test22: relaxed memory access (the clock, GBR and the RAM base in locals)

- Why: translated code reaches guest RAM through `Memory`'s inline path, a byte pointer. At -O1
  (most units) and with -fno-strict-aliasing (the -O2 units) every guest store may alias anything,
  so after each store the compiler re-read `Memory`'s fast-path state (RAM base, mask, journaling
  and watch flags) and the context fields the code uses (`cycles`, `next_event`, `gbr`), and had
  to write `cycles` back before each store. These are the "strict" accesses of item 2.
- Change (dream-recomp, patches 0022 and 0023), under the existing `local_regs` option:
  - Emitted functions keep `cycles`, `next_event`, `gbr` and the fast-path state (`FastMem`:
    RAM base, mask, stores allowed) in locals, written back and re-read at the same points as the
    general registers (calls, polls, `write_sr`, `trapa`, returns, tail calls).
  - Guest accesses go through `gld`/`gst`/`gld_pair`/`gst_pair`/`gsq_flush` (ops.h): main RAM
    inline from the locals; anything else writes the clock back, calls `Memory`, and re-reads the
    clock and the poll deadline (devices read `ctx.cycles`; Holly's `on_raise` moves
    `next_event`).
- Item 3 (spin loops): nothing writes main RAM behind the game thread's back between calls and
  polls (DMA, Maple and GD-ROM run on this thread inside slow-path calls or interrupt polls), so
  a value kept in a register across a polling loop is re-read at the loop's next poll or call.
  No loop can spin forever on a stale value; the one hot wait loop is already the idle skip.
- Tests: the golden replay against Flycast's recorded states now runs every golden twice, with
  main RAM on the slow path and on the inline path (`BareMemory::use_fast_path`); emitter 13
  cases (71,481 assertions), runtime 82 cases pass.
- Not in this build: T and the FP registers stay in the context.
- Build: 345 steps (one restart after the cloud disk filled, one after the 2-hour command limit). NRO sha256
  01540b18aadb1f981cc003c59dac691ccc55c78fa19e82639556b2b467238049 (165,318,687 bytes, 0.3% smaller than test21),
  verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test22.elf.

## 2026-11-01: switch-test21 result: idle skips 25x fewer, small gain; 1785 dips are render-side

- Idle skips per 5-second window fell from about 100,000 to about 4,000 (the line events are
  gone), with no visible breakage reported.
- Stock, Emerald Coast (17 windows): 0.86x average (test20 0.85x), game thread 79.5% busy,
  game-thread time per guest second 0.925 (test20 0.94): about 2%, inside the window-to-window
  noise.
- 1785: two windows mid-stage at 0.92x and 0.94x that Mark noticed as new slowdowns. In both the
  game thread was only 57 to 62% busy; what rose is the game thread waiting on the render thread
  (26%, 20% against about 9%) and the render thread's busy time (30%, 25% against 10 to 16%),
  while GPU time per render stayed normal (4.7 ms). test20's 1785 run had the same pattern in
  two windows at the start of the stage. Cause not identified from these logs: the perf line
  does not break the render thread's time down.
- Slow-only profile: scheduler `advance_to` still 5.4% self. 90% of it now comes from
  `DcMemory::resolve` -> `System::install` lambda (`on_device_access`): every MMIO access,
  including each store-queue flush to the TA FIFO (tens of thousands a second), syncs the
  scheduler, and `advance_to` scans every event even when nothing is due.

## 2026-11-01: switch-test21: line events only where something happens

- Correction: the Switch profiler already splits samples by speed (`stacks_slow`, below 0.95x);
  test20's slow-only samples put scheduler `advance_to` at 8.1% and the idle-wait function at
  4.3% of the game thread's busy time, so the scanline overhead is real at stock.
- Change (dream-recomp, patch 0022):
  - `Spg` arms its event only for eventful lines (0, VBlank in and out, vsync start and end, the
    HBlank interrupt line, every line in HBlank mode 2) instead of every line. SPG_STATUS works the
    line out from the SH-4's own cycle count (`Spg::clock`, pointed at `ctx.cycles`) and the
    vsync bit from the line. Register writes fold the passed lines in and re-arm.
  - `holly::Intc::on_raise`: System sets `ctx.next_event = ctx.cycles`, so an interrupt raised
    inside an MMIO access (TA end of list, channel-2 DMA) is still polled at the next check;
    before, the per-line event bounded that wait to one line.
  - The launcher's watchdog (frame and time limits) runs once a frame instead of once a line.
- Tests: `spg: the scanline in SPG_STATUS follows the clock between line events` (2,000 random
  steps against floor(t / line) and the vsync range), `spg: HBlank interrupt on one line and on
  every line`; the interrupt-delivery test now expects the next poll at VBlank-out (line 21).
  Runtime 82 cases, emitter 13 cases pass.
- Expectation: most of that 12% at stock, maybe 0.85x to about 0.9x. Risk: code that reads
  the scanline (flicker, tearing, stutter), and interrupt timing.
- Build: 13 of 13 steps. NRO sha256
  ebc6b0e5a7bcaf8f73ca2c9d6bd43b6505732b272ad194c9558d5bd9e94f55bd, verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test21.elf.

## 2026-11-01: switch-test20 result: lock wait gone, small gain

- Stock 1020/307/1331, Emerald Coast (17 windows): 0.85x average (test19 0.82x), game thread
  80% busy (83%); game-thread time per guest second 0.94 against 1.01, about 7% less. A second
  short stock stretch later averaged 0.71x (4 windows, heavier part of the stage).
- 1785: 1.00x, game thread 62% busy (test19 66%).
- Profile: `svcArbitrateLock` from the AICA event is gone (18.8% of busy samples to 0.4%). The
  gain is far smaller than that share because most of those waits happened during idle skips
  while the game was ahead of real time (the 1785 parts of the session), where they cost
  nothing; the profile does not separate the clock settings. Mark noticed no difference by feel,
  which fits 0.82x to 0.85x.
- Remaining busy time worth naming: scheduler `advance_to` 8.5%, SD card reads during play
  8.4% (GD-ROM sector reads and whole-file loads), idle-wait function 4.4%, `nonlocal_return`
  exceptions 3.6%; the memory layer about 9%; the rest is translated game code.

## 2026-11-01: switch-test20: the SH-4 stops waiting on the sound thread's lock

- Change (dream-recomp, patch 0022): `Aica::update_sh4_interrupts()` publishes the SH-4 line
  (MCIEB & MCIPD) in an atomic every time it computes it, which is under `lock_` both on the
  sound thread (every sample) and on SH-4 register writes. The `aica.sample` batch event reads
  that atomic and raises or clears Holly's AICA line without taking `lock_`. Interrupt latency
  is unchanged (at most one batch, 32 samples).
- Test: `aica: threaded, the SH-4 line follows the published state` (runtime tests, 80 cases
  pass); the AICA cases also run clean under ThreadSanitizer.
- Expectation: removes most of the 18.8% of game-thread busy time test19 spent in
  `svcArbitrateLock`; stock Emerald Coast from about 0.82x towards the mid 0.9s. Estimate only.
- Build: 5 of 5 steps (runtime only). NRO sha256
  6b68d6f6d53649d0565a5296f0124ffc4084e29e52a1444916eb07d9b0cfb70f, verified in `out\\sd\\switch\\sonicadv\\`; ELF kept as test20.elf.

## 2026-11-01: switch-test19 result: registers in locals, about 1.2x on the game thread

- Stock 1020/307/1331, Emerald Coast (23 five-second windows): 0.82x average (0.61 to 1.00),
  game thread 83% busy, 10% of it waiting on the render thread. Game-thread time per guest
  second about 1.0, against 1.2 to 1.3 on test18: roughly 1.2x to 1.25x faster. Still about
  1.2x short of full speed at stock.
- 1785/768/1600, Emerald Coast: 1.00x throughout, game thread about 66% busy (test18 about 71%).
- No crashes, no unmapped accesses, err log only the NVK conformance line. The 0 fps and
  negative percentages near the start are the port menu being open across a stats window
  (cosmetic accounting bug in the perf line).
- Profile (whole session, stock and 1785 mixed): 18.8% of the game thread's busy samples are
  `svcArbitrateLock` on the AICA `lock_`, taken by the `aica.sample` batch event every 32 samples
  to call `update_sh4_interrupts()` while the sound thread holds it running ARM7 and mixer
  chunks. 85% of those come from the idle-skip wait function (0x0C641E40), so the waits sit on
  the critical path: even at 0.82x about half of guest time is skipped idle (about 19,000 skips a
  second, one per SPG scanline event, about 20 us of guest time each). SD reads 5.7%,
  `nonlocal_return` exceptions 2.8%.
- Next (proposed): publish the AICA interrupt line in an atomic from the thread (and from SH-4
  register writes, under the lock), read it lock-free in the batch event. After that: SPG
  scheduling only the line events that matter, so idle skips jump further.
- Pipeline key list holds only 6 variants, so the Mystic Ruins first-visit slowness is probably
  not pipeline compiles; unexplained until a log of such a run arrives.

## 2026-11-01: switch-test19: guest registers in host locals (the "register work")

- Release 0.5.0 is on hold at Mark's request so the register work goes first.
- Cause being targeted: the emitter wrote every guest register as `c.r[n]`. Guest stores go
  through `Memory`, which the compiler must assume can alias the context, so after each store every
  live register was read back from memory. test18 showed -O2 does not help, which fits: the
  reloads are required by the code's shape, not by the optimisation level.
- Change (dream-recomp, in patch 0022): `EmitOptions::local_regs`, on by default,
  `--no-local-regs` to turn it off. A function's general registers are host locals `gpr0`..`gpr15`,
  loaded after the entry poll, written back to the context before calls, delivering interrupt
  polls, `write_sr`, `trapa`, `rte`, tail calls and returns, and read back after the ones that
  return. Design in dream-recomp `docs/emitter-design.md`, "Register model".
- Differential test: every existing emitter test now runs with it on (13 cases, 35,817 assertions:
  the golden replay against Flycast's recorded states, the GCC-compiled C unit, longjmp,
  RTE-as-jump, tail calls), plus a new program `p14_bank` that flips SR.RB twice inside one
  function and checks r0 comes from the right bank each time.
- Build fix: with a host dream-translate (`DREAM_HOST_TRANSLATE`), the game's translation now
  depends on that file, so an emitter change regenerates the game.
- `tools/recut_patches.sh` writes 0022 with `--binary` (the test program's .bin).
- Not done yet: T, PR and the FP registers stay in the context; spin-loop marking beyond the one
  idle skip. Expectation: a real but unmeasured gain on the game thread; the Switch run decides.
- Build: 328 of 328 steps, no warnings or errors. NRO sha256
  3b2ec07b43949b9231311e5f9f1573060e8895b0a6a268476473aeb2c448b0a3 (165,761,055 bytes), placed in
  `out\sd\switch\sonicadv\` and verified there; ELF kept as test19.elf.

## 2026-11-01: switch-test18 result: where stock clocks stand

- One launch: Emerald Coast at 1785/614/1600 (GPU 614 this time), then stock (1020/307/1331),
  then per-strip translucency at stock, then the clocks stepped back up (1224, 1581, GPU 460, 614,
  768, memory 1600, CPU 1785).
- 1785/768/1600, Emerald Coast: 1.00x in most windows, game thread 59 to 77% (about 71% on
  average); switch-test14's comparable windows averaged about 68%. The -O2 units made no
  difference that shows above the scene-to-scene noise. Opening movie window: 0.83x (test17:
  about 0.84x average).
- Stock 1020/307/1331, Emerald Coast: 0.58x to 0.60x with the game thread 70 to 78% busy and
  waiting on the render thread 9 to 16% (GPU at 307 MHz), dipping to 0.21x. Game-thread time per
  guest second: about 1.2 to 1.3 at stock against about 0.7 at 1785, the clock ratio (1.75).
  1224 MHz: 0.48x in a heavier stretch. Per-strip translucency at stock: no visible gain (Mark),
  as expected with the CPU the limit.
- So stock needs the game thread's work per frame cut to about 55% of today's (about 1.8x faster)
  plus the GPU waits at 307 MHz. Compiler optimisation of the hot units does not move it; the cost
  is the shape of the translated code (guest registers in the context struct, reloaded after
  stores; cycle and interrupt checks; the memory layer about 12 to 14%; indirect calls).

## 2026-10-31: switch-test18 (towards stock clocks: hot units at -O2; play-module list)

- Mark wants a real attempt at stock clocks before release (the build is release-ready at 1785).
- Gameplay profile (switch-test15 prev and test17 runs, pc_fast + pc_slow, 35,077 samples):
  translated code 33.5% of samples, flat (top function fn_0c90320c in module 34 5.3%,
  fn_0c037990 3.0%, fn_0c051790 2.0%). By unit: module 34 (Emerald Coast's) 23%, part1 23%,
  part29 17.8%, part2 10.1%, part11 5.7%, part30 3.9%, part27 3.1%, part31 2.6%: 84% in eight
  units. The movie player is eight units (reloc_8ceb0000 and _part1 to _part7); switch-test17
  optimised only the first, which may be why its gain was small.
- -O2 -fno-strict-aliasing now for main parts 1, 2, 11, 27, 29, 30, 31 (about 70 MB of generated
  C++), all of module 34 and all of the movie player; 24 compile steps.
- --play-modules: pick_target counts as play only the first words of the 31 gameplay modules (13
  distinct values, from extracted/modules), passed by switch_main.

## 2026-10-31: switch-test17 result

- Movie (opening): 0.74x to 0.90x, about 0.84x on average (switch-test15: 0.68x to 0.84x, about
  0.77x); game thread 89 to 95%. Mark: marginally better, not full speed, low priority now.
- Resolution switching (test16 and 17 logs) works: ADVERTISE (8C9000E8) gives 640x480, stages the
  chosen size, menu changes apply at once. One fault: right after boot, before any module is
  loaded, 0x8C900000 holds 0x4154414B ("KATA", leftover text), which pick_target took for a
  gameplay module, so the first screens (logos, opening) drew at the in-game size. Fix: match the
  first word against the gameplay modules' own first words instead.

## 2026-10-31: switch-test17 (the movie player at full optimisation, direct YUV writes)

- games/sonicadv/CMakeLists.txt: gen/sonicadv_reloc_8ceb0000.cpp (MOVIE.BIN, Sofdec, 1.2 MB of
  translated code) compiles with -O2 -fno-strict-aliasing (MSVC /O2); every other unit keeps
  -O1. Only that unit and the runtime rebuild (ninja -n: 5 steps). switch-test15's movie profile:
  translated code 64%, fn_0ceb1504 alone 32.5%.
- pvr/core.cpp: yuv_word copies the whole word; yuv_macroblock writes its 128 texel words straight
  into video memory (64-bit view, vram + (off & 0x7FFFFC)) with a page note per row and per new
  page, unless the memory is journaling or tracing stores (then write32 as before). New test
  "pvr: YUV converter, direct video memory writes match the write32 path": four macroblocks, the
  video memory bytes and the dirty pages identical both ways.
- No change to resolution (switch-test16 not yet reported).

## 2026-10-31: switch-test16 (resolution in play only, live)

- Mark: test16 is only the resolution change; the movie work is test17.
- PortOptions::scale is now percent (100, 150, 200; SCALE=1, 1.5, 2; test15's 3 reads as 2), row
  "RESOLUTION IN GAME". The launcher keeps the 1x target (base_) and makes 1.5x (960x720) and 2x
  (1280x960) targets the first time play needs them; pick_target, before each render with the
  render thread idle, picks one: the port menu's size when the module at 0x8C900000 starts with
  code (gameplay), 1x when it starts with a table of addresses into itself (ADVERTISE: title,
  menus, character select; SUMMARY) or is empty. On a switch the old target is finished and the
  new one rebinds the per-pixel set and pipelines (Offscreen::rebind_oit). The log prints each
  module change ("module: gameplay ..." with the first word) and each resolution change. --scale
  still fixes one size for the run (the PC). The options are no longer read before the window.
- rcap RCAP_SWITCH: one renderer through 1x, 2x, 1x, 1.5x, 2x, 1x on captures 013 and 002: every
  1x frame byte-identical to the plain 1x render, every 2x frame to the plain 2x render; the 1.5x
  frames look right.

## 2026-10-31: switch-test15 result; movies measured

- Mark: sound, controls and everything else as wanted; resolution: 2x good in game but menus and
  title slow; wants 1.5x and 2x, applied live, only in game (never title, menus, character
  select).
- 2x (prev log): menus 0.43x to 0.77x, GPU 4 to 21 ms per render (2,009 translucent polygons over
  the whole screen at 1280x960).
- Movies (1x, the opening movie): 0.68x to 0.84x, game thread 97%, GPU under 2 ms, 99 to 120 movie
  frames per 5 s. Movie profile (32,418 samples): translated code 64% (fn_0ceb1504 in MOVIE.BIN
  alone 32.5%, the Sofdec player's module at 0x8CEB0000, one 1.2 MB generated file), memory
  layer 14%, YUV converter (yuv_word, yuv_macroblock) about 5%, DcMemory::store 3.6%.
- Menu detection: the module at 0x8C900000 starts with a pointer table for ADVERTISE and SUMMARY
  (e800908c..., 4000908c...) and with code for every gameplay module (stages, fields, bosses,
  Chao garden, minigames).

## 2026-10-31: switch-test15 (smooth sound through dips, resolution, movie marking)

- Sound: dream/audio/stretch.h, a WSOLA time-stretcher (hop 512, Hann windows of 1024, search
  plus or minus 256 frames on a mono mix, coarse then fine). Both sinks (SDL2 on the Switch, SDL3
  on the PC) now feed it: every 512 input frames the ratio follows the device queue (none at 4096
  queued or more, up to 2.5x as it empties, eased 30% per step), so a slow stretch is heard as
  the same sound played longer at the same pitch rather than gaps. audio/tests/test_stretch.cpp
  (built by hand): ratio 1 passes the input through exactly after the first hop; 1.18, 1.5 and 2
  give 1.17x, 1.48x, 1.97x the length at 440 Hz (440.0 to 440.5) with no gap over one frame; a
  ratio changing every block gives no step larger than the tone's own (753 against 752).
  Sink::stretch = false restores the old queue. Report line gains frames out and frames added.
- Resolution: PortOptions::scale (SCALE=1..3), port menu row "RESOLUTION (ON RESTART)"; the
  launcher reads it from the options file before the renderer's targets are made, unless
  --scale is given. rcap gains RCAP_SCALE: captures 013 and 002 at 2x draw correctly (downsized,
  1,903 and 7,420 pixels differ by more than 32 levels, all edges). test_port_menu covers the row.
- Movies: the timing log gains a sound/movie line (ms of sound added by stretching, YUV converter
  words and frames per window); the profiler gains pc_movie and stacks_movie (dream_port_movie:
  YUV words fed in the last half second).
- Controls: already in the port menu (binding screen); Mark to try it on the Switch.

## 2026-10-31: switch-test14 result: menus and character select fixed

- Replayer on Mark's Switch with the fix: character select 3.0 ms GPU per render the game's way
  (resolve 1.3), against 1,693 with the old box scissor (still in as "boxscissor"). Pictures equal
  to the game's way in every resolve experiment.
- Game (accurate, 1785/768/1600): menus and character select at 1.00x; Emerald Coast mostly 1.00x
  (game thread 59 to 80%, render thread 8 to 17%, no resolve over 30 ms all run). Remaining dips:
  0.65x to 0.98x stretches with the game thread at 85 to 97% (game code bound; the slow-side
  profile has the model-drawing code fn_0c037990/0c037294/0c037350 at about 14%, the memory layer
  12%, the scheduler 4.5%, and 15% svcWaitProcessWideKeyAtomic, the game thread waiting on a
  condition variable); some of them are probably the movie or loading, which the log cannot tell
  apart yet.
- Mark's list: (1) for release, music must not crackle during dips; (2) the movies are slow (lower
  priority); (3) control mapping and resolution scaling in the port menu.
- Sound: the sink is a queue; when the game falls behind the device runs dry and plays silence
  between blocks (the crackle). Movies: SofDec decoded by the game's own code, frames through the
  YUV converter (already per macroblock in pvr/core.cpp). Controls: the port menu's Controls row
  already opens the binding screen (Switch buttons mapped in window_switch.cpp). Scale: --scale
  exists at launch only.

## 2026-10-31: found it: the resolve's scissor; switch-test14

- Capture replayer, second run (character select, captures 013 and 015; GPU ms per render):
  the game's way 1,693 (resolve 1,691); resolve over empty lists 1,691; lean 448; split pass
  1,693; no mid timestamps 1,693; gather only 1.7; resolve over the whole screen 3.0 (resolve
  1.3); split and whole screen 3.0; per strip 0.6; per triangle 0.65. Emerald Coast: every
  experiment about 1.95. So the slow path is the resolve's full-screen triangle scissored to the
  translucent box (switch-test8 added the box): on NVK it costs 450 to 1,700 ms even with no
  fragment to resolve, and nothing without the scissor. Emerald Coast was fast because its box is
  the whole screen. Character select was first reported at 1.5 fps in switch-test8. The NVK
  internals behind it are not known (guard band or scissor handling of a huge triangle on GM20B
  is a guess).
- switch-test14: the resolve covers the whole target, and the opaque copy is the whole image, so
  pixels outside the box (no fragments) write back the opaque colour. rcap: all six captures
  byte-identical, accurate and lean. The gather keeps the box scissor (1.2 ms on the Switch).
  Offscreen::Experiment::box_scissor brings the old way back for the replayer.

## 2026-10-31: capture replayer, first run

- Character select (captures 013 and 015) reproduces on the Switch from the captured frame alone:
  the first render, the game's way, did not finish within finish()'s one-second limit, and the
  replayer then skipped that capture's other experiments (its mistake). So the slowdown is in
  drawing that content on the Switch, not in the live game's state. Emerald Coast (capture 002):
  every experiment about 1.9 ms of GPU time, 6.3 ms wall.
- Replayer fixed: Offscreen::finish_timeout_ns (30 s in the replayer, 1 s in the game), cheapest
  experiments first, three samples of a slow one.

## 2026-10-31: the capture replayer (sa_replay.nro)

- The probe showed the resolve shader fast on NVK alone, so the cause is in how the game's pass
  wraps it. switch/replay draws captures 013, 015 (character select) and 002 (Emerald Coast)
  through the game's own Offscreen and Renderer on the Switch, 8 renders per experiment:
  the game's way, lean, the resolve in a second instance of the pass after a barrier (split),
  the resolve over the whole screen, both, no timestamp between gather and resolve, gather only,
  resolve only (empty lists), per strip, per triangle. Offscreen::Experiment holds the switches
  (all off in the game). Writes sdmc:/switch/sonicadv/replay.txt. On lavapipe: all run; split
  gives the game's picture exactly (a drop-in fix if it is fast); full-screen scissor changes the
  picture outside the box (the opaque copy only covers the box), so it is a diagnostic only.

## 2026-10-31: Stage 1 results (probe, switch-test13)

- Probe (CPU 1785, GPU 768, memory 1600): NVK resolves character select's lists in 1.19 ms of GPU
  time with test12's shader (2.03 ms wall); test11 0.89 ms, test10 1.02 ms. deko3d: 1.57 ms wall
  for test12, 6.79 ms for test10 (uam handles test10's arrays worse). Emerald Coast's lists: NVK
  0.51 ms, deko3d 0.70 ms. Pictures match between the APIs on every real scenario. So the resolve
  shader on its own is fast on NVK; the 386 to 657 ms resolves in the game come from something
  around it in the game's pass, not from the shader or the driver's compiler as such, and deko3d
  would not be faster at this. Odd: on 4 even layers NVK's test12 (kMax 32) picture differs from
  every other result (lean, test11, test10, deko3d all agree); not seen on the real scenarios.
- switch-test13 (Mark: Emerald Coast "WAS faster", full speed on accurate apart from occasional
  dips): mostly 0.96x to 1.00x; render thread 11 to 16% busy, the game thread waits for it 7 to 11%
  normally. The wait-loop skip jumps about 50% of guest time in Emerald Coast (the game runs at 30
  renders a second and waits out the other frame). Dips (0.74x, 0.83x, 0.88x) line up with the
  game thread waiting for the render thread 16 to 27%, and with slow resolves (3.4 ms average in
  one window, one over 30 ms): the same problem as character select, smaller. Character select is
  still about 0.4 fps.

## 2026-10-31: Stage 1: the resolve probe, switch-test13 (render thread, wait-loop skip)

- Plan agreed with Mark: Stage 1 measures NVK against deko3d before any port (the probe), and
  test13 moves our drawing off the game thread. Stage 2 (a deko3d renderer for the Switch) only if
  the probe backs it; Stage 3 the game-code speedups.
- switch/probe (sa_probe.nro): runs the resolve as the game does (full-screen triangle, the
  per-pixel buffers, texelFetch of the opaque colour, push constants) through NVK and, from the
  same GLSL compiled by uam, through deko3d. Variants: test12's resolve (32 and 8 kept), test11's
  and test10's exactly as shipped (rebuilt from git), the list walked without sorting, and only
  the first fragment. Scenarios: character select and Emerald Coast lists dumped by rcap from
  captures 013 and 002 (RCAP_DUMP_OIT, new Offscreen::read_oit), and 1, 2 and 4 even layers over
  83% of the screen. Sets CPU 1785, GPU 768, memory 1600 for the run, restores them, writes
  sdmc:/switch/sonicadv/probe.txt (deko3d first, flushed per line). On lavapipe the Vulkan side
  runs and test10, 11 and 12 give the same checksum on every scenario.
- switch-test13:
  - Render thread (boot_main render_post/render_loop/render_wait, core 0): the game thread
    decodes the display list, copies the palette and stride registers and the list of rewritten
    video memory, and hands over; texture decoding, building and submitting the GPU work happen on
    the render thread. present() and anything else that touches the renderer waits for it first.
    Textures are still read from live video memory. Log: render thread busy and the game
    thread's wait for it. --no-render-thread or DREAM_RENDER_THREAD=0 turns it off.
  - Wait-loop skip (--idle-skip, sh4/idle_skip.h, not in abi.h so no game-code rebuild): the
    game's loop at 0x8C641F98 and 0x8C641FD8 calls the empty function at 0x8C64162C (rts; nop,
    checked at start) until an interrupt changes its flag; called from there, the clock jumps to
    the next event. switch-test12's profile: 10.8% (fn_0c641e40) and 4.8% (fn_0c64162c) of the
    game thread at full speed, about 8% in slow stretches. Log line `idle:` with the guest time
    skipped. `--idle-skip none` in sonicadv_args.txt turns it off.

## 2026-10-31: switch-test12 result

- Accurate with the array-free resolve: memory card select (22 translucent polygons, 1.2 layers per
  pixel, longest list 5 nodes) 386 ms per resolve; other menus 130 to 145 ms (longest list 9 to 12).
  Mark: character select the worst yet. Emerald Coast: resolve 0.5 to 6 ms, longest lists 8 to 28,
  speed 0.74x to 1.00x (mostly 0.83x to 0.85x with the renderer 16% of the game thread).
- So the lists are short and complete, and the resolve keeps no array, yet a few reads per pixel
  take hundreds of milliseconds; lavapipe runs the same frames in milliseconds. Most likely NVK's
  shader compiler (NAK) on Maxwell; a bug of ours that only NVK trips on is not ruled out. Flycast
  runs the same kind of per-pixel sort on the Switch through OpenGL (nouveau's older compiler).
- Profile split: the slow side mixes in the GPU-bound menus (svcWaitSynchronization 50%), so the
  ground cost is not cleanly separated. Rising in slow stretches: fn_0c037990 (FTRV and store
  queue writes: model drawing) 2.0% to 3.8%, Renderer::draw 1.3% to 2.6%, do_write32 1.0% to 2.0%,
  Ta::feed and DisplayList::handle_vertex. fn_0c641e40 and fn_0c64162c are larger at full speed
  (10.8%, 4.8%): probably the game's own wait loop.

## 2026-10-31: switch-test12 (resolve without a working array, profile split by speed)

- oit_resolve.frag: no per-pixel array. The list is counted, then walked once per fragment blended,
  each time taking the farthest fragment after the last one blended (ties: submission order, then
  the newer node, as the stable sort did). Past kMax fragments the kMax-th nearest is found first
  and only fragments from it on are blended. rcap: captures 002, 006, 009, 013, 015, 016
  byte-identical, accurate and lean (013 and 015 have lists of 10 and 11, so lean's dropping is
  exercised).
- Counter word 4: the longest list walked in a render (counted only past 4 nodes, to keep pixels off
  one atomic); log `longest list`. rcap: 7 to 11.
- Profiler: samples also split by `dream_port_slow` (the launcher: the last half second of play ran
  below 0.95x); sections pc_fast, pc_slow, stacks_fast, stacks_slow.
- The previous launch's log, error log and profile are kept as sonicadv.prev.log,
  sonicadv.prev.err.log and sonicadv_profile.prev.txt.
- Mark: the game slows when ground fills the view and runs smoothly looking at sky. The Dreamcast's
  graphics chip has no transform stage, so the game's code positions every vertex on the CPU; on
  the port that is translated code on the game thread. Also measured: our renderer's CPU work and
  the present run on the game thread, 12 to 23% of it in Emerald Coast (switch-test11), highest in
  the slow stretches.

## 2026-10-31: switch-test11 result

- Mark's first run was lost: the log and profile start fresh each launch, and the test's second
  launch (to check pipelines built ahead) overwrote them. Rerun as one launch.
- Pipelines: `pipelines: 6 built ahead from earlier runs in 6 ms` before the first frame.
- Accurate, character select: resolve 657 ms per render (test9: 405 ms); 181 translucent polygons,
  1.5 layers per pixel, no long or unterminated lists counted. Menus (memory card select and
  others): resolve 60 to 260 ms with 15 to 111 translucent polygons and about 2.2 layers; a single
  full-screen layer resolves in 0.2 ms. Emerald Coast accurate: resolve 0.3 to 3.9 ms, speed 0.82x
  to 1.00x. The cost tracks pixels with two or more fragments, at roughly half a million GPU
  cycles per pixel, far beyond the work; rcap on lavapipe is fast and correct. Cause not known:
  either the walks are much longer on the Switch than the counters say, or NVK handles the
  per-pixel index array very badly. The single-array resolve did not help (Mark: Accurate felt
  slower, menus very slow).
- Per triangle: GPU 0.7 to 1.2 ms per render everywhere, character select included; Emerald Coast
  0.72x to 1.00x, game thread bound.

## 2026-10-30: switch-test10 result; switch-test11 (single-array resolve, per triangle, pipelines at launch)

- switch-test10 (Mark): the safety valves sent most of play down the per-strip sort, which hides
  the problem instead of fixing it. Mark's requirement: full speed with accurate characters and
  translucency, no silent fallback. Both valves are removed; slow resolves are still counted.
- Research: Flycast (GPL-2.0) has three modes: per pixel, per triangle (its default) and per strip.
  Its per-pixel resolve keeps one array of node indexes per pixel and reads depth and blend data
  from the node buffer when sorting, rather than copying whole fragments into several local
  arrays. Our resolve copied colour, depth and order into three arrays of 32 entries. On NVK
  those arrays live in per-thread local memory; suspected (not confirmed) to be why character
  select took 405 ms per resolve with about 1.2 fragments per pixel.
- switch-test11:
  - oit_resolve.frag: Flycast-style single index array; a pixel with no fragments returns at once.
    rcap: captures 002, 006, 009, 013, 015, 016 byte-identical to before, accurate and lean.
  - Per triangle (menu value TRIANGLE, file value `triangle`): translucent strips are split into
    triangles, each keyed on its farthest vertex and sorted stably back to front; odd triangles of
    a culled strip get the cull face flipped. rcap against accurate: 9,451 to 15,772 pixels differ
    (per strip: 9,882 to 16,219); errors on the Emerald Coast shoreline water layers and small
    ones on Sonic's glove at character select. It is an approximation, as in Flycast.
  - Menu order: Accurate, Lean, Triangle, Strip (Strip was called Fast; the file value `fast` is
    kept).
  - Pipelines: every pipeline variant seen is listed in `sonicadv_pipelines.bin.keys` with a Vulkan
    pipeline cache in `sonicadv_pipelines.bin` (SD card, app folder). At launch they are all built
    before the first frame, plus the per-pixel pipelines; saved every 300 presents when new ones
    appeared. Log lines `pipelines: N built ahead` and `pipelines: saved N`.
  - dcdisc.pytest fails on the cloud machine only because pycdlib is not installed there.

## 2026-10-30: switch-test9 result; switch-test10 (list counters, safety valves, menu on Minus)

- switch-test9 GPU timing (Mark; CPU 1785, GPU 768, memory 1600): Emerald Coast 1.7 to 2.7 ms per
  render with the per-pixel pass on nearly every frame, accurate or lean alike; speed mostly 0.95x
  to 1.00x, some stretches 0.85x to 0.92x with the game thread 80 to 86% busy (CPU-bound now).
  Character select: resolve 405 ms per render accurate, 97 ms lean (gather 1.4 ms), 179 to 181
  translucent polygons over 83% of the screen; after a character is picked (176 polygons, 79%)
  2.7 ms. Mark: lean helped; Plus (for the old Minus+Plus menu) is Start and picked a character.
- The deep-stack explanation was wrong. Captures 013 and 015 are character select (181 translucent
  polygons): rcap counts about 316,000 fragments (about 1.2 per pixel), no list past 64 nodes, none
  unterminated. The 4.2x ratio between 32 and 8 kept layers matches a walk stopped by the 4096-node
  guard (the replacement scan is proportional to kMax), so the Switch's lists on that screen are
  suspected to be damaged or cyclic. Not confirmed.
- switch-test10:
  - Window (Switch): the menu is Minus alone; Plus is only Start.
  - Counter buffer gains long_lists and unterminated, written by the resolve; the counter words
    are copied to a host buffer after each per-pixel pass. Log: fragments per render, peak,
    capacity, overflows, pixels over 64 layers, pixels whose list never ends.
  - Safety valve: a frame whose translucent layers (estimated from polygon areas over the
    rectangle) exceed 12 per pixel uses the per-strip sort; and a render whose resolve takes over
    30 ms sends the next 600 renders down the per-strip sort. Both are counted in the log. The
    estimate on the captures: 0.7 to 1.5 layers.
  - rcap: all captures unchanged.
  - NRO sha256 5c0a8ad2767853a094b00e4159381faf44ddb90f63deab55279c9860058579ce (the bridge timed
    out on many parts tonight; resent one at a time and checked).

## 2026-10-30: switch-test8 result; switch-test9 (GPU timing, lean translucency)

- switch-test8 (Mark played the whole of Emerald Coast at CPU 1785, GPU 768, memory 1600,
  translucency accurate): mostly 0.88x to 1.01x, average about 0.93x (test7 at 1785/768 without
  the memory clock: about 0.83x). Short drops to about 10 fps line up with 5-second stretches where
  the game thread waited on the GPU for 22 to 57% of the time. Back at character select: 1.5 fps,
  the GPU wait 97% (about 650 ms per frame); the port menu could not be reached, so the run ended
  from HOME. Translated game code is now the largest CPU item (37.8%); call_indirect 6.5%; resolve
  and store-queue stores dropped to about 2.6% together (from about 8%). Exception unwinding
  (uw_update_context, read_encoded_value) about 1% during play.
- Suspect for the GPU stalls (not measured on the GPU yet): the per-pixel resolve keeps up to 32
  fragments per pixel in per-thread arrays; screens with many stacked translucent layers make it
  very expensive on the Switch's GPU.
- switch-test9:
  - Offscreen writes GPU timestamps (start, after the opaque pass, after the gather, after the
    resolve, end) and sums per-pass times on each finish(); translucency stats (translucent
    polygons, renders with the per-pixel pass, the rectangle's share of the screen). The timing
    log prints a gpu: line every five seconds.
  - oit_resolve.frag: kMax is specialisation constant 0; the renderer builds a second resolve
    pipeline with 8. Port menu translucency: accurate, lean (8 layers), fast
    (TRANSLUCENCY=lean in the options file).
  - rcap: captures 002, 006, 009, 013, 015 render identically with lean and with accurate (no
    pixel there has more than 8 layers); accurate is unchanged from test8.
  - NRO sha256 5342cbd957a4946b90fe5b45e07e7b389b91d234ac09520a0c07f412c023df2d.

## 2026-10-30: switch-test7 result; switch-test8 (GPU present, bounded translucency, clocks)

- switch-test7 (handheld: GPU stock 307 MHz; CPU 1785): sound thread on core 2 busy 20 to 32%,
  the game never waited for it. Gameplay: GPU 307 accurate about 0.63x (GPU wait 13%); GPU 614
  0.72x to 0.87x; GPU 768 accurate 0.79x to 0.97x, typically 0.83x (GPU wait 5%); GPU 768
  translucency fast 0.97x to 1.00x (only about 10 s logged). CPU back to stock with GPU 768 and
  fast: 0.39x to 0.56x. Mark: GPU clock helps, translucency more; he wants full speed with the
  accurate setting.
- test7 profile (153,387 playing samples, mixed clocks): translated code 35.4% (fn_0c641e40
  10.2%, the object loop calling through call_indirect), find_function 7.1% (up from 3.5%: the
  cache grew to 4096 x 32 bytes), __aarch64_read_tp 4.0%, DcMemory resolve 4.3% and store<u32>
  3.8% (store-queue words from fn_0c037990/fn_0c037294), GPU fence wait 13.5%, memmove 3.9%
  (Presenter::upload), swapchain acquire 2.0%.
- switch-test8:
  - Offscreen: the readback is recorded only when Live needs pixels in video memory
    (writeback) or DREAM_CPU_PRESENT=1; pixels() copies on demand. An incoming subpass dependency
    orders the next render after copies reading the colour image. The per-pixel translucency
    pass is limited to the rectangle of the frame's translucent polygons (opaque copy, gather and
    resolve scissored); none on screen, no pass. DREAM_OIT_FULL=1 restores the whole target.
  - Presenter::copy_from takes the frame from the colour image on the GPU; Presenter::overlay
    puts the FPS counter (an opaque tile) over it. The menus still composite on the CPU.
  - Port menu: memory clock (stock, 1331, 1600; PcvModuleId_EMC).
  - find_function cache: 1024 slots of 24 bytes; call_indirect checks it inline first.
  - DcMemory::do_write32/do_write64: store-queue words go straight into sq_ when nothing watches
    stores.
  - Checks: rcap (lavapipe) on captures 002, 006, 009, 013, 015, 016: bounded translucency
    byte-identical to the full pass, on-demand readback identical, 013 identical to test6's
    output; the validation layer reports nothing from rendering. runtime.unit (new: store-queue
    quick path), render.unit, dream_emit_tests pass.
  - NRO sha256 d3d9d086a138f9746299c9c6a475a9c2eeb6748853b4840fbe43b56943e8cc8a.

## 2026-10-30: switch-test6 result; switch-test7 (sound thread, clocks, lookups, disc)

- switch-test6 (1785 MHz): gameplay about 0.59x (test5 about 0.55x). GPU fence wait 17.8% of the
  game thread (22.3%), FPS counter 1.3% (3.3%). The wait remains because the GPU is still busy
  when the frame is needed: drawing itself is slow on the Switch. Suspect: the per-pixel
  translucency (unverified). Mark: looks the same, nothing broke, marginal gain.
- Goal set by Mark: full speed at 1785 MHz first, then a researched strategy for stock clocks.
- switch-test7:
  - Aica::start_thread (default on the Switch, --aica-thread / --no-aica-thread): ARM7, timers and
    mixer on core 2; the SH-4's clock owes batches of 32 samples, the thread may trail by up to
    2048 before the SH-4 waits; SH-4 register access takes the thread's lock; Holly's line is
    re-evaluated on the SH-4 side each batch. Tests: same samples as on the game thread; SH-4
    register access mid-run; clean under ThreadSanitizer; 40 repeated runs.
  - Port menu: GPU clock (stock, 460, 614, 768) and translucency (accurate per pixel, or fast per
    strip, switchable between frames). switch_main.cpp: one clock helper for CPU and GPU.
  - find_function caches signed (overlay) entries with their signature, checked on each hit.
    Test: overlays swapped back and forth.
  - GdiDisc reads 64-sector windows. Test: a generated GDI read out of order across windows,
    backwards and across tracks.
  - Timing line: with the sound thread, its busy share and the game's waits for it.
  - NRO sha256 bc1cf258209e5512a1c08af22705d4fa642246252aab10af7821b9929688a23d.

## 2026-10-30: switch-test5 profile (play only); switch-test6: GPU overlap, cheap counter

- switch-test5 profile: 126,585 playing samples (1785 MHz, FPS counter on), 1,408 in a menu,
  13,676 after the run. Correction to test4: malloc/free during play is 0.3%; test4's 15% was
  the end-of-run teardown, which test4 could not separate.
- Playing: GPU fence wait 22.3% (nvFenceWait from Offscreen::render), sound 20.2%
  (Mixer::dsp_step 11.0%, Arm7::step 4.7%, Mixer::sample 1.9%), translated code 21.1%
  (fn_0c641e40 6.8%, fn_0c64162c 2.5%, fn_0c037990 2.4%), memory layer 11.2% (stores and
  store-queue flushes to non-RAM: the game's TA submission, fn_0c037990/fn_0c037294), renderer CPU
  5.1%, FPS counter 3.3% (fill_rect from draw_text from Live::draw_counter), find_function 3.0%
  (call_indirect from fn_0c641e40), __aarch64_read_tp 2.6% (nearly all from fn_0c64162c),
  memmove 2.6% (Presenter::upload 1.2%), disc reads on the game thread 2.6% (fsFileRead from
  stdio), swapchain IPC about 0.9%.
- gdrom syscalls: 2.60M in test4 (80 guest s) and 2.61M in test5 (146 guest s): not proportional
  to play time, so concentrated in loading rather than a steady polling cost.
- switch-test6: Offscreen::render submits and returns; finish() (called from pixels() and at the
  start of Live::render) waits, so the guest runs while the GPU draws. DREAM_SYNC_RENDER=1 restores
  the old wait. rcap on lavapipe: capture-013 renders byte-identical both ways. fill_rect: opaque
  rows are plain stores; translucent pixels use the exact (t + 1 + (t >> 8)) >> 8 for t / 255
  (checked for every t below 65535) instead of a division per channel. render.unit passes (78
  cases). The timing line reports the GPU wait as falling inside render or present. NRO sha256
  afc8c8dbb2c143eb31ed89f55654732c3b87a4fc2e3953398e41d1720dfdbb83.

## 2026-10-30: switch-test4 profile; switch-test5 profiles with callers

- switch-test4 profile (107,655 samples; Mark ran mostly at 1785 MHz). Two flaws found while
  reading it: offsets were raw addresses, because libnx's __start__ is an absolute symbol (0),
  not the load address (the crash handler had the same bug); and the file was cut off mid-write
  (the launcher ends the process before main() regains control), losing the lr section. The load
  address was recovered from the samples: at base 0x523a5000, 10 of the 30 hottest addresses fall
  exactly on svc instructions (svcWaitSynchronization+4, svcSendSyncRequest).
- Split of the game thread (includes menu and end-of-run time, which test4 could not exclude):
  translated game code 20.3%, waiting in the kernel 19.4% (svcWaitSynchronization 13.3%, mostly
  the GPU fence; svcSendSyncRequest 5.6%), sound 16.0% (Mixer::dsp_step 8.8%, Arm7::step 3.6%,
  Mixer::sample 1.4%), malloc/free 15.1% (nearly all at one instruction in _free_r: dlmalloc's
  sorted large-bin insertion walking a long list), memory layer 8.7% (non-RAM paths: resolve,
  store<u32>), overlay fill_rect 7.0% (the menu's full-screen shade; probably menu time),
  thread-local reads 2.1%, find_function 2.3%, scheduler 1.3%.
- Not known yet: who frees so much, and whether it happens during play or in teardown.
- switch-test5: the profiler takes the load address from svcQueryMemory, walks frame records on
  the game thread's stack (its mirror mapping) for up to 8 callers, counts only samples while
  playing (dream_port_phase from the launcher: menu and after-run samples are counted apart), and
  writes to a .tmp then renames. The crash handler uses the same load address. NRO sha256
  54335b503eb9e08b7c121dae854c1cc9c930ea451f1d3525876f060f6d6bad8c.
- tools/recut_patches.sh rebuilds 0022 and 0023 from the dream-recomp tree and checks the set.

## 2026-10-30: switch-test3 result (no gain); switch-test4 is a sampling profiler

- switch-test3 on Mark's Switch, all at stock 1020 MHz (no clock change in the log; no report in
  switchlogs). He felt no difference, and the numbers agree: the gameplay stretch at about 10
  renders/s ran 0.31x to 0.37x (test2: 0.31x to 0.37x for the same pattern). Other stretches
  0.32x to 0.49x. Split: game 40 to 63%, sound 9 to 29% (typically about 20%), render 10 to 47%
  (GPU wait 5 to 41%), present 4 to 8%.
- So patch 0023 (inline main RAM) made no measurable difference at stock. The PC profile it was
  based on was mostly the intro movie, on a different CPU; it did not carry over to Switch
  gameplay. 0023 stays for now (neutral, code 55% larger); the profile decides.
- New and measured: sound (the AICA's ARM7, timers and mixer) is about a fifth of the game
  thread, on the same core as the game.
- switch-test4: switch_main.cpp samples the game thread about 500 times a second from core 2
  (svcSetThreadActivity pause, svcGetThreadContext3, resume) and writes pc and lr counts as module
  offsets to sonicadv_profile.txt every 20 s and at exit. Offsets are ELF addresses (__start__ is
  0). NRO sha256 8f67f93e0f4348cb25421cd25d0d09dfd608c37f25437f7e8e1aa839cf17906a.

## 2026-10-30: switch-test3 (inline main RAM, sound in the timing log)

- switch-test2 timing (Mark's Switch; menu-open lines dropped). Stock 1020 MHz: speed 0.30x to
  0.40x, game thread 70 to 81% game, 13 to 34% render (mostly GPU wait), about 5% present.
  1785 MHz: 0.49x to 0.62x, 68 to 75% game, 19 to 27% render. So the game bucket dominates: moving
  rendering off the thread caps at about 0.46x at stock. A 75% faster clock gave about 50% more
  speed. Mark confirmed the letterbox bars are plain black in test2.
- What is inside the game bucket on the Switch is not measured yet. The best lead is Mark's PC
  profile (out/logs/profile.txt, a run mostly in the intro movie): translated guest code 9.7% of
  the game thread, DcMemory load/store, note_ram_write and resolve about 45%, Arm7 about 4 to 5%.
- Patch 0023: emitted code calls Memory::read8..write64, which were pure virtual. They are now
  inline (forced, since game units build at -O1) and serve main RAM (area 3 through P0 to P3)
  straight from DcMemory's buffer; everything else goes to the renamed virtual do_read8 and so on.
  The store observers (journaling, hash_writes, watch_lo/hi) moved to Memory so the inline store
  path can see them: while any is armed, every store takes the full path. Tests: runtime.unit
  (new: the inline path through Memory& matches, and an armed write hash still sees every store),
  dream_emit_tests (12 cases, 35,811 assertions) pass on the host.
- Timing log: a sound share (Aica::sample_tick, one tick in eight timed), taken out of the game
  share; present calls while the menu is open now count as menu time, which removes the negative
  percentages. The report's ARM7 pc histogram samples one tick in sixteen (it was a hash-map
  update on every one of 44,100 ticks a second).
- Built: 342 steps, no errors or warnings. part18's 5,113 memory accesses compile to the inline
  area-3 test (5,465 compares against 0xDFFFFFFF), no calls. The NRO grew from 98,287,647 to
  152,793,119 bytes: the inlined checks enlarge the code, which costs instruction cache; whether that
  eats into the gain is for the test to show. sha256
  5ced187a11cd986a3a7b55363d2353762c9cc0d844ea66eb7dd85d3de6f7ddad.
- Seen in the disassembly, for later: every emitted function entry reads a thread-local flag
  (__aarch64_read_tp twice) before its interrupt check.

## 2026-10-29: switch-test2 (letterbox bleed, timing log, CPU 1785 by default)

- switch-test1 on Mark's Switch: boots and plays, the PC fixes held, but slow, and the area right
  of the 4:3 picture showed its textures smeared out. Logs: startup only (no timing log existed);
  no `[clock]` line, so it ran at the stock 1020 MHz; no report because the app was closed from
  the HOME menu, which ends the process before the launcher writes one.
- The bleed: present.vert's covering triangle is shrunk to letterbox but overhangs the image on its
  right and bottom, where the coordinates pass 1 and the clamped sampler repeats the last column.
  A 4:3 PC window has no bars, so it never showed. present.frag now draws those pixels black.
- Timing log: every five seconds the launcher prints fps shown, renders/s, guest speed, and the
  game thread's split: game, render (and the GPU fence wait inside it, Offscreen::wait_ns),
  present, pacing sleep, paused. On by default on the Switch; `--perf-log` elsewhere.
- switch_main.cpp: a first run writes sonicadv_options.txt with CPU_MHZ=1785; after that the port
  menu's choice stands.
- Patch 0022 re-cut (verified: the set reproduces the submodule tree). NRO 98,287,647 bytes, sha256
  98379df69a9676731c4535f79cc0d0e9f7a615f65e558ad0bf40459af735e7a6.

## 2026-10-29: first Switch NRO built (switch-test1)

- All 319 game parts and the libraries compile for the Switch. Two fixes to link:
  switch_main.cpp lacked <cstdint>; devkitPro's libSDL2.a pulls SDL_egl.o in through its video
  code and wanted EGL. Linking switch-mesa's libEGL would bring Mesa's GL stack next to NVK, so
  switch/src/egl_stubs.c fails every EGL call instead (SDL's video is never initialised; audio
  only).
- NRO 98,287,647 bytes, sha256 7a66df38dd8224ca676ec332c4907f58670130feb375812c807326d6b159b5b0.
  The matching ELF (for crash offsets) is kept with it.
- Patch set 0001-0022 checked: applied to 51a148c it reproduces the submodule tree exactly.

## 2026-10-29: Switch phase started (patch 0022, switch/)

- Mark: with 0021 the intro plays as real video, a bit slow (0.8x in its profile: DcMemory stores
  about a third, the YUV converter writing word by word, textures re-created each frame).
  Agreed to move to the Switch; movie speed comes back with Switch performance work.
- Toolchain in the cloud: Mark's C:\devkitPro (devkitA64, libnx, portlibs with switch-sdl2,
  cmake, tools) copied over the bridge and run under Wine with CoD-nx's
  tools/linux/setup_switch_wine.sh; mesa-switch NVK (libvulkan.a, headers, Mesa license) from
  ghcr.io/danfromtico/switch-dev-legacy-vulkan (MIT; Mark's OK). glslang is not needed: the
  renderer's shaders are SPIR-V at build time. One 10.7 MB part compiles in 43 s at -O1.
- Patch 0022: a host dream-translate for cross builds (DREAM_HOST_TRANSLATE), libchdr static on
  the Switch, render's Switch branch (NVK, render/src/vk/window_switch.cpp: VI surface, libnx
  pad, SDL's positional binding names mapped to the Switch's buttons, Minus+Plus for the menu),
  an SDL2 audio sink, the sink's volume, Presenter::stretch, and the port menu
  (render/include/dream/render/port_menu.h; PC: F1; options in --options FILE; the game pauses
  while it is open). Tests: render.unit (port menu options and navigation).
- switch/src/switch_main.cpp: logs per stream, crash reports as module offsets through the fs
  service, terminate handler, applet-type check, the CPU clock for the port menu (clkrst; not
  under Horizon OC), the launcher on a 256 MB-stack thread on core 1. switch/build.sh builds and
  stages the NRO and an SD copy of sonicadv.toml; tools/win/stage_switch.ps1 copies the disc,
  1ST_READ.BIN and the memory card on Mark's PC.
- Not yet built for the PC with 0022 (MSVC). The libraries, the launcher and switch_main compile
  for the Switch; the full build is running.

## 2026-10-29: movie frames never refreshed; video memory writes now invalidate textures (0021)

- With 0020 the Perfect Chaos intro showed one clean frame (water) and "shook". The intro is
  not SA1_600.SFD (the file play.ps1 hides); likely SAH448_6.SFD, the 448-line PAL file, not
  yet confirmed. Capture 16: one polygon, a 512x512 YUV422 stride texture at 0x1C0000, u
  0..0.624 (320/512), v 0..0.875 (448/512); TEXT_CONTROL 0x0A (320 texels);
  TA_YUV_TEX_CTRL 0x1B13 (20 x 28 macroblocks = 320x448, 4:2:0 input); TA_YUV_TEX_BASE
  0x140000. The frame decodes cleanly at the new pitch.
- Cause of the freeze: the texture cache was only ever invalidated by palette changes and the
  (off) framebuffer write-back. The game double-buffers the movie at 0x140000 and 0x1C0000, so
  the first two decoded frames were shown alternately for ever.
- Patch 0021: DcMemory marks 4 KB pages of video memory on every store, write64 and store-queue
  burst that lands there (DMA and the YUV converter go through store); before each render the
  launcher passes each run of written pages to TextureCache::invalidate_range. The window
  summary counts the re-decoded textures. Test: page runs from 32-bit, 16-bit and store-queue
  writes, taken once, main RAM excluded.

## 2026-10-29: sound confirmed; movie texture pitch (patch 0020)

- Patch 0019 on Mark's PC: "sound was all gloriously correct". Report: 49 key-ons,
  1,915,391 of 2,537,093 samples non-zero. A movie played (4,784,640 words through the YUV
  converter); its audio was right, its picture "a wonky mess".
- Likely cause, not yet confirmed on a capture of the movie: the texture cache always passed a
  stride width of 0, so a stride texture (how movies are drawn: frames are rarely a power of
  two wide) was read with its power-of-two width as the row pitch, shearing every row. And when
  a width was passed, describe_texture replaced the texture's width with it, which would have
  stretched the mapping. Patch 0020: TextureInfo::pitch (the TEXT_CONTROL stride for stride
  textures; width stays the power of two the coordinates use), scan-order decoding steps by the
  pitch, the cache keys stride textures by the stride, and the launcher sets the stride from
  TEXT_CONTROL every frame. Test: a 320-texel-pitch YUV422 frame in a 512x256 texture (fails on
  the old pitch, 3 of 16 assertions).

## 2026-10-29: sound: 32-bit key-on writes (patch 0019)

- Mark: patch 0018 "perfect"; the Station Square glows in the Adventure intro look right.
- Last play report: the AICA's ARM7 ran 165,695,113 instructions and wrote 20,554 channel
  registers, but 0 key-ons and 0 of 2,540,771 samples non-zero. SH-4 wrote 9 AICA registers.
- Cause: AICA registers are 16 bits on a 32-bit stride. Aica::reg_write passed the access size
  through to the mixer, whose key-on check (KYONEX, bit 15 of channel +0x00) fired only for
  16-bit writes or byte writes of +0x01. The driver on the disc (SONICAD/MASARU.DRV,
  disassembled with arm-none-eabi-objdump) keys voices with 32-bit stores: at 0x11F0
  `orreq r0, r0, #0xC000` then `str r0, [r8]` (key on), and at 0x17BC/0x180C
  `orr r1, r1, #0x8000` then `str r1, [r8]` (key off), r8 = 0x00800000 + channel * 0x80.
- Fix: a 32-bit channel register write reaches the mixer as a 16-bit write of that register.
  Test: key-on and key-off by 32-bit writes from the SH-4 side and the ARM side (fails without
  the fix, 8 of 8 assertions).

## 2026-10-29: per-pixel translucency (patch 0018): Sonic's eyes

- Captures 12 and 13 (end of Emerald Coast, character select): Sonic's irises partly or wholly
  missing. The irises (tcw 8804d128) and eye whites (0804d118) are all in the translucent list.
  The renderer sorted translucent strips by their nearest vertex; the PowerVR sorts per pixel
  (ISP_FEED_CFG = 0x00800408 in every capture: bit 0, presort, clear). Two eye-white strips
  have a corner nearer than the irises, sort after them and cover them.
- Scored against a per-pixel reference (scratch rasteriser over the captured vertices): the
  per-strip sort put the wrong translucent surface on top in 1.3% (capture 13) to 21% (capture
  12) of translucent pixels; strip centroid, strip farthest and per-triangle keys were each
  still 1% to 19% wrong somewhere. No sort key fixes it.
- Patch 0018: translucent fragments are appended to per-pixel lists in a gather pass (depth
  tested in the shader against the opaque pass's depth, by each polygon's own depth mode), then
  a resolve pass sorts each list (farthest first, submission order on ties, at most 32 kept)
  and applies the hardware's blend instructions over a copy of the opaque image. Needs
  fragmentStoresAndAtomics (already enabled by the context); DREAM_NO_OIT=1 or a GPU without it
  falls back to the per-strip sort. Shading moved to shaders/shade.glsl, shared with
  geometry.frag.
- Checked offline with the new tools/rcap (re-renders an F11 capture through the real renderer
  on lavapipe): its render of capture 13 with the old renderer matches the screenshot Mark's PC
  took (mean difference 0.04 per channel, no pixel off by more than 8); the refactored opaque
  shader renders byte-identical; synchronization validation is clean after one fix (the load
  of the colour attachment). Results: both irises in captures 12 and 13; in capture 12 the
  lives icon is opaque (its colour is ffffffff and its texture has no alpha) where before a sea
  strip at 60% alpha with one vertex at 1/w = 1.0 sorted last and was drawn over it, and over
  Tails' leg; the same sea strip had been drawn over the capsule in capture 11.
- Noted, not changed: background.cpp reads the cheap-shadow bit from 0x098 (ISP_FEED_CFG), while
  KallistiOS lists cheap shadow at 0x074; Sonic Adventure leaves it 0 either way.

## 2026-10-29: patch 0017 confirmed: full speed in Emerald Coast

- Play run on Mark's PC: "readback memory: host-cached", guest 403.0 s in host 403.3 s (0.9x
  overall including loads; Mark: "phenomenal"). Profile (207,752 samples): 35% of the game
  thread now waits in ZwWaitForMultipleObjects. Inclusive time places it under
  Live::render > Offscreen::render > the AMD driver: the CPU waits for the GPU to finish each
  frame, because the offscreen render is submit-and-wait. Not vsync, not spare time: the guest
  cannot run during it. Rendering one frame behind (overlapping GPU and CPU) is the next large
  speedup, and matters most on the Switch.
  The memcpy is down from 43.8% to about 1%; resolve() from 9.9% to 0.7%.
- Remaining real work: DcMemory load/store (about 16%), AICA ARM7 plus DSP (about 11%),
  find_function plus call_indirect (about 4%), translated code 11%. Headroom for the Switch,
  where the CPU is far slower; not needed on this PC.

## 2026-10-29: first profile; cached frame readback and a main RAM fast path (patch 0017)

- Profile of a full Emerald Coast run on Mark's PC (473 host s, 303 guest s, 0.6x real time,
  244,954 samples, 0 unreadable; function names resolved from the PDB). Self time: 50% other DLLs,
  40% launcher runtime, 10% translated guest code.
- 43.8% of all samples sit in one copy in VCRUNTIME140 (shown as `_NLG_Return2`, the nearest
  export: that DLL has no symbols loaded, so this is memcpy) under Presenter::upload (44.0%
  inclusive). The copy reads the offscreen target's readback buffer, which was allocated
  HOST_VISIBLE | HOST_COHERENT without HOST_CACHED: on a discrete GPU that is uncached for CPU
  reads. Fix: HostBuffer::ensure(read_back) prefers HOST_CACHED (coherent if available,
  otherwise vkInvalidateMappedMemoryRanges after the fence), plus a transfer-to-host barrier.
  The launcher prints "readback memory: host-cached" so the log shows what it got.
- Next: DcMemory::resolve 9.9% and load<u32> 8.0% self. Loads and stores to main RAM (area 3
  and its mirrors) now skip resolve(); stores keep journaling and the write watch. New test.
- Then: AICA emulation (ARM7 plus DSP, about 7%), find_function 1.3%, call_indirect, and
  incremental-link thunks (ILT entries, about 2%).

## 2026-10-29: sampling profiler (patch 0016)

- Speed is 0.6x to 0.7x real time and the untranslated calls (a few thousand per run) cannot
  explain it, so the next step is measurement. Patch 0016 adds `--profile FILE`
  (runtime/boot/profiler.cpp, Windows only): a thread samples the game thread about 1,000 times a
  second (SuspendThread, GetThreadContext, RtlVirtualUnwind over the image's unwind tables; no
  allocation while the target is suspended), counting self and inclusive time per function, and
  names them from the PDB at exit. The report groups self time into translated guest code
  (fn_XXXXXXXX), the launcher runtime, and other DLLs (driver, OS). play.ps1 always passes it
  (out\logs\profile.txt).
- Checked here with a MinGW cross build under Wine: 3,601 samples in 6 s while throwing an
  exception every thousand calls, every stack unwound to RtlUserThreadStart, no stalls. Names did
  not resolve there (MinGW writes DWARF, not PDB); the MSVC build writes a PDB.

## 2026-10-29: graphics pass 1 confirmed on Mark's PC; captures stop overwriting (0015)

- Play run with 0013 and 0014 (87 guest s, 0.6x real time): Mark reports big improvements. His
  six F11 shots show a clean sky, textured grass, rock walls without streaks, round shadows and
  clean sand and water. One remaining oddity under the rock arch is the game's lens flare drawn
  over the rock (translucent list, VQ 1555 flare textures), likely correct; not checked against
  hardware.
- F11 numbering restarted at 000 each run and overwrote the earlier captures. Patch 0015 skips
  numbers whose .ppm already exists.

## 2026-10-29: graphics pass 1 (patch 0013, one sampler per texture); Emerald Coast seeds

- Mark's F11 captures worked: 7 captures of Emerald Coast (capture-NNN.{ppm,ta,vram,regs,txt}).
  A scratch inspector (built on dream_render) lists each polygon's texture words, UV range and
  screen bounds, and decodes any texture to an image.
- Texture decode is not the fault: the wall, water, sand and rock textures in capture 4 decode
  cleanly (VQ, 4444, 565, twiddled, mipmapped).
- Found: polygons sharing one texture ask for different clamp, mirror or filter settings (the
  rock texture tcw 0x88061640 is drawn with V clamped, U+V clamped, and wrapping, in one frame).
  Both the renderer's per-frame key and the texture cache key ignored those TSP bits, so all of
  them got whichever sampler was built first. A strip that wraps but gets a clamping sampler
  smears its edge texels into lines. Capture 4: 23 of 96 textures, 1,270 of 1,846 textured
  polygons are in that situation. Patch 0013 adds texture_sampling_key (tcw, size bits, sampler
  bits) to tsp.h, used by both keys, with a unit test from the captured words. Cost: a texture
  used two ways is decoded twice. Expected to fix at least some of the streaked rock walls; not
  yet seen on Mark's PC.
- Patch 0014: the decoder read every mipmapped 16-bit texture from 2 bytes past the 1x1 level's
  padding start too early: the chain begins 6 bytes (three texels) in, not 2. Evidence: the sky
  texture in capture 4 matches BG_BEACH.PRS byte for byte at the 6-byte offset; across 115
  mipmapped 565 textures in captures 4 and 5, 111 are smoother at 6 than at 2 (mean roughness
  13.9 vs 8.6); Sonic's 16x16 4444 shadow decodes as a round blob at 6 and a blocky cross at 2.
  Read 2 texels early, every 2x2 block of a twiddled texture is scrambled: the speckled sky,
  the blocky shadows, and grainy world textures. Tests: a 16-bit chain in all three formats
  (fails on the old offset) and a VQ chain (unchanged). 8-bit palette chains left as they were;
  no capture has one yet.
- Seeds from the Emerald Coast run (suggested.toml): 19 targets. Eight of them are the tails of a
  trig lookup at 0x0C085990 that ends in `braf r1` through a halfword table at 0x0C085A38; all
  eight table targets are now seeds. They total about 3,400 calls in 138 guest s, so they are
  not the cause of the 0.7x speed; that needs a profile.

## 2026-10-29: past Station Square's title card (patch 0012); saves kept

- Boot 9 on Mark's PC (0012): ran the full 120 guest s into Station Square, 0 unmapped reads,
  6,002 sectors, 0.7x real time in the window. Mark saw the area: models and world textures off,
  lamppost glow and helicopter spotlight low-res/odd. 7,183 untranslated calls, almost all into a
  module at 0x8CB80000 (busiest 0x0CB830CA x2622): the next speed lead, file not identified yet.
- Saves: the launcher already supports `--vmu FILE` (created blank and formatted if missing).
  boot_test.ps1 and play.ps1 now pass out\saves\vmu_A1.bin. Headless check (30 guest s, scripted
  Start): 48 block writes; the card's directory holds SONICADV_INT, "SONIC ADVENTURE / Main Save
  File", 10 blocks.
- A complete save Mark found (raw card image, 135,253 bytes: the 128 KB card plus 4 KB trailer)
  holds SONICADV_INT, the same file name the PAL build writes, plus Chao data and an SA2 save.
  Installed as out\saves\vmu_A1.bin (first 128 KB); the game accepts it as a complete save, so
  stages can be started from Trial mode.
- The 0x8CB80000 module is cutscene code: the 236 EV*.PRS event files are linked there, each with
  its own code (hashes of the busiest functions differ per file), 2.3 MB of code in all. Left for
  later: stages matter more for playability.
- Added the other 32 code modules (stages, fields, bosses, Chao garden, minigames, summary) as
  overlays at 0x8C900000, each sized to its dense code (last 4 KB window with 6+ rts and 4+
  sts.l pr, plus one window) and seeded at 0 or, for SUMMARY, its leading pointer table (0x20 is
  not a shared entry: data in several modules). Translated here in 17 s, no errors: 34 module units,
  10,692 functions, about 101 MB of C++ beside the main program's 309 MB.

## 2026-10-28: Station Square's bad model reads were the boot register bank (patch 0008)

- Patch 0010 did not change Mark's crash (same pc). Trace with entries (patch 0011, Mark's PC,
  cycles 7.040e9 to 7.0446e9) shows the cause directly: fn 0x0C04E714 is entered at 191,801
  (pr 0x0C04EA94, r15 0x8C00F34C); its jsr at 0x0C04E72C calls 0x0C099990, the task yield, which
  ends `mov.l @r15+,r0; jmp @r0; lds.l @r15+,pr`; at 191,833 a second activation of 0x0C04E714
  starts as a RESUME at 0x0C04E730 (r15 0x8C00F32C) with no runtime event in between: the jmp went
  through call_indirect, which found 0x0C04E730 inside 0x0C04E714 and called its resume entry
  nested. That activation finished the function and popped the frame; the first, still waiting in
  C++ behind the jsr, then ran the same code again on the popped frame (r15 32 higher), and its
  rts took stack data as PR: the crash.
- Patch 0012: a computed jump to the function's own entry PR is a return; any other computed jump
  goes through jump_indirect (entry: tail call; inside a translated function: non-local transfer
  to run_guest; else the untranslated path). Test: _main3/_jret in p13 (returns via jmp @r0); it
  fails without the fix exactly as the game did (tail run twice, stack off, throw).

- trace_run on Mark's PC (returns after cycle 7,043,000,000, translated vs interpreted): identical
  through fn 0x0C04E714's return to 0x0C04EA94 (r15 0x8C00F34C); then the interpreted run continues
  in fn 0x0C04EA4A (next call at 0x0C04EB14), the translated one returns from 0x0C04F7EA with r15
  0x8C00F2E8 called from 0x0C0912EC. That return is where run_guest resumes fn 0x0C04EA4A at
  0x0C04EA94, and the resume body's first statement is the interrupt poll, which set c.pc to the
  function's ENTRY before delivering. The handler switched tasks; the task came back through RTE to
  SPC = 0x0C04EA4A and ran the prologue again on its built frame: the later rts popped stack data.
- Patch 0010: a resumed body polls with c.pc = resume_pc. Test: resume tailpr_main at its call's
  return with an interrupt due; the poll must see 0x8C040006 and only the epilogue may run.
- (My mistake: trace_run had no end cycle, so the interpreted run wrote 1.9 GB until stopped; the
  file was cut to the window.)

- Boot 6 on Mark's PC (0008 + wider 0007): crash at the same pc as headless (0x8C754CEE, 36.8 s
  in the window), unmapped reads 0 (130M before). Character select: Sonic mostly right, one shoe
  stretched into a long grey spike. Crash: fn 0x0C04E714 runs with r15 = 0x8C00F36C and PR =
  0x8C754CEC (a value from the stack), during a resume chain after false non-local returns.
- Patch 0009 (DREAM_TRACE_RETURNS_FROM) and `build_pc.ps1 -Trace` / `trace_run.ps1`: a separate
  trace build on Mark's PC logs every return after a cycle count, translated and interpreted, to diff.

- Boot 5 on Mark's PC (patch 0007): same as before the break on screen, black after the Station
  Square title card; 0.6x in the window (was 0.3x), stop pc 0x0C037CFA, 130M unmapped reads. The
  0007 freeze only existed in the headless runs with MOVIE.BIN translated.
- Ruled out in turn: model data (ADV00 in RAM equals the file apart from 54 material words the game
  updates), the drawing code's translation (same fault with 0x0C030000-0x0C040000 interpreted, and
  with `--interpret`: identical frame 2132 and registers), interrupts and thread switches
  (`DREAM_TRACE_BANKS`, a local diagnostic: both banks survive every interrupt and switch intact).
- A local watchpoint in the interpreter (`DREAM_WATCH=pc:reg:below`, a ring of the last 4096
  instructions) at the mesh loop's `add #24,r2` (0x0C037858) caught it: the loop at 0x0C0376D0
  holds the meshset pointer in r2 on bank 0 (SR 0x40000000); the helper at 0x0C037EE0 clears RB on
  entry and its exit at 0x0C03828C sets RB, so on return the loop reads bank 1's r2 (0), adds 24 and
  draws "meshset" 0x18, whose garbage strips walk into unmapped 0x0040xxxx. All six helpers that
  build the RB mask (0x0C037EE4, 7FCE, 827C, 851A, 863A, 8804, 88BE, 8B14) clear on entry and set on
  exit: the renderer assumes RB = 1. SARecomp's captured main-loop state
  (src/postpal_bootstrap_state.hpp) has SR = 0x60000001. Our boot (bios.cpp, after Flycast's reios)
  starts 1ST_READ at SR 0x400000F1, and nothing in the game sets RB.
- Starting at SR 0x600000F1, fully interpreted: 45 guest s, 0 unmapped reads, 5,920 sectors,
  407 opaque polygons in the last frame (64 before). Patch 0008 adds `--boot-sr VALUE`;
  boot_test.ps1 and play.ps1 pass 0x600000F1.
- Translated, with the bank fix: a fault at 35.4 s, a return to 0xFFFFFFFF. RAM at 35.0 s matches
  the interpreted run except PR values (translated code writes the 0x0C link-space form) and timer
  counters. The chain started at a false non-local return from `jmp @r3` at 0x0C0A9842: computed
  tail jumps went through call_indirect without the 0007 handoff. 0007 now covers them too, and the
  handoff carries its target so a prologue only takes one meant for itself (a handoff landing in a
  runtime hook or the interpreter is dropped). p13_tailpr gained a `jmp @r2` case.

## 2026-10-28: the stall after translating MOVIE.BIN was a false non-local return (patch 0007)

- With MOVIE.BIN translated, every headless run stopped asking for disc data at exactly 761
  sectors. `DREAM_INTERP_RANGE` over the whole file: 5,644 sectors, so the cause was in its
  translations. Quarters, eighths, then `DREAM_INTERP_FUNCS` halving (tools in the scratch build)
  gave three functions, any one of which unstalls the run when interpreted: 0x0CECF91C (times a
  movie step with the timer at 0x8C60254E), 0x0CED38C4 and 0x0CEDA59E, all on one call path.
- RAM equals the file at the stall (63 words differ, all data), so not stale code.
  `DREAM_TRACE_NONLOCAL` showed about 147,000 cycles of: "nonlocal: rts from 0x0CED1206 to
  0x0CED0F52", then a resume of every frame of the chain, one at a time.
- Cause: discovery keeps shared epilogues as functions of their own (0x0CED1206..0x0CED1210 and
  0x0CED12D0..0x0CED12DA here). A `bra` into one is emitted as a direct tail call, and the
  callee took `entry_pr = c.pr`, which still held the last `jsr`'s return address. The epilogue
  restores the real PR and returns correctly, but the check called it non-local, threw, and
  run_guest rebuilt the chain. Results stay right (which is why the golden tests, run through
  run_guest, never caught it), but the movie player's task never got anywhere.
- Patch 0007: direct tail jumps (bra, bt/bf, bt/s, bf/s and falling into the next function) hand
  over the jumping function's entry PR through a thread-local that the next prologue takes
  (`take_entry_pr`). Differential test: tests/sh4/programs/p13_tailpr (hand-encoded, checked
  with `dream-translate disasm`), called directly so a throw fails it.
- `Apply-Patches` now also runs `git clean` in the submodule, since 0007 adds files.
- Seeds: 0x0C6446D4 (147,793 untranslated calls), 0x0C6446DA, 0x0C65C148.
- Headless rerun with 0007: 5,644 sectors (stall gone), 1.6x real time, non-local returns
  about 450,000 -> 33,990. It then reaches the model fault at pc 0x0C037CFA (unmapped reads).
- Model fault, first look: the drawing routine (fn 0x0C037CCC, store-queue vertex writer) gets a
  mesh pointer r2 = 0x18 (NULL plus an offset); its reads land in BIOS ROM at address 0, and the
  garbage there (0x00200106) becomes the vertex list, which then reads unmapped 0x0040xxxx.
  Interpreting 0x0C030000-0x0C040000 does not remove it (21M unmapped reads by 40 s), so the
  drawing code is translated correctly and the bad pointer comes from upstream data. `--replay`
  to 32 s: only fn 0x0C600502 and fn 0x0C600FC4 disagree (GD-ROM server, polls hardware).

## 2026-10-28: headless runs here; the movie player kept everything interpreted

- Built a headless replay build in the cloud (patch 0006: the launcher builds without the
  renderer) and ran with scripted Start presses. `--replay` agreed on 264M calls to the title; its
  large counts later (fn 0x0C040784 16,097, fn 0x0C057B00 1.6M) are harness artifacts: both
  functions recurse, so the interpreter pass stops at the first nested return to the same PR.
- `DREAM_STOP_ON_UNMAPPED=1` stopped at the first bad read (frame 2132, read16 0x00400020) with
  no emitted frames on the stack: the drawing code was running in the interpreter.
- First cause: the menus module ADVERTISE.PRS (header table at 0x8C900000 at 12 and 24 guest s)
  was untranslated. Added (354 functions): untranslated calls 31,930 -> 2,636, speed 0.4x -> 0.6x,
  but the same fault at the same frame.
- Real cause: DREAM_TRACE_NONLOCAL showed 517 resumes to untranslated code, all at 0x8CEB03BA,
  0x8CECFAA0 (169), 0x8CED9D26 ... A dump of 0x8CEB0000 matched SONICAD/MOVIE.BIN (code at offset
  0x3B0; RAM equals the file for its first 0x1B000 bytes at 34 s): the Sofdec movie player, loaded
  at 0x8CEB0000. Each resume of its task (return points after jsr in the file) left the
  interpreter running at top level. Added the whole file as an overlay (1,196 functions) plus six
  main-program entries the runs reached. Explains the slow black intro and why hiding the movie
  file did not skip it too.

## 2026-10-28: boot 4, the hang is model data, not missing code

- Boot 4 (closed by hand at 103.8 guest s, 0.3x): `--hide-file` active ("hiding 1 file"), intro
  still played black; Station Square title card, then black.
- ADV00 is not the problem: the build has all 516 functions, the stop dump equals ADV00.bin
  (0 of 256 halfwords differ), and a standalone test linking the ADV00 units with the runtime
  resolves 0x8C900000/20/A0/A6/63E with ADV00 in RAM. The repeated untranslated calls to
  0x8C9000A6 and 0x8C90063E (15 each) come from menus, when another module (likely ADVERTISE.PRS)
  sits at 0x8C900000: the game calls fixed entry points on whichever module is loaded.
- Every run stops in the same model-drawing routine, fn 0x0C037CDC (pc 0x0C037CFA), reading
  unmapped 0x0040xxxx with r4 = 0x00200058 / 0x00200280 (`mov.w @r4+` vertex reads). Those look like
  model-data offsets that were never turned into RAM pointers. Same likely cause as the wrong
  character models at character select. Next: find where the game fixes up model pointers after
  loading (and why that step is skipped or wrong here), and log GBR at the fault.
- Intro: renaming the directory record did not stop the movie. Next: check whether the game
  finds files through its own cached table rather than re-reading the directory.

## 2026-10-28: boot 3, calls into a function's middle (patches 0004, 0005)

- Boot 3 (120 guest s = 204 host s, 0.5x): Station Square title card, then black, though ADV00
  was translated (504 functions in the build log). The untranslated list was mostly resume misses
  at ordinary mid-function instructions (0x8C9000AA is `sts.l pr` inside a prologue).
- Cause: the main program calls Station Square at entry points discovery had inside other
  functions: 0x8C900020 (from 0x0C053C0A), 0x8C9000A6 (from 0x0C053C16, three instructions into
  fn 0x0C9000A0) and 0x8C90063E (from 0x0C0986F6). call_indirect found the containing function, its
  resume entry had no such block start, and resume_miss went to on_resume, which interprets until
  the next non-local return: effectively the rest of the run, so interrupts then landed at
  arbitrary pcs and every later resume missed too.
- Patch 0004: a miss reached from call_indirect is interpreted only until that call returns
  (on_untranslated), still recorded. Config: those three plus 0x62, 0x1440, 0xF620, 0x11F00 as
  ADV00 entries (now 516 functions), and the run's three main-image seeds.
- Patch 0005: `--hide-file NAME` bumps the last character of NAME's directory record in every
  sector read. boot_test and play hide SA1_600.SFD (62 MB, the intro) unless -KeepIntro. Mark
  pressed Start during the black intro and nothing seemed to skip; untested whether the game
  copes with the file missing.

## 2026-10-28: boot 2, Station Square is a PRS module

- Build 3 failed instantly: Apply-Patches checked 0001 alone after 0002 had edited a line 0001
  adds. Now the patch set is stamped and reapplied whole when it changes.
- Boot 2 (60 guest s = 126 host s, 0.4x): reached the Station Square title card, then black.
  The dump at 0x8C900000 began e6 2f d6 2f, not 2_DP.BIN's nop padding: the overlay check had
  rightly refused 2_DP.BIN. Searching every file, raw and PRS-decompressed, then comparing the
  full 512-byte dump: ADV00.PRS decompressed matches 256 of 256 halfwords (others at most 48).
- 37 PRS files are code modules linked at 0x8C900000 (self-pointer and rts counts): ADV00-03,
  STG00-12, the bosses, AL_MAIN, MINICART, SHOOTING, SBOARD, SUMMARY and more. Their code sits at
  the start, data after (ADV00: dense code to about 0x13000 of 0x23871B).
- Config: dropped 2_DP.BIN; added ADV00 (first 0x14000 bytes) as an overlay. Translates to 504
  functions, 30,048 instructions, 0 not lowered. Other modules get added as play reaches them.

## 2026-10-28: build 2 failed on a duplicate label (patch 0003)

- MSVC C2045 "L_0c9ca5ba: label redefined" in sonicadv_reloc_8c900000_part3.cpp, function
  fn_0c9c5678. 0x0c9ca5ba is the `nop` delay slot of `bra 0xc9ca892` at 0x0c9ca5b8, and it is both
  a branch target (a slot label) and a block start (in the resume switch), so the emitter wrote
  the slot copy and the block under one label.
- Fix: when the slot is already a block, skip the copy; the owning branch's not-taken path jumps
  to pc+4 (made a block by the existing after-slot rule), so the slot runs once. Retranslated all
  47 units: zero duplicate labels; the failing part compiles with clang. Upstream rule says every
  emitter change gets a differential test: still owed (needs an SH-4 test program for
  "bt/s whose slot is also a branch target").

## 2026-10-28: first PC build and boot (patch 0002)

- First build: 4 min 37 s on Mark's PC, 3 jobs, Vulkan renderer and SDL3 audio on. Before it,
  `build_pc.ps1` had picked devkitPro's MSYS2 cmake (`/c/SA-nx/C:/SA-nx`); it now uses VS's.
- Boot 1 (60 guest s = 143 host s, 0.4x): no-save prompt, SEGA logo, intro movie black (2,419,712
  YUV words went to the PVR, nothing shown: the YUV path is not drawn), title, Adventure, character
  select (models visibly wrong), picked Sonic, then ran into untranslated code until the limit.
- Interrupts: 14,903 delivered, every one to VBR+0x600 (0x8C000600) through the interpreter. The
  startup copies two 0x34-byte stubs from 0x0C65E6E4 (EXPEVT, to +0x100 and +0x400) and
  0x0C65E718 (INTEVT, to +0x600); each patches its own pool word, so the 0x600 copy never matched
  the image. Added all three as no_fold relocations.
- 2_DP.BIN: untranslated returns at 0x8C900096 and 0x8C9000A0 sit right after jsr instructions in
  the file. Added `file = ` to `[[relocations]]` (translator + DreamAddGame, patch 0002) and
  configured 2_DP.BIN at 0x8C900000 as an overlay, split in 8 parts: 10,455 functions, 866,859
  instructions, 10 not lowered. boot_test now dumps 0x8C900000 at the stop to confirm.
- Also merged the run's 5 seeds (0x0C0C5C7E was called 650 times) and 4 exact-copy relocations.
- Still open: no sound (AICA mixer: 0 key-ons; the driver gets no commands), movie not drawn,
  model rendering, 0x00400020 reads (unmapped) at pc 0x0C037CFA from frame 2775.

## 2026-10-28: translator fixes for a large binary (patch 0001)

- `dream-translate game` on Sonic's 1ST_READ.BIN never finished (killed after 40 min). gdb samples
  all landed in `FunctionEmitter::scan_stores` on one 50-byte function (0x0c2842d0), with
  `Image::contains` called on climbing addresses. Cause: the loop marking written literal words
  used a 32-bit counter; a store whose address normalises into the top bytes of the address space
  wrapped the counter past zero and walked all 4 GiB (and marked every image word written, which
  silently disabled literal folding). Fix: 64-bit bounds. Translation now takes 11 s.
- Output: 53,149 functions, 1,058,847 instructions, 35,055 not lowered, 122,522 FP runtime
  branches, 74 switch tables, 158 MB of C++ in one file. Coverage 66.7% of the bytes it scanned.
- Added `--parts N` (CMake `DREAM_TRANSLATE_PARTS`, 32 here): bodies split evenly (about 10 MB
  each), resume entries non-static when split so the one table in part 0 links. One part compiles
  in 2 min 36 s with clang -O1 on one core; resume symbols resolve across parts. MSVC units use
  `/O1 /bigobj`.
- Both live in `patches/dream-recomp/0001-large-binaries-wrap-and-split.patch`, applied by
  `build_pc.ps1`. The wrap fix is worth offering upstream.

## 2026-10-28: kickoff

- Route chosen: dream-recomp (GPL-2.0, ARM64 parity by design, Vulkan renderer). SARecomp could
  not be a base: its repo holds only the Sonic adapter; the KatanaRecomp SDK and generated code it
  links are private (`CMakeLists.txt` refuses to configure without `.local/baseline/r354`), and
  the KatanaRecomp repo now returns 404. SARecomp's own notes say development was discontinued.
- Disc: PAL v1.003 GDI, all three tracks match SARecomp's published SHA-256s; IP.BIN reads
  "SONIC ADVENTURE, MK-5100050 V1.003 (19990909)", CRC ok, not Windows CE.
- Extracted 2699 files (993 MB) with `tools/extract_resumable.py` (dcdisc in passes; one
  `new-game` run cannot finish inside the bridge's 3-minute call limit). Config written by
  `dcdisc new-game` with defaults; no seeds yet.
- Code files found by `dcdisc scan`: 1ST_READ.BIN (6.7 MB), 2_DP.BIN (2.8 MB, SH-4, needs a
  relocation entry), MAIGO.BIN and SG_DPLDR.BIN (14.8 KB each).
- dream-recomp pinned at 51a148c (2026-09-21).
