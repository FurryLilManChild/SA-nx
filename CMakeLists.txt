# SA-nx: Sonic Adventure (Dreamcast, PAL v1.003) through dream-recomp, PC first, then Switch.
# dream-recomp is consumed as a submodule; it skips its own reference titles when it is not the
# top-level project, so the only game target here is ours.
cmake_minimum_required(VERSION 3.24)
project(SA_nx LANGUAGES C CXX)

add_subdirectory(dream-recomp)
add_subdirectory(games/sonicadv)
