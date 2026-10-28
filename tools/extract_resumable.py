#!/usr/bin/env python3
"""Resumable disc extraction for dream-recomp's dcdisc.

`dcdisc new-game` extracts the whole disc in one go (1.2 GB for Sonic Adventure). Where a shell
call has a time limit, that never finishes. This does the same extraction in passes: files already
written at the right size are skipped, and a pass stops after --budget seconds with exit code 3
("run me again"). When everything is written it writes IP.BIN, the plain boot binary, and then
asks dcdisc for the game's TOML and CMakeLists.txt (without extracting a second time).

  python3 tools/extract_resumable.py DISC.gdi games sonicadv --title "Sonic Adventure"

Needs PYTHONPATH pointing at dream-recomp/tools/dcdisc.
"""
import argparse
import os
import sys
import time

from dcdisc.image import open_image
from dcdisc.ipbin import parse_ipbin, read_ipbin
from dcdisc.iso9660 import Iso9660
from dcdisc.scramble import ensure_plain


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("image")
    p.add_argument("games_dir")
    p.add_argument("game_id")
    p.add_argument("--title", default=None)
    p.add_argument("--budget", type=float, default=150.0, help="seconds per pass")
    a = p.parse_args()

    start = time.monotonic()
    out_dir = os.path.join(os.path.abspath(a.games_dir), a.game_id)
    extracted = os.path.join(out_dir, "extracted")
    fs_dir = os.path.join(extracted, "fs")

    with open_image(a.image) as img:
        ip_raw = read_ipbin(img)
        ip = parse_ipbin(ip_raw)
        fs = Iso9660(img)
        files = [e for e in fs.walk() if not e.is_dir]
        total = len(files)
        done = written = 0
        boot = None
        for e in files:
            dest = os.path.join(fs_dir, e.path.lstrip("/"))
            is_boot = e.name.upper() == ip.boot_filename.upper()
            if os.path.isfile(dest) and os.path.getsize(dest) == e.size and not is_boot:
                done += 1
                continue
            if time.monotonic() - start > a.budget:
                print(f"pass stopped: {done} of {total} files written, run again", flush=True)
                return 3
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            data = fs.read(e)
            with open(dest, "wb") as f:  # overwrite in place: deleting is not allowed there
                f.write(data)
            if is_boot:
                boot = data
            done += 1
            written += 1
        print(f"{done} of {total} files present ({written} written this pass)", flush=True)

        with open(os.path.join(extracted, "IP.BIN"), "wb") as f:
            f.write(ip_raw)
        if boot is None:
            print(f"error: boot file {ip.boot_filename} not found", file=sys.stderr)
            return 1
        plain, was_scrambled = ensure_plain(boot)
        plain_path = os.path.join(extracted, os.path.splitext(ip.boot_filename)[0] + ".plain.bin")
        with open(plain_path, "wb") as f:
            f.write(plain)
        print(f"boot binary: {'descrambled' if was_scrambled else 'stored plain'} -> {plain_path}")

    from dcdisc.project import cmd_new_game

    class _A:
        image = a.image
        dir = a.games_dir
        id = a.game_id
        title = a.title
        no_extract = True
        force = True
        brief = True
        chdman = False

    return cmd_new_game(_A())


if __name__ == "__main__":
    sys.exit(main())
