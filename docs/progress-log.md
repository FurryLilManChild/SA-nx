# Progress log

Newest first. One entry per session: what broke, the cause, the fix, with evidence.

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
