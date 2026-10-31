# SA-nx resolve probe (Stage 1): a separate NRO that times the per-pixel translucency resolve
# through NVK and through deko3d. Built with the Switch build: ninja sa_probe_nro.
find_program(SANX_UAM NAMES uam HINTS "$ENV{DEVKITPRO}/tools/bin" /opt/devkitpro/tools/bin)
if(NOT SANX_UAM OR NOT DREAM_GLSLC)
  message(STATUS "SA-nx probe: needs glslc and uam; skipped")
  return()
endif()
set(_render_shaders ${CMAKE_SOURCE_DIR}/dream-recomp/render/shaders)
set(_probe_header ${CMAKE_CURRENT_BINARY_DIR}/probe_shaders.h)
file(GLOB _probe_glsl ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag)
add_custom_command(
  OUTPUT ${_probe_header}
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen_shaders.py ${_render_shaders}
          ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${DREAM_GLSLC} ${SANX_UAM} ${_probe_header}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_shaders.py ${_probe_glsl}
          ${_render_shaders}/oit_resolve.frag ${_render_shaders}/oit_resolve.vert
          ${_render_shaders}/oit_common.glsl
  COMMENT "probe shaders (glslc and uam)"
  VERBATIM)
add_executable(sa_probe probe_main.cpp probe_vk.cpp probe_dk.cpp ${_probe_header}
  ${CMAKE_SOURCE_DIR}/switch/src/egl_stubs.c)
target_include_directories(sa_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sa_probe PRIVATE dream_render_vk deko3d nx)
nx_generate_nacp(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sa_probe.nacp
  NAME "SA-nx resolve probe"
  AUTHOR "SA-nx"
  VERSION "1")
nx_create_nro(sa_probe NACP ${CMAKE_CURRENT_BINARY_DIR}/sa_probe.nacp
  ICON ${CMAKE_SOURCE_DIR}/switch/icon.jpg)
