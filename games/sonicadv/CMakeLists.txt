# Sonic Adventure. Everything is in cmake/DreamAddGame.cmake; this file exists so that adding a game is
# adding a directory. Add BOOT_TEST once the title boots far enough to be worth asserting on.
# The US version (v1.005) is translated into the same launcher; the disc picks one at start
# (sonicadv_us.toml, generated from sonicadv.toml by tools/map_variant.py).
dream_add_game(sonicadv TITLE "Sonic Adventure" VARIANTS sonicadv_us)

# SA-nx: units built with the compiler's full optimisation (the rest keep -O1, which keeps the
# whole game's build to hours). switch-test17: the movie player's main unit (MOVIE.BIN, Sofdec at
# 0x8CEB0000). switch-test18: all of the movie player, and the units holding most of play's
# translated code in switch-test15 and 17's gameplay profiles (main parts 1, 2, 11, 27, 29, 30
# and 31 and the Emerald Coast module, 84% of it). -fno-strict-aliasing because translated code
# reads guest memory through casts that -O2 would otherwise be entitled to reorder.
set(_sanx_full_opt)
# The same units of the US version (its config keeps the PAL order, so the same names).
foreach(_g sonicadv sonicadv_us)
  foreach(_u part1 part2 part11 part27 part29 part30 part31 reloc_8ceb0000 reloc_8c900000_34)
    list(APPEND _sanx_full_opt ${CMAKE_CURRENT_BINARY_DIR}/gen/${_g}_${_u}.cpp)
  endforeach()
  foreach(_k RANGE 1 7)
    list(APPEND _sanx_full_opt ${CMAKE_CURRENT_BINARY_DIR}/gen/${_g}_reloc_8ceb0000_part${_k}.cpp
                               ${CMAKE_CURRENT_BINARY_DIR}/gen/${_g}_reloc_8c900000_34_part${_k}.cpp)
  endforeach()
endforeach()
if(MSVC)
  set_source_files_properties(${_sanx_full_opt} PROPERTIES COMPILE_OPTIONS "/O2;/bigobj")
else()
  set_source_files_properties(${_sanx_full_opt} PROPERTIES COMPILE_OPTIONS "-O2;-fno-strict-aliasing")
endif()

# SA-nx Test 2.3: the movie player's IDCT (0x0CEB1504) run natively, checked against the
# translation on real calls (native/movie_idct.cpp, native/native_movie.cpp).
if(TARGET sonicadv_boot)
  set(_native ${CMAKE_CURRENT_SOURCE_DIR}/native/movie_idct.cpp
              ${CMAKE_CURRENT_SOURCE_DIR}/native/native_movie.cpp)
  target_sources(sonicadv_boot PRIVATE ${_native})
  if(MSVC)
    set_source_files_properties(${_native} PROPERTIES COMPILE_OPTIONS "/O2")
  else()
    set_source_files_properties(${_native} PROPERTIES COMPILE_OPTIONS "-O2;-fno-strict-aliasing")
  endif()
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
  # Test 2.2: with the US version the ELF's debug information passed 4 GB and the linker (the
  # Windows build under Wine on a cloud machine) wrote a broken file. Compressed, it is a fraction
  # of that, and addr2line still reads it for crash offsets.
  target_link_options(sonicadv_boot PRIVATE -Wl,--compress-debug-sections=zlib)
  set(SANX_VERSION "0.6.0" CACHE STRING "Version shown in the homebrew menu")
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
