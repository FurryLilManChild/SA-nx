#!/usr/bin/env python3
"""Decompress Sonic Adventure's PRS code modules for the translator.

Areas, stages, bosses, the Chao garden and the minigames are PRS-compressed modules that the game
loads to 0x8C900000 (ADV00.PRS = Station Square, confirmed byte for byte against a RAM dump on
2026-10-28). The translator reads plain bytes, so this writes each module decompressed to
games/<id>/extracted/modules/<NAME>.bin (gitignored, like the rest of extracted/).

  python3 tools/prs_extract.py games/sonicadv/extracted/fs/SONICAD games/sonicadv/extracted/modules
  python3 tools/prs_extract.py --events games/sonicadv/extracted/fs/SONICAD games/sonicadv/extracted/events

--events writes the story cutscene files (EV*.PRS) that carry code: they load at 0x8CB80000 and
their first word points at their entry function there (207 of them; EV_*.PRS are textures).

Only files whose literal pools point back into themselves at 0x8C900000 are written (code modules).
"""
import os
import struct
import sys


def prs_decompress(src: bytes) -> bytes:
    """Sega PRS (LZ77 with a bit-stream of control flags), as used across Sonic Team titles."""
    out = bytearray()
    i = 0
    bits = 0
    nbits = 0

    def bit() -> int:
        nonlocal i, bits, nbits
        if nbits == 0:
            bits = src[i]
            i += 1
            nbits = 8
        b = bits & 1
        bits >>= 1
        nbits -= 1
        return b

    while True:
        if bit():  # literal byte
            out.append(src[i])
            i += 1
            continue
        if bit():  # long copy: 13-bit offset, 3-bit length (0 = length in the next byte)
            v = src[i] | (src[i + 1] << 8)
            i += 2
            if v == 0:
                break
            off = (v >> 3) - 0x2000
            n = v & 7
            if n == 0:
                n = src[i] + 1
                i += 1
            else:
                n += 2
        else:  # short copy: 2-bit length, 8-bit offset
            n = ((bit() << 1) | bit()) + 2
            off = src[i] - 0x100
            i += 1
        for _ in range(n):
            out.append(out[off])
    return bytes(out)


def is_code_module(data: bytes, base: int = 0x0C900000) -> bool:
    n = len(data)
    if n < 4096:
        return False
    self_ptrs = 0
    for k in range(0, n - 3, 4):
        v = struct.unpack_from("<I", data, k)[0] & 0x1FFFFFFF
        if base <= v < base + n:
            self_ptrs += 1
    rts = sum(1 for k in range(0, min(n, 65536) - 1, 2) if data[k] == 0x0B and data[k + 1] == 0)
    return self_ptrs > 200 and rts > 20


def is_event_code(data: bytes) -> bool:
    return len(data) >= 8 and (struct.unpack_from("<I", data, 0)[0] >> 16) == 0x0CB8


def main() -> int:
    args = sys.argv[1:]
    events = "--events" in args
    args = [a for a in args if a != "--events"]
    src_dir, out_dir = args[0], args[1]
    os.makedirs(out_dir, exist_ok=True)
    wrote = 0
    for name in sorted(os.listdir(src_dir)):
        if not name.upper().endswith(".PRS"):
            continue
        if events != name.upper().startswith("EV"):
            continue
        dest = os.path.join(out_dir, os.path.splitext(name)[0] + ".bin")
        if os.path.exists(dest):
            wrote += 1
            continue
        try:
            data = prs_decompress(open(os.path.join(src_dir, name), "rb").read())
        except IndexError:
            continue  # not PRS after all
        if not (is_event_code(data) if events else is_code_module(data)):
            continue
        with open(dest, "wb") as f:  # overwrite in place; never delete
            f.write(data)
        wrote += 1
        print(f"{name}: {len(data)} bytes -> {dest}")
    print(f"{wrote} code {'events' if events else 'modules'} in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
