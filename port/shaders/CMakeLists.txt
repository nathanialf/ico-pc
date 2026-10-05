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

ico_shaders_library(ico_shaders)

if(TARGET ico_shaders)
    add_subdirectory(test)
endif()
