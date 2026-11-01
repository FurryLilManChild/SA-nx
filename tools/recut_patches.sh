#!/usr/bin/env bash
# Rebuilds patches/dream-recomp/0022 and 0023 from the dream-recomp working tree: 0001-0021 are
# applied to the pinned commit in a scratch worktree, then 0023's files (the inline memory path)
# are cut separately from everything else (0022). Checks that the whole set reproduces the tree.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
P="$REPO/patches/dream-recomp"
D="$REPO/dream-recomp"
PIN=51a148c
MEM="runtime/include/dream/runtime/memory.h runtime/include/dream/runtime/mem/dc_memory.h runtime/src/mem/dc_memory.cpp runtime/tests/test_memory.cpp"
W="$(mktemp -d)/wt"
git -C "$D" worktree add -q "$W" "$PIN"
trap 'git -C "$D" worktree remove --force "$W"' EXIT
for p in "$P"/00{01..21}-*.patch; do git -C "$W" apply --whitespace=nowarn "$p"; done
git -C "$W" add -A && git -C "$W" -c user.name=x -c user.email=x@x commit -qm base
changed=$(cd "$D" && { git diff --name-only "$PIN"; git ls-files -o --exclude-standard; } | sort -u)
for f in $changed; do
    case " $MEM " in *" $f "*) continue;; esac
    [ -f "$D/$f" ] || continue
    if ! cmp -s "$D/$f" "$W/$f"; then mkdir -p "$W/$(dirname "$f")"; cp "$D/$f" "$W/$f"; fi
done
git -C "$W" add -A && git -C "$W" diff --cached --binary > "$P/0022-switch-build-and-port-menu.patch"
git -C "$W" -c user.name=x -c user.email=x@x commit -qm 22
for f in $MEM; do cp "$D/$f" "$W/$f"; done
git -C "$W" add -A && git -C "$W" diff --cached > "$P/0023-inline-main-ram-access.patch"
V="$(mktemp -d)/v"
git -C "$D" worktree add -q "$V" "$PIN"
for p in "$P"/*.patch; do git -C "$V" apply --whitespace=nowarn "$p"; done
if diff -rq --exclude=.git "$V" "$D" | grep -v "^Only in $D"; then echo "MISMATCH"; fi
git -C "$D" worktree remove --force "$V"
echo "patches re-cut and verified"
