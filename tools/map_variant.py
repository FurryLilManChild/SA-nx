#!/usr/bin/env python3
"""Map Sonic Adventure addresses from one disc version to another (PAL v1.003 to US v1.005).

The two builds are the same program compiled a little apart: most of the US main program is the
PAL code 192 bytes further on, with a few small changes in between, and every code module and
cutscene file is the same code shifted by its own small amounts. So an address found for PAL (a
seed, a module entry, a copy source, an address in boot_main) maps to the US build by finding the
same instruction bytes nearby.

For an address A in an image (main program, a module, MOVIE.BIN, a cutscene file):
  1. take the 16-byte windows starting at A, A+2, ... A+62 in the PAL image;
  2. look each up in an index of every 16-byte window of the US image;
  3. keep the candidates whose shift (US offset - PAL offset) is closest to the shift of the
     previous mapped address in that image (code moves in long runs);
  4. accept the shift only if the bytes at A itself (32 bytes, or fewer near the end) are equal in
     both images at that shift. Otherwise the address is reported as unmapped, never guessed.

Literal pools hold absolute addresses that differ between the builds, so a window that crosses
one does not match; the walk in step 1 steps past it.

  python3 tools/map_variant.py games/sonicadv/sonicadv.toml games/sonicadv/sonicadv_us.toml \\
      --to-root games/sonicadv/extracted_us --report out/map_us.txt

Writes the US config: the PAL config with every mapped address replaced and the file paths
pointed at the US extraction. Unmapped addresses are left out of lists (and named in the report)
or, where a single value is required, keep the PAL value with a "# UNMAPPED" note.
"""
import argparse
import collections
import json
import os
import re
import sys

import numpy as np

W = 16          # window bytes
WALK = 64       # how far past the address windows are tried
VERIFY = 32     # bytes that must be equal at the address itself


class Image:
    def __init__(self, pal: bytes, us: bytes, base: int, name: str):
        self.pal, self.us, self.base, self.name = pal, us, base, name
        # Every even 16-byte window of the US image as one 64-bit key, sorted: a compact index
        # (a dict of byte strings ran out of memory on the 6.7 MB main program).
        self.keys, self.pos = _index(us)
        self.last_shift = 0
        self.weak = []
        self.header = []

    def lookup(self, window: bytes):
        k = _key(window)
        lo = np.searchsorted(self.keys, k, side="left")
        hi = np.searchsorted(self.keys, k, side="right")
        out = []
        for i in range(lo, min(hi, lo + 65)):
            p = int(self.pos[i])
            if self.us[p:p + W] == window:
                out.append(p)
        return out

    def map_offset(self, off: int):
        best = None
        for k in range(0, WALK, 2):
            o = off + k
            if o + W > len(self.pal):
                break
            cands = self.lookup(self.pal[o:o + W])
            if not cands or len(cands) > 64:
                continue
            for c in cands:
                s = c - o
                if best is None or abs(s - self.last_shift) < abs(best - self.last_shift):
                    best = s
            if best is not None and abs(best - self.last_shift) <= 0x400:
                break
        if best is None:
            return None
        n = min(VERIFY, len(self.pal) - off, len(self.us) - (off + best))
        if n < 2 or off + best < 0:
            return None
        if self.pal[off:off + n] != self.us[off + best:off + best + n]:
            return None
        self.last_shift = best
        return off + best

    def map(self, addr: int):
        """addr in any of the SH-4 views (0x0C.., 0x8C.., 0xAC..); same view back."""
        view = addr & 0xE0000000
        phys = addr & 0x1FFFFFFF
        base = self.base & 0x1FFFFFFF
        off = phys - base
        if off < 0 or off >= len(self.pal):
            return None
        # A module's header is a table of its entry functions: the US header names the same
        # entry in the same slot (ADVERTISE: 8C9000E8 8C9001A0 8C9001BA -> F4 1C0 1DA).
        for i in range(0, min(64, len(self.pal), len(self.us)) - 3, 4):
            w = int.from_bytes(self.pal[i:i + 4], "little")
            if w and (w & 0x1FFFFFFF) == phys:
                u = int.from_bytes(self.us[i:i + 4], "little")
                if u >> 24 == w >> 24:
                    self.header.append(addr)
                    return view | (u & 0x1FFFFFFF)
        m = self.map_offset(off)
        if m is None:
            # Next to a literal pool the windows and the 32-byte check fail. Take the shift of
            # the previous address mapped in this image if the instruction bytes at the address
            # still agree for 4 or more bytes; such matches are listed as weak in the report.
            s = self.last_shift
            u = off + s
            n = 0
            while (off + n < len(self.pal) and 0 <= u + n < len(self.us)
                   and self.pal[off + n] == self.us[u + n] and n < VERIFY):
                n += 1
            if n >= 4:
                self.weak.append((addr, "neighbour shift %+d, %d bytes equal" % (s, n)))
                m = u
        return None if m is None else view | (base + m)


def _mix(a, b):
    return (a * np.uint64(0x9E3779B97F4A7C15)) ^ b


def _index(data: bytes):
    n = (len(data) - W) // 2 + 1
    if n <= 0:
        return np.zeros(0, np.uint64), np.zeros(0, np.int64)
    buf = np.frombuffer(data, dtype=np.uint8)
    pos = np.arange(n, dtype=np.int64) * 2
    a = np.zeros(n, np.uint64)
    b = np.zeros(n, np.uint64)
    for j in range(8):
        a |= buf[pos + j].astype(np.uint64) << np.uint64(8 * j)
        b |= buf[pos + 8 + j].astype(np.uint64) << np.uint64(8 * j)
    with np.errstate(over="ignore"):
        keys = _mix(a, b)
    order = np.argsort(keys, kind="stable")
    return keys[order], pos[order]


def _key(window: bytes):
    a = np.uint64(int.from_bytes(window[:8], "little"))
    b = np.uint64(int.from_bytes(window[8:16], "little"))
    with np.errstate(over="ignore"):
        return _mix(a, b)


HEX = re.compile(r"0x[0-9A-Fa-f]{5,8}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pal_toml")
    ap.add_argument("us_toml")
    ap.add_argument("--to-root", required=True, help="US extraction, as fs/ modules/ events/")
    ap.add_argument("--report", required=True)
    ap.add_argument("--pal-gen", help="the PAL translation's gen/ folder: every function the PAL"
                    " translation found is carried over as a seed (test 2.2 showed the US"
                    " discovery missing module functions the PAL runs had found)")
    ap.add_argument("--us-gen", help="the US translation's gen/ folder: seeds it already finds"
                    " are left out, keeping the config small")
    a = ap.parse_args()

    def entries_of(gen, stem):
        f = os.path.join(gen, stem + ".functions.json") if gen else None
        if not f or not os.path.exists(f):
            return set()
        return {int(x["entry"], 16) for x in json.load(open(f))["functions"]}

    dest_count = collections.Counter()
    seeded = 0

    pal_dir = os.path.dirname(os.path.abspath(a.pal_toml))
    us_root = os.path.abspath(a.to_root)
    us_rel = os.path.relpath(us_root, pal_dir)
    text = open(a.pal_toml).read()

    def us_path(pal_rel: str) -> str:
        p = pal_rel.replace("extracted/", "", 1)
        p = p.replace("fs/SONICAD/", "fs/SONICADV/").replace("fs/1ST_READ.BIN", "1ST_READ.plain.bin")
        return os.path.join(us_rel, p)

    images = {}

    def image_for(pal_rel: str, base: int) -> Image:
        key = (pal_rel, base)
        if key not in images:
            pal = open(os.path.join(pal_dir, pal_rel), "rb").read()
            us = open(os.path.join(pal_dir, us_path(pal_rel)), "rb").read()
            images[key] = Image(pal, us, base, pal_rel)
        return images[key]

    main_img = image_for("extracted/fs/1ST_READ.BIN", 0x0C010000)
    report = []
    mapped = unmapped = 0

    # Split into blocks at each table header so a module's addresses use the module's image.
    parts = re.split(r"(?m)^(?=\[)", text)
    out = []
    for part in parts:
        img = main_img
        fm = re.search(r'(?m)^file = "([^"]+)"', part)
        dm = re.search(r"(?m)^dest = (0x[0-9A-Fa-f]+)", part)
        if fm and dm:
            img = image_for(fm.group(1), int(dm.group(1), 16))
            part = part.replace(f'file = "{fm.group(1)}"', f'file = "{us_path(fm.group(1))}"')
        new_lines = []
        size_fix = None
        # Seeds from the PAL translation for this unit (main program or a file relocation).
        seeds = []
        if a.pal_gen and (dm or part.startswith("[functions]")):
            if dm:
                d = int(dm.group(1), 16)
                dest_count[d] += 1
                n = dest_count[d]
                pal_stem = f"sonicadv_reloc_{d:08x}" + ("" if n == 1 else f"_{n}")
                us_stem = f"sonicadv_us_reloc_{d:08x}" + ("" if n == 1 else f"_{n}")
            else:
                pal_stem, us_stem = "sonicadv", "sonicadv_us"
            if fm or not dm:
                have = entries_of(a.us_gen, us_stem)
                have_phys = {x & 0x1FFFFFFF for x in have}
                saved = img.last_shift
                for e in sorted(entries_of(a.pal_gen, pal_stem)):
                    nweak = len(img.weak)
                    r = img.map(e)
                    if len(img.weak) != nweak:
                        img.weak.pop()  # only strong matches become seeds
                        continue
                    if r is not None and (r & 0x1FFFFFFF) not in have_phys:
                        seeds.append(r)
                img.last_shift = saved
                seeded += len(seeds)
        if fm and dm:
            # The US file's code region: the PAL size grown by however much longer the US file
            # is, within the file (cutscene files and modules move by a few bytes to a few KB).
            sm = re.search(r"(?m)^size = (0x[0-9A-Fa-f]+)", part)
            if sm:
                grow = max(0, len(img.us) - len(img.pal))
                size_fix = (sm.group(0), "size = 0x%X" % min(len(img.us),
                            (int(sm.group(1), 16) + grow + 3) & ~3))
        for line in part.split("\n"):
            code, sep, comment = line.partition("#")
            key = code.split("=")[0].strip() if "=" in code else None
            if key in ("dest", "load_address", "link_address", "size") or code.strip().startswith(
                    ("load_address", "link_address")):
                new_lines.append(line)  # layout, not code addresses
                continue
            if key == "path":
                m = re.search(r'"([^"]+)"', code)
                if m:
                    line = line.replace(m.group(1), us_path(m.group(1)))
                new_lines.append(line)
                continue
            if key == "image":
                new_lines.append('image = "../../assets/Sonic Adventure (USA) (En,Ja,Fr,De,Es)/'
                                 'Sonic Adventure (USA) (En,Ja,Fr,De,Es).gdi"')
                continue
            if key == "play_modules":
                # Instruction words, not addresses: each is a gameplay module's first word, so
                # the US list is the first word of the US module with the same name.
                words = []
                for mo in HEX.finditer(code):
                    w = int(mo.group(0), 16)
                    hit = None
                    for name in sorted(os.listdir(os.path.join(pal_dir, "extracted/modules"))):
                        pm = open(os.path.join(pal_dir, "extracted/modules", name), "rb").read(4)
                        if int.from_bytes(pm, "little") == w:
                            um = os.path.join(pal_dir, us_rel, "modules", name)
                            if os.path.exists(um):
                                hit = int.from_bytes(open(um, "rb").read(4), "little")
                                break
                    if hit is None:
                        report.append(f"play_modules: 0x{w:08X} kept (no PAL module starts so)")
                        hit = w
                    words.append(f"0x{hit:08X}")
                words = sorted(set(words))
                new_lines.append('play_modules = "' + ",".join(words) + '"')
                continue
            in_list = "[" in code or code.strip().startswith("0x") or key == "entries"
            src_img = main_img if key == "source" and not fm else img

            def sub(mo):
                nonlocal mapped, unmapped
                v = int(mo.group(0), 16)
                if fm and key == "source":
                    return mo.group(0)  # offset into the file, not an address
                r = src_img.map(v)
                if r is None and src_img is main_img and key == "charsel_words":
                    # Past the end of the boot program (zeroed data the program sets up): moved
                    # by the shift at the program's end. A guess, confirmed or not by the US
                    # run's "screen words" log lines.
                    end = main_img.base + len(main_img.pal)
                    if (v & 0x1FFFFFFF) >= (end & 0x1FFFFFFF):
                        shift = main_img.last_shift
                        if shift:
                            r = v + shift
                            report.append(f"GUESS charsel {mo.group(0)} -> 0x{r:08X} (end shift {shift:+d})")
                if r is None:
                    if (v & 0x1FFFFFFF) < 0x0C000000:
                        return mo.group(0)
                    unmapped += 1
                    report.append(f"UNMAPPED {src_img.name}: {mo.group(0)}  ({line.strip()[:80]})")
                    return mo.group(0) + "_UNMAPPED"
                mapped += 1
                if r != v:
                    report.append(f"{src_img.name}: {mo.group(0)} -> 0x{r:08X}")
                return f"0x{r:08X}" if mo.group(0)[2:].isupper() or True else hex(r)

            new_code = HEX.sub(sub, code)
            if "_UNMAPPED" in new_code:
                if in_list:
                    # drop unmapped list items; keep the rest of the line
                    new_code = re.sub(r"0x[0-9A-Fa-f]+_UNMAPPED,?\s*", "", new_code)
                    comment = (comment + " UNMAPPED item(s) dropped").strip()
                    sep = "#"
                else:
                    new_code = new_code.replace("_UNMAPPED", "")
                    comment = (comment + " UNMAPPED: PAL value kept").strip()
                    sep = "#"
            if sep and not new_code.strip() and comment == line.partition("#")[2]:
                new_lines.append(line)  # a comment line
            elif sep:
                new_code = new_code.rstrip() + "  " if comment and not code.endswith(" ") else new_code
                new_lines.append(new_code + "#" + ("" if comment.startswith(" ") else " ") + comment)
            else:
                new_lines.append(new_code)
        block = "\n".join(new_lines)
        if seeds:
            seed_text = ", ".join(f"0x{x:08X}" for x in seeds)
            key = "entries" if dm else "extra"
            m = re.search(rf"(?m)^{key} = \[", block)
            note = (f"\n# {len(seeds)} seed(s) carried over from functions the PAL translation"
                    f" found (tools/map_variant.py --pal-gen)\n")
            if m:
                block = block[:m.end()] + seed_text + ",\n    " + block[m.end():]
            else:
                block = block.rstrip("\n") + note + f"{key} = [{seed_text}]\n\n"
        if size_fix:
            block = block.replace(size_fix[0], size_fix[1], 1)
        out.append(block)
    us_text = "".join(out)
    us_text = us_text.replace('id = "sonicadv"', 'id = "sonicadv_us"', 1)
    us_text = re.sub(r'(?m)^region = "Europe"', 'region = "USA"', us_text)
    us_text = re.sub(r'(?m)^product = "MK-5100050"', 'product = "MK-51000"', us_text)
    us_text = re.sub(r"(?m)^# Sonic Adventure, MK-5100050",
                     "# Sonic Adventure (USA v1.005, MK-51000): generated from sonicadv.toml by\n"
                     "# tools/map_variant.py. Do not edit by hand; edit the PAL config and rerun.",
                     us_text)
    open(a.us_toml, "w").write(us_text)
    os.makedirs(os.path.dirname(os.path.abspath(a.report)), exist_ok=True)
    weak = [(img.name, ad, why) for img in images.values() for ad, why in img.weak]
    with open(a.report, "w") as f:
        hdr = sum(len(img.header) for img in images.values())
        f.write(f"mapped {mapped} ({hdr} from module/cutscene header tables, {len(weak)} weak),"
                f" unmapped {unmapped}; {seeded} seeds from the PAL translation\n")
        for name, ad, why in weak:
            f.write(f"WEAK {name}: 0x{ad:08X} ({why})\n")
        f.write("\n".join(report) + "\n")
    print(f"mapped {mapped}, unmapped {unmapped}; report in {a.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
