# port/render: the native draw API (rd.h) on the RHI.
#
# ico_render needs the shader table (ico_shaders, DXC) and an RHI backend
# (ico_rhi_vk); without either it is left out with a status message, like
# the other fetched dependencies.

# ico_vu1_ref (wave 3, R3c): CPU references of the five VU1 microprograms, the oracle of port/shaders/test/vu1_test.c.
# Plain C on port/math's ps2float helpers; built in every configuration.
add_library(ico_vu1_ref STATIC
    vu1_ref/vu1_ref.c
    vu1_ref/vu1_normal.c
    vu1_ref/vu1_scissor.c
    vu1_ref/vu1_cluster.c
    vu1_ref/vu1_mesh.c
    vu1_ref/vu1_particle.c)
target_include_directories(ico_vu1_ref PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vu1_ref"
                           PRIVATE "${CMAKE_SOURCE_DIR}/port/math")
target_compile_options(ico_vu1_ref PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_vu1_ref PUBLIC ico_math)

if(NOT TARGET ico_shaders OR NOT TARGET ico_rhi_vk)
    message(STATUS "ico: port/render not built (needs ico_shaders and ico_rhi_vk; run "
                   "tools/fetch_deps.sh)")
    return()
endif()

add_library(ico_render STATIC
    rd_blur.c # wave 5 (R5a): staticBlur.c's sprites
    rd_core.c
    rd_crt.c # package CRT: the CRT filter at present time
    rd_dump.c
    rd_frame.c
    rd_interp.c # wave 7 (R7b): interpolation between the retained frames
    rd_mesh.c
    rd_perf.c # package P1: per-replay performance records
    rd_photo.c # package PHOTO: the camera override and its pinned frame
    rd_pipeline.c
    rd_png.c
    rd_post.c
    rd_present.c
    rd_replay.c
    rd_shadow.c
    rd_tex.c
    rd_water.c # wave 5 (R5b): block targets, aliases, camera scopes
    rd_video.c # Phase 4E: the FMV picture (port/fmv/rd_video.h)
    texpack_dds.c # texture packs: the DDS and PNG loaders (texpack.h)
    texpack_image.c
    texpack_png.c)
target_include_directories(ico_render
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(ico_render PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_render PUBLIC ico_rhi_vk ico_shaders ico_vu1_ref)
# texture packs: texpack_png.c inflates with miniz's tinfl
target_link_libraries(ico_render PRIVATE ico_miniz)
if(NOT WIN32)
    target_link_libraries(ico_render PUBLIC m)
endif()

# rd_replay_tool: a dump to a PNG, headless.
add_executable(rd_replay_tool tools/rd_replay_tool.c)
target_compile_options(rd_replay_tool PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_replay_tool PRIVATE ico_render)

# --- Tests ----------------------------------------------------------------------
#   rd_state  lists, state leakage, keep frames, recording of the stubbed
#             commands, dump round trip of the recording, pipeline
#             enumeration (CPU only)
#   rd_pixel  GS sprite rasterisation, reduction against a CPU reference,
#             blend_int feedback over 100 frames, dump -> load -> replay,
#             pipeline count (Vulkan; exit 77 without a device)
#   rd_replay_tool  rd_pixel's dump rendered to a PNG through the presenter
#   rd_shadow  Shadow.c's host path and the stencil count (wave 4, below)
#   rd_fog     ZFog.c's host path and the depth fog (wave 4, below)
add_executable(rd_state_test test/rd_state_test.c)
target_compile_options(rd_state_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_state_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_state_test PRIVATE ico_render)

add_executable(rd_pixel_test test/rd_pixel_test.c)
target_include_directories(rd_pixel_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/shaders/test" "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_pixel_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_pixel_test PRIVATE ico_render)

# rd_layout_test: the seki layer's host path (GifPacket.c, DisplayList.c,
# DmaPacket.c compiled with ICO_HOST and ICO_RD, as the window build has
# them) fed layout_texture.c's call sequence for a synthetic layout; the
# recording is checked without a device, one sprite's pixels on one.
add_executable(rd_layout_test test/rd_layout_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
ico_include_dirs(seki _ico_seki_incs)
target_include_directories(rd_layout_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(rd_layout_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_layout_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_layout_test PRIVATE ico_render ico_math)

# rd_tex_test: the texture cache, and Texture.c's host path with the three
# files above, fed synthetic TIM2 files; decode, CLUT scroll, cache keys and
# the decoder's binding on the recording, one sprite per format's texels on
# a device (77 without one).
add_executable(rd_tex_test test/rd_tex_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Texture.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(rd_tex_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(rd_tex_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_tex_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_tex_test PRIVATE ico_render ico_math)

# rd_gsbase_test (wave 2, R2c): GsBase.c's host path with the 2D layer
# (GifPacket.c, DisplayList.c, DmaPacket.c) as the window build has them,
# the rest of the game stubbed: frame lifecycle, field parity, camera
# packing, Z scale and the post passes against CPU references.
add_executable(rd_gsbase_test test/rd_gsbase_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GsBase.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(rd_gsbase_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/shaders/test"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(rd_gsbase_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_gsbase_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
# ico_options: GsBase.c's widescreen hook reads the display options (R7a),
# whose config store needs host_config.c (above)
target_link_libraries(rd_gsbase_test PRIVATE ico_render ico_math ico_options)
if(WIN32)
    target_link_libraries(rd_gsbase_test PRIVATE comdlg32 user32)
endif()

# rd_present_test (wave 7, R7a): the display options.  GsBase.c as
# rd_gsbase_test has it (the widened cull, +0x80/+0xC0 untouched) and the
# options module with its config (port/platform/host_config.c): the Original
# preset bit-exact against recorded frames, the Enhanced scene resolution,
# the wide projection, the UI box, the letterbox bars, the presenter's boxes
# and the filter upgrade's mips.
add_executable(rd_present_test test/rd_present_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GsBase.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(rd_present_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/shaders/test"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi"
                           "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(rd_present_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_present_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_present_test PRIVATE ico_render ico_math ico_options)
if(WIN32)
    target_link_libraries(rd_present_test PRIVATE comdlg32 user32)
endif()

# rd_crt_test (package CRT): the CRT filter (rd_crt.c, crt.hlsl): its [video]
# options with their config (host_config.c), the modes and overrides, and on
# a device the presents: off is rd_present's bytes, each mode's recorded hash,
# black outside the box, the brightness, the mask's period and its fade.
add_executable(rd_crt_test test/rd_crt_test.c "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(rd_crt_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(rd_crt_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_crt_test PRIVATE ico_render ico_options)
if(WIN32)
    target_link_libraries(rd_crt_test PRIVATE comdlg32 user32)
endif()

# rd_filter_test (package MV): the draw filter (rd.h rd_SetDrawFilter) on
# synthetic keys, recorded without a device.
add_executable(rd_filter_test test/rd_filter_test.c)
target_compile_options(rd_filter_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_filter_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_filter_test PRIVATE ico_render)

# rd_interp_test (wave 7, R7b): the presenter's interpolation between the
# retained frames (rd_interp.c): keyed draws blend by alpha, the snap rules,
# the feedback FIX at dt; on a device the replays at alpha 0 and 1 against
# the frames' own, the half-way pixels, rd_Present.
add_executable(rd_interp_test test/rd_interp_test.c)
target_compile_options(rd_interp_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_interp_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_interp_test PRIVATE ico_render)

# rd_mirror_test (wave 7, R7c): the mirror mode: the present is the exact
# flip with it on (Original, Enhanced, interpolated), UI prims land on the
# same output pixels (the replay's flip cancels the present's), SCENE's UI
# flipped about the centre.
add_executable(rd_mirror_test test/rd_mirror_test.c)
target_compile_options(rd_mirror_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_mirror_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_mirror_test PRIVATE ico_render)

# rd_perf_test (package P1): a synthetic game-shaped frame replayed 200
# times and recorded 200 times: no resource is created or destroyed and no
# texture or static mesh uploaded again in steady state, and the CPU time of
# a replay stays under 20 ms on lavapipe; with --dump FILE it times a dump's
# replay phase by phase.
add_executable(rd_perf_test test/rd_perf_test.c)
target_compile_options(rd_perf_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_perf_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_perf_test PRIVATE ico_render)

# rd_mesh_test (wave 3, R3ab): the mesh path.  Packet.c, RegistPacket.c,
# MicroCode.c, DisplayP2O.c and Primitive.c with the 2D layer and Matrix.c,
# as the window build has them, fed synthetic p2o-decoded models, a grid and
# a particle batch: the meshes, the recorded draws and VuCB, the GS state;
# on a device each case against the VU reference drawn as sprites (77
# without one); the title logo's strips Packet.c draws in another order,
# their geometry from the disc image when there is one.
add_executable(rd_mesh_test test/rd_mesh_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Packet.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/RegistPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/MicroCode.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayP2O.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Primitive.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Matrix.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(rd_mesh_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/render/vu1_ref")
target_compile_definitions(rd_mesh_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_mesh_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
# the strip order case reads the title logo's models from the disc image
# (df_pack.h); without it that part is skipped
target_link_libraries(rd_mesh_test PRIVATE ico_render ico_vu1_ref ico_math ico_port_data)

# rd_shadow_test (wave 4, R4b): the shadow count on the stencil.  Shadow.c
# with the 2D layer and Matrix.c, as the window build has them, the rest of
# the game stubbed: the count against the wrapped colour sum, the blur chain
# and the composites against CPU references, the DATE receiver mask, and
# the commands Shadow.c's host path records (77 without a device, after the
# recording checks).
add_executable(rd_shadow_test test/rd_shadow_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Shadow.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Matrix.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(rd_shadow_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(rd_shadow_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_shadow_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_shadow_test PRIVATE ico_render ico_math)

# rd_fog_test (wave 4, R4c): the depth fog.  ZFog.c with the 2D layer, as
# the window build has it, the rest of the game stubbed: the Z byte the
# PSMT8H read of the Z copy selects (a CPU model of the GS swizzle), the
# CLUT order, the recording, and on a device quads at known Z fogged
# against a CPU reference (77 without one, after the CPU checks).
add_executable(rd_fog_test test/rd_fog_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/ZFog.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(rd_fog_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(rd_fog_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_fog_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_fog_test PRIVATE ico_render ico_math)

# rd_blur_test (wave 5, R5a): the full-screen effects of staticBlur.c.
# staticBlur.c with the 2D layer and Matrix.c, as the window build has
# them, the rest of the game stubbed: the commands each entry point records,
# then on a device every effect against a CPU model of the same GS
# arithmetic, and 600 frames of the motion blur and aura feedback (77
# without a device, after the recording checks).
ico_include_dirs(sugipon _ico_sugipon_incs)
add_executable(rd_blur_test test/rd_blur_test.c
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/staticBlur.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Matrix.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(rd_blur_test PRIVATE ${_ico_sugipon_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(rd_blur_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_blur_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_blur_test PRIVATE ico_render ico_math)

# rd_water_test (wave 5, R5b): the render-to-texture surfaces.  puddle.c,
# pool.c, queen_barrier_disp.c, waterDot.c and clothAnimation.c with the
# mesh path, the 2D layer and matrixDrive.c, as the window build has them,
# the rest of the game stubbed: the block targets, aliases and camera scopes
# they record, the refraction STs against double-precision references, then
# on a device each surface against a CPU raster of the VU reference (77
# without a device, after the recording checks).
add_executable(rd_water_test test/rd_water_test.c
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/puddle.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/pool.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/waterDot.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/clothAnimation.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/matrixDrive.c"
    "${CMAKE_SOURCE_DIR}/ico2/ito/src/queen_barrier_disp.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Packet.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/RegistPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/MicroCode.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayP2O.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Primitive.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Matrix.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(rd_water_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/render/vu1_ref")
target_compile_definitions(rd_water_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_water_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_water_test PRIVATE ico_render ico_vu1_ref ico_math)
if(WIN32 AND SDL3_FOUND)
    add_custom_command(TARGET rd_water_test POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                               "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:rd_water_test>"
                       VERBATIM)
endif()

# rd_raw_test (wave 5, R5c): the raw packet builders outside seki.
# darkVolume.c, particleEffect.c, lineManager.c and lightning.c with the
# VU1 chain reader (MicroCode.c), Primitive.c, the 2D layer and Matrix.c, as
# the window build has them, the rest of the game stubbed: the packets
# hand-decoded against the recorded commands, then on a device the dark
# volume's count and composite, two lightning blend modes, a particle batch
# (vu1_ref) and a line set against CPU references (77 without a device,
# after the recording checks).
add_executable(rd_raw_test test/rd_raw_test.c
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/darkVolume.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/particleEffect.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/lineManager.c"
    "${CMAKE_SOURCE_DIR}/ico2/ito/src/lightning.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/matrixDrive.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/MicroCode.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Primitive.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Matrix.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(rd_raw_test PRIVATE ${_ico_sugipon_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/render/vu1_ref")
target_compile_definitions(rd_raw_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_raw_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_raw_test PRIVATE ico_render ico_vu1_ref ico_math)

# rd_debug_test (wave 6, R6a): common/src/debug.c on the host, with the 2D
# layer as the window build has them and port/data/sifdev_host.c for host0:,
# the rest of the game stubbed: the debug font's points (the VU1 routine
# debug.c runs on the host), the debug menu and option page drawn in
# developer mode, the font window, the option file round trip; on a device
# a glyph's pixels and a snapshot PNG (77 without one).
add_executable(rd_debug_test test/rd_debug_test.c
    "${CMAKE_SOURCE_DIR}/ico2/common/src/debug.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/data/sifdev_host.c")
ico_include_dirs(common _ico_common_incs)
target_include_directories(rd_debug_test PRIVATE ${_ico_common_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(rd_debug_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_debug_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
# sifdev_host.c opens host0: files through host_fs (ico_widen, ico_mkdir)
target_link_libraries(rd_debug_test PRIVATE ico_render ico_math ico_host_fs)

# rd_mip_test (F2 C1): rdtex_MipChainBytes against the level sum and
# rdtex_BuildMipChain inside it for non-square textures (128x4, 512x2).
# CPU only.
add_executable(rd_mip_test test/rd_mip_test.c)
target_compile_options(rd_mip_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_mip_test PRIVATE ico_render)

# texpack_load_test (texture packs): the DDS and PNG loaders on files built
# in memory (every DDS layout and BC format, truncated files, a BC size
# that is not a multiple of 4; PNG colour types, depths, filters, Adam7,
# tRNS), CPU only.  texpack_pack_files: the same binary on the files of a
# real pack (ICO_TEXPACK_DIR, else build-host/tmp/texpack), 77 without one.
add_executable(texpack_load_test test/texpack_load_test.c)
target_compile_options(texpack_load_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(texpack_load_test PRIVATE ico_render ico_miniz)

if(WIN32 AND SDL3_FOUND)
    foreach(t rd_replay_tool rd_state_test rd_pixel_test rd_layout_test rd_tex_test rd_gsbase_test
              rd_mesh_test rd_shadow_test rd_fog_test rd_blur_test rd_raw_test rd_debug_test
              rd_present_test rd_interp_test rd_mirror_test rd_mip_test rd_perf_test rd_crt_test
              texpack_load_test)
        add_custom_command(TARGET ${t} POST_BUILD
                           COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                   "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${t}>"
                           VERBATIM)
    endforeach()
endif()

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME rd_state COMMAND rd_state_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME rd_pixel COMMAND rd_pixel_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME rd_layout COMMAND rd_layout_test)
    set_tests_properties(rd_layout PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_mip COMMAND rd_mip_test)
    add_test(NAME rd_tex COMMAND rd_tex_test)
    set_tests_properties(rd_tex PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_gsbase COMMAND rd_gsbase_test)
    set_tests_properties(rd_gsbase PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_mesh COMMAND rd_mesh_test "${ICO_DISC_IMAGE}")
    set_tests_properties(rd_mesh PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_shadow COMMAND rd_shadow_test)
    set_tests_properties(rd_shadow PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_fog COMMAND rd_fog_test WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_fog PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_blur COMMAND rd_blur_test WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_blur PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500)
    set_tests_properties(rd_pixel PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP rd_dump)
    # the dump rd_pixel leaves behind, through the tool
    add_test(NAME rd_replay_tool
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool.png" --present 640x480)
    set_tests_properties(rd_replay_tool PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED rd_dump)
    add_test(NAME rd_water COMMAND rd_water_test)
    set_tests_properties(rd_water PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_raw COMMAND rd_raw_test WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_raw PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_debug COMMAND rd_debug_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_debug PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_present COMMAND rd_present_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_present PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_crt COMMAND rd_crt_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_crt PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_filter COMMAND rd_filter_test)
    add_test(NAME rd_interp COMMAND rd_interp_test)
    add_test(NAME rd_mirror COMMAND rd_mirror_test)
    set_tests_properties(rd_mirror PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_perf COMMAND rd_perf_test)
    set_tests_properties(rd_perf PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME texpack_load COMMAND texpack_load_test)
    if(DEFINED ENV{ICO_TEXPACK_DIR})
        set(_ico_texpack_dir "$ENV{ICO_TEXPACK_DIR}")
    else()
        set(_ico_texpack_dir "${CMAKE_SOURCE_DIR}/build-host/tmp/texpack/SCES-50760/replacements")
    endif()
    add_test(NAME texpack_pack_files COMMAND texpack_load_test --pack "${_ico_texpack_dir}")
    set_tests_properties(texpack_pack_files PROPERTIES SKIP_RETURN_CODE 77)
    set(_env "")
    set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
    if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
        set(_env "VK_ADD_LAYER_PATH=${_ico_vvl}" "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                 "ICO_VK_VALIDATION=1")
    endif()
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    if(_env)
        set_tests_properties(rd_water PROPERTIES ENVIRONMENT "${_env}")
        set_tests_properties(rd_pixel rd_replay_tool rd_layout rd_tex rd_gsbase rd_mesh rd_shadow
                             rd_fog rd_blur rd_raw rd_debug rd_present rd_crt rd_interp rd_mirror rd_perf PROPERTIES ENVIRONMENT
                                                                           "${_env}")
    endif()
endif()
