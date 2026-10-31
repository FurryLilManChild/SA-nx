# SA-nx capture replayer: draws F11 captures through the game's renderer on the Switch, once per
# experiment (Offscreen::Experiment). Built with the Switch build: ninja sa_replay_nro.
add_executable(sa_replay replay_main.cpp ${CMAKE_SOURCE_DIR}/switch/src/egl_stubs.c)
target_link_libraries(sa_replay PRIVATE dream_render_vk nx)
nx_generate_nacp(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sa_replay.nacp
  NAME "SA-nx capture replayer"
  AUTHOR "SA-nx"
  VERSION "1")
nx_create_nro(sa_replay NACP ${CMAKE_CURRENT_BINARY_DIR}/sa_replay.nacp
  ICON ${CMAKE_SOURCE_DIR}/switch/icon.jpg)
