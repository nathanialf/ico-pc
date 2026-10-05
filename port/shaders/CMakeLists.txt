# port/shaders/CMakeLists.txt: the HLSL shaders, compiled by DXC at build
# time (cmake/IcoShaders.cmake) and embedded in ico_shaders. docs/port/SHADERS.md
# says how to add one. Tests are in test/.
include(${CMAKE_SOURCE_DIR}/cmake/IcoShaders.cmake)

# name, file, entry, stage
ico_add_shader(sprite_ui_vs sprite.hlsl sprite_ui_vs vertex)
ico_add_shader(sprite_world_vs sprite.hlsl sprite_world_vs vertex)
ico_add_shader(sprite_ps sprite.hlsl sprite_ps fragment)
ico_add_shader(date_snap_ps sprite.hlsl date_snap_ps fragment)
ico_add_shader(blit_vs blit.hlsl blit_vs vertex)
ico_add_shader(blit_ps blit.hlsl blit_ps fragment)
ico_add_shader(blit_fix_ps blit.hlsl blit_fix_ps fragment)
# wave 2 (R2c): tests only, rd__CameraProbe
ico_add_shader(camera_probe_ps blit.hlsl camera_probe_ps fragment)
ico_add_shader(blend_int_vs blend_int.hlsl blend_int_vs vertex)
ico_add_shader(blend_int_ps blend_int.hlsl blend_int_ps fragment)
# wave 4 placeholder
ico_add_shader(fog_lut_vs fog_lut.hlsl fog_lut_vs vertex)
ico_add_shader(fog_lut_ps fog_lut.hlsl fog_lut_ps fragment)
ico_add_shader(font_vs font.hlsl font_vs vertex)
ico_add_shader(font_ps font.hlsl font_ps fragment)
# wave 3 (R3c): the VU1 microprograms (docs/port/VU1_PROGRAMS.md)
ico_add_shader(vu_prelit_vs vu_prelit.hlsl vu_prelit_vs vertex)
ico_add_shader(vu_ps vu_prelit.hlsl vu_ps fragment)
ico_add_shader(vu_lit_vs vu_lit.hlsl vu_lit_vs vertex)
ico_add_shader(vu_lit_spec_vs vu_lit.hlsl vu_lit_spec_vs vertex)
ico_add_shader(vu_reflect_vs vu_lit.hlsl vu_reflect_vs vertex)
ico_add_shader(vu_skin_vs vu_skin.hlsl vu_skin_vs vertex)
ico_add_shader(vu_skin_spec_vs vu_skin.hlsl vu_skin_spec_vs vertex)
ico_add_shader(vu_skin_debug_vs vu_skin.hlsl vu_skin_debug_vs vertex)
ico_add_shader(vu_grid_vs vu_grid.hlsl vu_grid_vs vertex)
ico_add_shader(vu_grid_lit_vs vu_grid.hlsl vu_grid_lit_vs vertex)
ico_add_shader(vu_grid_spec_vs vu_grid.hlsl vu_grid_spec_vs vertex)
ico_add_shader(vu_particle_vs vu_particle.hlsl vu_particle_vs vertex)
# tests only: the probe output of vu1_test (VU_F_PROBE)
ico_add_shader(vu_probe_ps vu_prelit.hlsl vu_probe_ps fragment)

ico_shaders_library(ico_shaders)

if(TARGET ico_shaders)
    add_subdirectory(test)
endif()
