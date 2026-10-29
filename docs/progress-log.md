# Progress log

Newest first. One entry per session: what broke, the cause, the fix, with evidence.

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
- Pushed for a PC build before the headless rebuild here finished; the 761-sector stall is not
  yet confirmed gone.

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
