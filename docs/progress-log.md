# Progress log

Newest first. One entry per session: what broke, the cause, the fix, with evidence.

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
