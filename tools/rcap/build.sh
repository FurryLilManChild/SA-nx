#!/bin/bash
# Builds rcap, which re-renders an F11 capture (capture-NNN.ta/.vram/.vram.regs) through
# dream-recomp's real Vulkan renderer with no window, so renderer changes can be checked against
# captures before a PC build. Linux; needs glslc, libvulkan-dev and a Vulkan driver (lavapipe:
# mesa-vulkan-drivers is enough).
#   tools/rcap/build.sh BUILD_DIR      BUILD_DIR: a dream-recomp CMake build with libdream_render.a
#                                      and libdream_runtime.a (patches applied to the submodule)
#   tools/rcap/out/rcap capture-013 out.ppm     DREAM_NO_OIT=1 for the per-strip sort,
#                                               RCAP_VALIDATE=1 for the validation layer
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd); R=$ROOT/dream-recomp/render; B=${1:?build dir}
O=$ROOT/tools/rcap/out; mkdir -p $O/spv
for sh in $(ls $R/shaders | grep -v 'glsl$'); do
  sym=$(echo $sh | tr . _)
  glslc --target-env=vulkan1.1 -O -mfmt=c -o $O/spv/$sym.inc $R/shaders/$sh
  printf '#pragma once\n#include <cstdint>\nstatic const std::uint32_t %s[] =\n#include "%s.inc"\n;\n' $sym $sym > $O/spv/$sym.h
done
for f in context resources renderer texture_cache offscreen; do
  g++ -std=c++20 -O1 -c -I$R/include -I$R/../runtime/include -I$O/spv $R/src/vk/$f.cpp -o $O/$f.o
done
g++ -std=c++20 -O1 -I$R/include -I$R/../runtime/include $ROOT/tools/rcap/rcap.cpp $O/*.o \
  $B/render/libdream_render.a $B/runtime/libdream_runtime.a -lvulkan -o $O/rcap
echo "built $O/rcap"
