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
# package R8: R8 coverage textures (the font atlas) behind the sprite vertex shaders
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
# Phase 4E: the FMV picture (port/render/rd_video.c, docs/port/FMV.md)
ico_add_shader(yuv_vs yuv.hlsl yuv_vs vertex)
ico_add_shader(yuv_ps yuv.hlsl yuv_ps fragment)
# wave 5 (R5a): staticBlur.c's sprites in the GS integer arithmetic
# (port/render/rd_blur.c, docs/port/RENDER_API.md "Render-to-texture surfaces")
ico_add_shader(fx_rect_vs fx_sprite.hlsl fx_rect_vs vertex)
ico_add_shader(fx_sprite_ps fx_sprite.hlsl fx_sprite_ps fragment)
# wave 5 (R5c): COLCLAMP 0 screen prims (darkVolume.c), RENDER_API.md "Full-screen effects and the
# raw packet builders"
ico_add_shader(wrap_acc_ps raw_wrap.hlsl wrap_acc_ps fragment)
ico_add_shader(wrap_resolve_ps raw_wrap.hlsl wrap_resolve_ps fragment)
# tests only: the probe output of vu1_test (VU_F_PROBE)
ico_add_shader(vu_probe_ps vu_prelit.hlsl vu_probe_ps fragment)

ico_shaders_library(ico_shaders)

if(TARGET ico_shaders)
    add_subdirectory(test)
endif()
