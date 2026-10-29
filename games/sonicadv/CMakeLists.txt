# Sonic Adventure. Everything is in cmake/DreamAddGame.cmake; this file exists so that adding a game is
# adding a directory. Add BOOT_TEST once the title boots far enough to be worth asserting on.
dream_add_game(sonicadv TITLE "Sonic Adventure")

# Every guest call, and every jump between translated functions, is a host call. Windows gives the
# main thread 1 MB of stack; play test 2026-10-29 died with 0xC00000FD (stack overflow) at Emerald
# Coast's mid-stage load. 256 MB, reserved not committed, so it costs nothing unless used.
if(MSVC)
  target_link_options(sonicadv_boot PRIVATE /STACK:268435456)
endif()
