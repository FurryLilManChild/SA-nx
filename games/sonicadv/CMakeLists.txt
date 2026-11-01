# Sonic Adventure. Everything is in cmake/DreamAddGame.cmake; this file exists so that adding a game is
# adding a directory. Add BOOT_TEST once the title boots far enough to be worth asserting on.
dream_add_game(sonicadv TITLE "Sonic Adventure")

# SA-nx (switch-test17): the movie player (MOVIE.BIN, Sega's Sofdec, translated as one unit at
# 0x8CEB0000) with the compiler's full optimisation. The rest of the game stays at the -O1 that
# keeps its build to hours rather than days; this unit is 1.2 MB, and in switch-test15's movie
# profile its code was two thirds of a movie's time. -fno-strict-aliasing because translated code
# reads guest memory through casts that -O2 would otherwise be entitled to reorder.
if(MSVC)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gen/sonicadv_reloc_8ceb0000.cpp
    PROPERTIES COMPILE_OPTIONS "/O2;/bigobj")
else()
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gen/sonicadv_reloc_8ceb0000.cpp
    PROPERTIES COMPILE_OPTIONS "-O2;-fno-strict-aliasing")
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
  set(SANX_VERSION "0.1.0" CACHE STRING "Version shown in the homebrew menu")
  nx_generate_nacp(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sonicadv.nacp
    NAME "Sonic Adventure (SA-nx)"
    AUTHOR "SA-nx"
    VERSION "${SANX_VERSION}")
  nx_create_nro(sonicadv_boot NACP ${CMAKE_CURRENT_BINARY_DIR}/sonicadv.nacp
    ICON ${CMAKE_SOURCE_DIR}/switch/icon.jpg)
  add_subdirectory(${CMAKE_SOURCE_DIR}/switch/probe ${CMAKE_BINARY_DIR}/probe)
  add_subdirectory(${CMAKE_SOURCE_DIR}/switch/replay ${CMAKE_BINARY_DIR}/replay)
endif()
