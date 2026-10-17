# port/shaders/CMakeLists.txt: the HLSL shaders, compiled by DXC at build
# time (cmake/IcoShaders.cmake) and embedded in ico_shaders. Tests are in test/.
include(${CMAKE_SOURCE_DIR}/cmake/IcoShaders.cmake)

# name, file, entry, stage
ico_add_shader(sprite_ui_vs sprite.hlsl sprite_ui_vs vertex)
ico_add_shader(sprite_world_vs sprite.hlsl sprite_world_vs vertex)
ico_add_shader(sprite_ps sprite.hlsl sprite_ps fragment)
# TEXA per texel before the bilinear filter (24- and 16-bit textures under AEM)
ico_add_shader(sprite_texa_ps sprite.hlsl sprite_texa_ps fragment)
ico_add_shader(date_snap_ps sprite.hlsl date_snap_ps fragment)
# PRIM.AA1 lines and triangles
ico_add_shader(sprite_aa1_ui_vs sprite.hlsl sprite_aa1_ui_vs vertex)
ico_add_shader(sprite_aa1_world_vs sprite.hlsl sprite_aa1_world_vs vertex)
ico_add_shader(sprite_aa1_ps sprite.hlsl sprite_aa1_ps fragment)
ico_add_shader(blit_vs blit.hlsl blit_vs vertex)
ico_add_shader(blit_ps blit.hlsl blit_ps fragment)
# the present's box blit with the scene's depth (rd_present.c)
ico_add_shader(blit_depth_ps blit.hlsl blit_depth_ps fragment)
# the shadow count box-reduced to the GS size (rd_replay.c doShadowResolve)
ico_add_shader(box_reduce_ps blit.hlsl box_reduce_ps fragment)
# supersampling: DISPLAY box-averaged to the present box (rd_present.c)
ico_add_shader(aa_reduce_ps blit.hlsl aa_reduce_ps fragment)
# MSAA: the multisampled scene depth resolved (smallest sample) and the
# single-sampled one spread over the samples (rd_replay.c, the MSAA epoch)
ico_add_shader(depth_resolve_ps blit.hlsl depth_resolve_ps fragment)
ico_add_shader(depth_upsample_ps blit.hlsl depth_upsample_ps fragment)
# tests only, rd__camera_probe
ico_add_shader(camera_probe_ps blit.hlsl camera_probe_ps fragment)
ico_add_shader(blend_int_vs blend_int.hlsl blend_int_vs vertex)
ico_add_shader(blend_int_ps blend_int.hlsl blend_int_ps fragment)
# the depth fog's CLUT lookup (rd_replay.c doFog)
ico_add_shader(fog_lut_ps fog_lut.hlsl fog_lut_ps fragment)
# the same, reading the depth from an R32_UINT copy of it (rd_fog_path.c)
ico_add_shader(fog_lut_buffer_ps fog_lut.hlsl fog_lut_ps fragment DEFINES FOG_BUFFER=1)
# the same per sample, reading the multisampled depth (MSAA with sample-rate
# shading)
ico_add_shader(fog_lut_ms_ps fog_lut.hlsl fog_lut_ps fragment DEFINES FOG_MS=1)
# R8 coverage textures (the font atlas) behind the sprite vertex shaders
ico_add_shader(font_ps font.hlsl font_ps fragment)
# sheet text, coverage strips drawn in the menu sheets' style
ico_add_shader(font_sheet_ps font.hlsl font_sheet_ps fragment)
# the VU1 microprograms
ico_add_shader(vu_prelit_vs vu_prelit.hlsl vu_prelit_vs vertex)
ico_add_shader(vu_ps vu_prelit.hlsl vu_ps fragment)
ico_add_shader(vu_texa_ps vu_prelit.hlsl vu_texa_ps fragment)
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
# the vertex pre-pass (docs/port/RENDER.md, "The vertex pre-pass";
# rd_replay_vu.c rd__vu_prepass): each mesh program's vertices once, as records
# (vu_X_pre_vs, vu_pre_ps into three RGBA32_UINT targets), and the triangle
# stage of every mesh program reading them back (vu_tri_vs)
foreach(_v prelit:prelit lit:lit lit_spec:lit reflect:lit skin:skin skin_spec:skin skin_debug:skin
        grid:grid grid_lit:grid grid_spec:grid)
    string(REGEX REPLACE ":.*" "" _e "${_v}")
    string(REGEX REPLACE ".*:" "" _f "${_v}")
    ico_add_shader(vu_${_e}_pre_vs vu_${_f}.hlsl vu_${_e}_pre_vs vertex)
endforeach()
ico_add_shader(vu_pre_ps vu_tri.hlsl vu_pre_ps fragment)
ico_add_shader(vu_tri_vs vu_tri.hlsl vu_tri_vs vertex)
# the plain variants (ICO_VU_PLAIN, vu_common.hlsli): no card test and no
# tick cameras, for the draws with neither (rd_replay_vu.c vuDraw); about half
# the size (issue 107: the Adreno 740's compiler hangs the device on the
# extended ones)
foreach(_v prelit:prelit lit:lit lit_spec:lit reflect:lit skin:skin skin_spec:skin skin_debug:skin
        grid:grid grid_lit:grid grid_spec:grid particle:particle tri:tri)
    string(REGEX REPLACE ":.*" "" _e "${_v}")
    string(REGEX REPLACE ".*:" "" _f "${_v}")
    ico_add_shader(vu_${_e}_plain_vs vu_${_f}.hlsl vu_${_e}_vs vertex DEFINES ICO_VU_PLAIN=1)
endforeach()
# the FMV picture (port/render/rd_video.c)
ico_add_shader(yuv_vs yuv.hlsl yuv_vs vertex)
ico_add_shader(yuv_ps yuv.hlsl yuv_ps fragment)
# a movie's field deinterlaced and converted at the picture's size
ico_add_shader(yuv_field_ps yuv.hlsl yuv_field_ps fragment)
# staticBlur.c's sprites in the GS integer arithmetic
# (port/render/rd_blur.c)
ico_add_shader(fx_rect_vs fx_sprite.hlsl fx_rect_vs vertex)
ico_add_shader(fx_sprite_ps fx_sprite.hlsl fx_sprite_ps fragment)
# COLCLAMP 0 screen prims (darkVolume.c)
ico_add_shader(wrap_acc_ps raw_wrap.hlsl wrap_acc_ps fragment)
ico_add_shader(wrap_resolve_ps raw_wrap.hlsl wrap_resolve_ps fragment)
# perspective-correct STQ on screen prims
ico_add_shader(sprite_stq_ui_vs sprite.hlsl sprite_stq_ui_vs vertex)
ico_add_shader(sprite_stq_world_vs sprite.hlsl sprite_stq_world_vs vertex)
ico_add_shader(sprite_stq_ps sprite.hlsl sprite_stq_ps fragment)
# the screen-prim triangles with their planes (rd_screen_planes.c)
ico_add_shader(sprite_plane_ui_vs sprite.hlsl sprite_plane_ui_vs vertex)
ico_add_shader(sprite_plane_world_vs sprite.hlsl sprite_plane_world_vs vertex)
ico_add_shader(sprite_stq_plane_ui_vs sprite.hlsl sprite_stq_plane_ui_vs vertex)
ico_add_shader(sprite_stq_plane_world_vs sprite.hlsl sprite_stq_plane_world_vs vertex)
# the CRT filter (port/render/rd_crt.c)
ico_add_shader(crt_vs crt.hlsl crt_vs vertex)
ico_add_shader(crt_bloom_ps crt.hlsl crt_bloom_ps fragment)
ico_add_shader(crt_blur_ps crt.hlsl crt_blur_ps fragment)
ico_add_shader(crt_ps crt.hlsl crt_ps fragment)
# tests only: the probe output of vu1_test (VU_F_PROBE)
ico_add_shader(vu_probe_ps vu_prelit.hlsl vu_probe_ps fragment)
# the entries that call gs_dual_out, without the second output,
# for a device without dual-source blending (rd_pipeline.c rd__expand_no_dual)
foreach(_e sprite_ps sprite_texa_ps sprite_aa1_ps sprite_stq_ps)
    ico_add_shader(${_e} sprite.hlsl ${_e} fragment DEFINES ICO_NO_DUAL=1 SUFFIX _nodual)
endforeach()
ico_add_shader(fog_lut_ps fog_lut.hlsl fog_lut_ps fragment DEFINES ICO_NO_DUAL=1 SUFFIX _nodual)
ico_add_shader(fog_lut_buffer_ps fog_lut.hlsl fog_lut_ps fragment
               DEFINES FOG_BUFFER=1 ICO_NO_DUAL=1 SUFFIX _nodual)
ico_add_shader(fog_lut_ms_ps fog_lut.hlsl fog_lut_ps fragment
               DEFINES FOG_MS=1 ICO_NO_DUAL=1 SUFFIX _nodual)
ico_add_shader(font_ps font.hlsl font_ps fragment DEFINES ICO_NO_DUAL=1 SUFFIX _nodual)
ico_add_shader(font_sheet_ps font.hlsl font_sheet_ps fragment DEFINES ICO_NO_DUAL=1 SUFFIX _nodual)
ico_add_shader(vu_ps vu_prelit.hlsl vu_ps fragment DEFINES ICO_NO_DUAL=1 SUFFIX _nodual)
ico_add_shader(vu_texa_ps vu_prelit.hlsl vu_texa_ps fragment DEFINES ICO_NO_DUAL=1 SUFFIX _nodual)

# the same entries computing the GS blend from the destination (the exact
# blend, port/render/rd_dst_read.c): _dstrov reads and writes the target
# as a rasterizer-ordered view (D3D12 ROV, Vulkan fragment shader
# interlock), _dstatt reads the colour attachment in rasterization order
# (Vulkan only; its DXIL is the ROV build, never used)
foreach(_e sprite_ps sprite_texa_ps sprite_aa1_ps sprite_stq_ps)
    ico_add_shader(${_e} sprite.hlsl ${_e} fragment DEFINES ICO_DST_READ=1 SUFFIX _dstrov)
    ico_add_shader(${_e} sprite.hlsl ${_e} fragment DEFINES ICO_DST_READ=2 SUFFIX _dstatt)
endforeach()
foreach(_v rov:1 att:2)
    string(REGEX REPLACE ":.*" "" _s "${_v}")
    string(REGEX REPLACE ".*:" "" _d "${_v}")
    ico_add_shader(fog_lut_ps fog_lut.hlsl fog_lut_ps fragment DEFINES ICO_DST_READ=${_d}
                   SUFFIX _dst${_s})
    ico_add_shader(fog_lut_buffer_ps fog_lut.hlsl fog_lut_ps fragment
                   DEFINES FOG_BUFFER=1 ICO_DST_READ=${_d} SUFFIX _dst${_s})
    ico_add_shader(font_ps font.hlsl font_ps fragment DEFINES ICO_DST_READ=${_d} SUFFIX _dst${_s})
    ico_add_shader(font_sheet_ps font.hlsl font_sheet_ps fragment DEFINES ICO_DST_READ=${_d}
                   SUFFIX _dst${_s})
    ico_add_shader(vu_ps vu_prelit.hlsl vu_ps fragment DEFINES ICO_DST_READ=${_d} SUFFIX _dst${_s})
    ico_add_shader(vu_texa_ps vu_prelit.hlsl vu_texa_ps fragment DEFINES ICO_DST_READ=${_d}
                   SUFFIX _dst${_s})
endforeach()

ico_shaders_library(ico_shaders)

if(TARGET ico_shaders)
    add_subdirectory(test)
endif()

# ---- v0.7.10 (docs/port/RENDER.md): each package adds its entries inside its
# own fence and nowhere else, so the packages merge without conflicts.  The
# ids and names are rd_internal.h's RD_VS_V0710_FIRST / RD_FS_V0710_FIRST on
# (rd_replay.c s_vsNames, s_fsNames); a module missing from the table leaves
# its id empty until its package lands (rd__gpu_init's fence).
# ---- G1 record stages (vu_main_vs, vu_particle_main_vs, vu_tri_rule_pre_vs,
#      vu_tri_tick_pre_vs, vu_tri_planes_pre_vs, vu_particle_pre_vs,
#      vu_legacy_ps, vu_legacy_texa_ps and their twins) ----
# ---- G1 end ----
# ---- G2 2D varying diet (sprite_*_ps_planes and their twins) ----
# ---- G2 end ----
# ---- X2 tiler passes (fog_lut_local_ps, fx_blur2_ps, date_clear_ps, date_stencil_ps) ----
# ---- X2 end ----
# ---- X3 pixel cuts (fx_sprite_u_ps) ----
# fx_sprite_ps reading its textures through their RGBA8_UINT views
# (RHI_TEX_UINT_VIEW; rd_replay.c doBlurSpriteDraw)
ico_add_shader(fx_sprite_u_ps fx_sprite.hlsl fx_sprite_ps fragment DEFINES ICO_UINT_SRC=1)
# ---- X3 end ----
