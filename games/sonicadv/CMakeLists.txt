# Sonic Adventure. Everything is in cmake/DreamAddGame.cmake; this file exists so that adding a game is
# adding a directory. Add BOOT_TEST once the title boots far enough to be worth asserting on.
dream_add_game(sonicadv TITLE "Sonic Adventure")

# SA-nx: units built with the compiler's full optimisation (the rest keep -O1, which keeps the
# whole game's build to hours). switch-test17: the movie player's main unit (MOVIE.BIN, Sofdec at
# 0x8CEB0000). switch-test18: all of the movie player, and the units holding most of play's
# translated code in switch-test15 and 17's gameplay profiles (main parts 1, 2, 11, 27, 29, 30
# and 31 and the Emerald Coast module, 84% of it). -fno-strict-aliasing because translated code
# reads guest memory through casts that -O2 would otherwise be entitled to reorder.
set(_sanx_full_opt)
foreach(_u sonicadv_part1 sonicadv_part2 sonicadv_part11 sonicadv_part27 sonicadv_part29
           sonicadv_part30 sonicadv_part31 sonicadv_reloc_8ceb0000 sonicadv_reloc_8c900000_34)
  list(APPEND _sanx_full_opt ${CMAKE_CURRENT_BINARY_DIR}/gen/${_u}.cpp)
endforeach()
foreach(_k RANGE 1 7)
  list(APPEND _sanx_full_opt ${CMAKE_CURRENT_BINARY_DIR}/gen/sonicadv_reloc_8ceb0000_part${_k}.cpp
                             ${CMAKE_CURRENT_BINARY_DIR}/gen/sonicadv_reloc_8c900000_34_part${_k}.cpp)
endforeach()
if(MSVC)
  set_source_files_properties(${_sanx_full_opt} PROPERTIES COMPILE_OPTIONS "/O2;/bigobj")
else()
  set_source_files_properties(${_sanx_full_opt} PROPERTIES COMPILE_OPTIONS "-O2;-fno-strict-aliasing")
endif()

# Every guest call, and every jump between translated functions, is a host call. Windows gives the
# main thread 1 MB of stack; play test 2026-10-29 died with 0xC00000FD (stack overflow) at Emerald
# Coast's mid-stage load. 256 MB, reserved not committed, so it costs nothing unless used.
if(MSVC)
  target_link_options(sonicadv_boot PRIVATE /STACK:268435456)
endif()

# The Switch (SA-nx): Horizon's own main() around the launcher, and the NRO the homebrew menu
# runs. switch/build.sh configures this with devkitPro's toolchain.
if(NINTENDO_SWITCH)
  target_sources(sonicadv_boot PRIVATE ${CMAKE_SOURCE_DIR}/switch/src/switch_main.cpp
    ${CMAKE_SOURCE_DIR}/switch/src/egl_stubs.c)
  target_link_libraries(sonicadv_boot PRIVATE nx)
  # switch-test28: a cache in front of libgcc's FDE search for C++ exception unwinding
  # (switch_main.cpp, __wrap__Unwind_Find_FDE).
  target_link_options(sonicadv_boot PRIVATE -Wl,--wrap=_Unwind_Find_FDE)
  set(SANX_VERSION "0.5.0" CACHE STRING "Version shown in the homebrew menu")
  # A release: no end-of-run report, no profiler, stock clocks on the first run (switch_main.cpp).
  option(SANX_RELEASE "Build the Switch launcher as a release" OFF)
  if(SANX_RELEASE)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/switch/src/switch_main.cpp
      PROPERTIES COMPILE_DEFINITIONS SANX_RELEASE=1)
  endif()
  nx_generate_nacp(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sonicadv.nacp
    NAME "Sonic Adventure (SA-nx)"
    AUTHOR "SA-nx"
    VERSION "${SANX_VERSION}")
  nx_create_nro(sonicadv_boot NACP ${CMAKE_CURRENT_BINARY_DIR}/sonicadv.nacp
    ICON ${CMAKE_SOURCE_DIR}/switch/icon.jpg)
  add_subdirectory(${CMAKE_SOURCE_DIR}/switch/probe ${CMAKE_BINARY_DIR}/probe)
  add_subdirectory(${CMAKE_SOURCE_DIR}/switch/replay ${CMAKE_BINARY_DIR}/replay)
endif()
