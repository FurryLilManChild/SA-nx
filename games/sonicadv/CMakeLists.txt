# Sonic Adventure. Everything is in cmake/DreamAddGame.cmake; this file exists so that adding a game is
# adding a directory. Add BOOT_TEST once the title boots far enough to be worth asserting on.
dream_add_game(sonicadv TITLE "Sonic Adventure")

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
endif()
