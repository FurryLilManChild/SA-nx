#!/usr/bin/env bash
# Linker launcher for the Switch build (CMAKE_CXX_LINKER_LAUNCHER, set by switch/build.sh).
# On a cloud machine devkitPro is the Windows build under Wine, and a Windows command line stops
# at 32,767 characters. The game links over 500 objects (the cutscene units brought the command
# to 42,979 characters), and the link then failed with no message at all. This moves the object
# files into a response file, in their original order, and runs the real command.
#
# Test 2.2: with the US version the generated code's debug information took the ELF past what
# devkitPro's linker can write under Wine (a 4 GB file, then a 512 MB compressed one, both with an
# empty header). Generated units are linked from copies without debug information
# (devkitPro's objcopy --strip-debug, kept next to the objects and redone when an object is newer;
# llvm-objcopy's copies broke three US units' local relocations); their
# function symbols stay, which is what crash offsets need. Hand-written code keeps full debug info.
set -euo pipefail
STRIP_TOOL="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-objcopy"
[[ -x "$STRIP_TOOL" ]] || STRIP_TOOL=""
rsp="$(mktemp --suffix=.rsp -p "$PWD" link_XXXXXX)"
trap 'rm -f "$rsp"' EXIT
args=()
placed=0
for a in "$@"; do
    if [[ "$a" == *.o || "$a" == *.obj ]]; then
        if [[ -n "$STRIP_TOOL" && "$a" == */gen/* ]]; then
            s="${a%/*}/nodebug/${a##*/}"
            if [[ ! -f "$s" || "$a" -nt "$s" ]]; then
                mkdir -p "${s%/*}"
                "$STRIP_TOOL" --strip-debug "$a" "$s"
            fi
            a="$s"
        fi
        printf '%s\n' "$a" >> "$rsp"
        if [[ $placed == 0 ]]; then args+=("@$(basename "$rsp")"); placed=1; fi
    else
        args+=("$a")
    fi
done
"${args[@]}"
