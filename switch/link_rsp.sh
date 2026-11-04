#!/usr/bin/env bash
# Linker launcher for the Switch build (CMAKE_CXX_LINKER_LAUNCHER, set by switch/build.sh).
# On a cloud machine devkitPro is the Windows build under Wine, and a Windows command line stops
# at 32,767 characters. The game links over 500 objects (the cutscene units brought the command
# to 42,979 characters), and the link then failed with no message at all. This moves the object
# files into a response file, in their original order, and runs the real command.
set -euo pipefail
rsp="$(mktemp --suffix=.rsp -p "$PWD" link_XXXXXX)"
trap 'rm -f "$rsp"' EXIT
args=()
placed=0
for a in "$@"; do
    if [[ "$a" == *.o || "$a" == *.obj ]]; then
        printf '%s\n' "$a" >> "$rsp"
        if [[ $placed == 0 ]]; then args+=("@$(basename "$rsp")"); placed=1; fi
    else
        args+=("$a")
    fi
done
"${args[@]}"
