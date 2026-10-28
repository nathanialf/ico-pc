# port/render: the native draw API (rd.h) on the RHI.
#
# ico_render needs the shader table (ico_shaders, DXC) and an RHI backend
# (ico_rhi_vk); without either it is left out with a status message, like
# the other fetched dependencies.

# ico_vu1_ref: CPU references of the five VU1 microprograms, the oracle of port/shaders/test/vu1_test.c.
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

# ico_sheet_ref: font_sheet_ps on the CPU (test/sheet_ref.h),
# the oracle of rd_pixel_test's sheet text and the menu text comparisons.
# Plain C; built in every configuration.
add_library(ico_sheet_ref STATIC test/sheet_ref.c)
target_include_directories(ico_sheet_ref PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/test"
                           "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(ico_sheet_ref PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(ico_sheet_ref PUBLIC m) # floorf
endif()

# ico_texpack_name: the names PCSX2 gives textures
# (texpack_name.h) and the XXH3-64 they are made with (xxh3.c, a
# reimplementation of xxHash's algorithm, BSD-2-Clause, docs/THIRD_PARTY.md).
# CPU only, built in every configuration.
add_library(ico_texpack_name STATIC texpack_name.c xxh3.c)
target_include_directories(ico_texpack_name PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_texpack_name PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# texpack_name_test: XXH3 against xxHash's sanity vectors, the block
# swizzles against PCSX2's column writes, the CLUT orders, the expanded
# bytes, the candidate list and the file names (CPU only).
add_executable(texpack_name_test test/texpack_name_test.c)
target_compile_options(texpack_name_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(texpack_name_test PRIVATE ico_texpack_name)

# ico_texpack_walk: every TIM2 of the disc as the game loads it
# (texpack_walk.h), read through df_pack's IcoDfIndex: texpack_disc_test's
# walk and the texture export's (texpack_export.h).  No render device.
add_library(ico_texpack_walk STATIC texpack_walk.c)
target_include_directories(ico_texpack_walk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_texpack_walk PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_texpack_walk PUBLIC ico_texpack_name ico_port_data)

# texpack_disc_test: every TIM2 of the disc named, written to
# texpack_names.txt; with a PCSX2 replacements folder (ICO_TEXPACK_DIR) the
# pack's files matched against those names.  Skipped (77) without the disc.
add_executable(texpack_disc_test test/texpack_disc_test.c)
target_compile_options(texpack_disc_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(texpack_disc_test PRIVATE ico_texpack_walk)

# ico_bisect_plan: the loss bisect's second-level planner (rd_bisect_plan.h:
# which draws of the lost pass the next step replays).  Pure C, built in
# every configuration; the platform's state file (diag_host.c) and the
# window's driver use it.
add_library(ico_bisect_plan STATIC rd_bisect_plan.c)
target_include_directories(ico_bisect_plan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_bisect_plan PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# the platform's state file of the second level (diag_host.c)
target_link_libraries(ico_platform PUBLIC ico_bisect_plan)

# rd_bisect_plan_test: the planner against oracles (a draw that faults with
# every draw before it found within 12 steps, random verdicts end within 16)
add_executable(rd_bisect_plan_test test/rd_bisect_plan_test.c)
target_compile_options(rd_bisect_plan_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_bisect_plan_test PRIVATE ico_bisect_plan)

# ico_gltf: glTF 2.0 for model packs (gltf.h), the dump's
# .gltf + .bin writer and the .gltf/.glb reader.  CPU only, built in every
# configuration: JSON through ico_json, files through host_fs (UTF-8 paths).
add_library(ico_gltf STATIC gltf.c)
target_include_directories(ico_gltf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_gltf PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_gltf PUBLIC ico_json ico_host_fs)
if(NOT WIN32)
    target_link_libraries(ico_gltf PUBLIC m)
endif()

# gltf_test: write -> read bit-exact (static and skinned), the written JSON,
# a GLB container, a Blender-style file, every rejection (CPU only; its
# files under the build tree).
add_executable(gltf_test test/gltf_test.c)
target_compile_options(gltf_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(gltf_test PRIVATE ico_gltf)

if(ICO_RUN_TESTS)
    # a PCSX2 pack's replacements folder (never committed), for
    # texpack_disc, texpack_e2e and texpack_pack_files; the match is reported
    # only when it exists. The environment variable of the same name, when
    # set at the first configure, is the default.
    set(_ico_texpack_default "${CMAKE_SOURCE_DIR}/build-host/tmp/texpack/SCES-50760/replacements")
    if(DEFINED ENV{ICO_TEXPACK_DIR})
        set(_ico_texpack_default "$ENV{ICO_TEXPACK_DIR}")
    endif()
    set(ICO_TEXPACK_DIR "${_ico_texpack_default}"
        CACHE PATH "A PCSX2 texture pack's replacements folder for the texture pack tests")
    add_test(NAME texpack_name COMMAND texpack_name_test)
    add_test(NAME rd_bisect_plan COMMAND rd_bisect_plan_test)
    add_test(NAME gltf COMMAND gltf_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME texpack_disc COMMAND texpack_disc_test "${ICO_DISC_IMAGE}" "${ICO_TEXPACK_DIR}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(texpack_disc PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP texpack_names)
endif()

if(NOT TARGET ico_shaders OR NOT TARGET ico_rhi_vk)
    message(STATUS "ico: port/render not built (needs ico_shaders and ico_rhi_vk; run "
                   "tools/fetch_deps.sh)")
    return()
endif()

add_library(ico_render STATIC
    rd_alpha_plan.c # the alpha/blend planner: the passes a draw's alpha range can need
    rd_blend_selftest.c # the exact blend's start-up check on the device (issue 89)
    rd_blur.c # staticBlur.c's sprites
    rd_blur_border.c # the post sprites' replay: the reduction's open border, its outer columns
    rd_blur_smooth.c # Motion blur: Smooth, the motion blur's accumulator
    rd_core.c
    rd_crack_fill.c # above 1x: the pixels no draw covered between drawn ones, before the fog
    rd_crt.c # the CRT filter at present time
    rd_date_selftest.c # DATE through the stencil bit: the start-up check on the device
    rd_dst_read.c # the exact GS blend: the destination read
    rd_dump.c
    rd_fog_path.c # how the fog reads the depth: the paths, the probe, the self-test
    rd_frame.c
    rd_fx_plan.c # the frame-size post sprites' plan: fixed-function forms, local read
    rd_interp.c # interpolation between the retained frames
    rd_reshow.c # a between-tick picture the same as the last rendered one's is shown again
    rd_glow_hold.c # the between-tick presents paste the glow of the tick's first present
    rd_join.c
    rd_local_probe.c # the desktop's local reads: the start-up cost probe
    rd_mesh.c
    rd_msaa.c # the MSAA epoch's resolve pass (the covered-majority rule) and its close line
    rd_passring.c # the last passes recorded, for the device-loss report
    rd_profile.c # the device profile: what this device class gets 
    rd_content_hide.c # the content table: stage geometry made for the 4:3 picture
    rd_content_extend.c # the content table's sheets: overlays continued to the frame's edges
    rd_content_chain.c # Natural chains: the chains a stage animation lets down
    modelpack.c # model packs, the index, the glTF conversion, the dump
    rd_perf.c # per-replay performance records
    rd_pixel_plan.c # the VU pixel side at a fractional scale
    rd_pipeline.c
    rd_png.c
    rd_post.c
    rd_present.c
    rd_replay.c
    rd_replay_tiler.c # the replay's tiler passes: DATE through the stencil bit, the blur fold
    rd_replay_vu.c # the replay's VU draws
    rd_vuprepass_selftest.c # the VU vertex pre-pass's start-up check on the device
    rd_window.c
    rd_upload.c # the upload ring and the uploads before a replay's first pass
    rd_screen_planes.c # the screen-prim triangles' planes
    rd_shadow.c
    rd_shadow_one.c # the shadow count resolved in one draw from the stencil view
    rd_shadow_selftest.c # the shadow count's one draw: the start-up check on the device
    rd_tex.c
    rd_water.c # block targets, aliases, camera scopes
    rd_video.c # the FMV picture (port/fmv/rd_video.h)
    texpack_dds.c # texture packs: the DDS and PNG loaders (texpack.h)
    texpack_image.c
    texpack_png.c
    texpack.c) # texture packs, the index, the loader thread, the budget
target_include_directories(ico_render
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(ico_render PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_render PUBLIC ico_rhi_vk ico_shaders ico_vu1_ref ico_texpack_name)
# texpack.c, modelpack.c: the selected disc's pack folder (port/core)
target_link_libraries(ico_render PUBLIC ico_region)
# texture packs: texpack_png.c inflates with miniz's tinfl
target_link_libraries(ico_render PRIVATE ico_miniz)
if(NOT WIN32)
    target_link_libraries(ico_render PUBLIC m)
endif()
# texpack.c: the pack's folders through host_fs (UTF-8 paths, ico_dir_walk)
# and its loader thread on SDL's threads (without SDL the loads run between
# frames instead)
target_link_libraries(ico_render PUBLIC ico_host_fs)
# modelpack.c: the model files through gltf.c
target_link_libraries(ico_render PUBLIC ico_gltf)
if(TARGET SDL3::SDL3-shared)
    target_link_libraries(ico_render PUBLIC SDL3::SDL3-shared)
    set_source_files_properties(texpack.c TARGET_DIRECTORY ico_render
                                PROPERTIES COMPILE_DEFINITIONS "ICO_TEXPACK_THREAD=1")
endif()

# ico_texpack_export: every texture on the disc as PNGs under the runtime
# dump's names (texpack_export.h): the synchronous core and the job on an SDL
# thread (without SDL the job is not offered: texpack_export_begin fails).
add_library(ico_texpack_export STATIC texpack_export.c)
target_include_directories(ico_texpack_export PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_texpack_export PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_texpack_export PUBLIC ico_render ico_texpack_walk ico_host_fs)
# SDL3::SDL3 stands for the shared library where there is one and for the
# static one on the Apple builds (fetch_moltenvk.sh builds SDL_SHARED=OFF), so
# the row works on every window build
if(TARGET SDL3::SDL3)
    target_link_libraries(ico_texpack_export PUBLIC SDL3::SDL3)
    set_source_files_properties(texpack_export.c TARGET_DIRECTORY ico_texpack_export
                                PROPERTIES COMPILE_DEFINITIONS "ICO_TEXPACK_EXPORT_THREAD=1")
endif()

# rd_replay_tool: a dump to a PNG, headless.
# (and a F12 .zip: platform/dump_zip.c reads it)
add_executable(rd_replay_tool tools/rd_replay_tool.c ../platform/dump_zip.c)
target_compile_options(rd_replay_tool PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_replay_tool PRIVATE ../platform)
target_link_libraries(rd_replay_tool PRIVATE ico_render ico_vu1_ref ico_miniz ico_host_fs)

# --- Tests ----------------------------------------------------------------------
# rd_state_test: lists, state leakage, keep frames, recording of the stubbed
# commands, dump round trip of the recording, pipeline enumeration (CPU only)
add_executable(rd_state_test test/rd_state_test.c)
target_compile_options(rd_state_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_state_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_state_test PRIVATE ico_render)

# rd_pixel_test: GS sprite rasterisation, reduction against a CPU reference,
# blend_int feedback over 100 frames, dump -> load -> replay, pipeline count
# (Vulkan; exit 77 without a device). rd_replay_tool renders its dump to a PNG
# through the presenter (the rd_replay_tool test, below).
add_executable(rd_pixel_test test/rd_pixel_test.c)
target_include_directories(rd_pixel_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/shaders/test" "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_pixel_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_pixel_test PRIVATE ico_render ico_sheet_ref)

# rd_native_test: one synthetic game frame (VU triangles, UI and full-frame
# sprites, the DISPLAY reduction) at native widths 512..2560: the centre
# columns equal the 512 render's, the sides continue exactly (a narrower
# frame is the widest one's centre) and no step where the 4:3 columns end;
# corners on pixel boundaries cover the same pixels at every width.
add_executable(rd_native_test test/rd_native_test.c)
target_include_directories(rd_native_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_native_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_native_test PRIVATE ico_render)

# rd_layout_test: the seki layer's host path (GifPacket.c, DisplayList.c,
# DmaPacket.c compiled with ICO_HOST and ICO_RD, as the window build has
# them) fed layout_texture.c's call sequence for a synthetic layout and
# kanban.c (compiled in) for the boot signs' backdrop; the
# recording is checked without a device, one sprite's pixels on one.
add_executable(rd_layout_test test/rd_layout_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/common/src/kanban.c")
ico_include_dirs(seki _ico_seki_incs)
target_include_directories(rd_layout_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/ui"
                           "${CMAKE_SOURCE_DIR}/port/include")
target_compile_definitions(rd_layout_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_layout_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_layout_test PRIVATE ico_render ico_math)

# rd_tex_test: the texture cache, and Texture.c's host path with the three
# files above, fed synthetic TIM2 files; decode, CLUT scroll, cache keys and
# the decoder's binding on the recording, one sprite per format's texels on
# a device (77 without one). ico_options: Texture.c's CLUT recolour
# (port/game/appearance.c), whose config store needs host_config.c
add_executable(rd_tex_test test/rd_tex_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Texture.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(rd_tex_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(rd_tex_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(rd_tex_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(rd_tex_test PRIVATE ico_render ico_math ico_options)

# rd_gsbase_test: GsBase.c's host path with the 2D layer
# (GifPacket.c, DisplayList.c, DmaPacket.c) as the window build has them,
# the rest of the game stubbed: frame lifecycle, field parity, camera
# packing, Z scale and the post passes against CPU references; with
# port/game/photo_view.c, photo mode's camera in the game's
# matrices (photo_mode.c from ico_options).
add_executable(rd_gsbase_test test/rd_gsbase_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GsBase.c"
    "${CMAKE_SOURCE_DIR}/port/game/photo_view.c"
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
# ico_options: GsBase.c's widescreen hook reads the display options,
# whose config store needs host_config.c (above)
target_link_libraries(rd_gsbase_test PRIVATE ico_render ico_math ico_options)

# rd_present_test: the display options.  GsBase.c as
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

# gsb_cull_test: the game's cull against the cameras of the blended
# pictures (GsBase.c gsb_ClipBox) and the windows that scale with
# the picture's width (the water dots, the lines, the shadow volumes).
# GsBase.c as rd_present_test has it, with the display options; synthetic
# cameras a tick apart (moves to 300 units, turns to 30 degrees) at three
# widths: a box an in-between picture shows is never culled.  Records
# only: no device.
add_executable(gsb_cull_test test/gsb_cull_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GsBase.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(gsb_cull_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/shaders/test"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi"
                           "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(gsb_cull_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(gsb_cull_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(gsb_cull_test PRIVATE ico_render ico_math ico_options ico_port_data)

# rd_crt_test: the CRT filter (rd_crt.c, crt.hlsl): its [video]
# options with their config (host_config.c), the modes and overrides, and on
# a device the presents: off is rd_present's bytes, each mode's recorded hash,
# black outside the box, the brightness, the mask's period and its fade.
add_executable(rd_crt_test test/rd_crt_test.c "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(rd_crt_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(rd_crt_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_crt_test PRIVATE ico_render ico_options)

# rd_filter_test: the draw filter (rd.h rd_set_draw_filter) on
# synthetic keys, recorded without a device.
add_executable(rd_filter_test test/rd_filter_test.c)
target_compile_options(rd_filter_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_filter_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_filter_test PRIVATE ico_render)

# rd_still_test: the still re-present (rd_reshow.c): the frame hash that is the
# proof a between-tick picture repeats the tick's last rendered one, and the
# decision on synthetic frames recorded without a device.
add_executable(rd_still_test test/rd_still_test.c)
target_compile_options(rd_still_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_still_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_still_test PRIVATE ico_render)

# rd_interp_test: the presenter's interpolation between the
# retained frames (rd_interp.c): keyed draws blend by alpha, the snap rules,
# the feedback FIX at dt; on a device the replays at alpha 0 and 1 against
# the frames' own, the half-way pixels, rd_present.
add_executable(rd_interp_test test/rd_interp_test.c)
target_compile_options(rd_interp_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_interp_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_interp_test PRIVATE ico_render)

# rd_mirror_test: the mirror mode: the present is the exact
# flip with it on (Original, Enhanced, interpolated), UI prims land on the
# same output pixels (the replay's flip cancels the present's), SCENE's UI
# flipped about the centre.
add_executable(rd_mirror_test test/rd_mirror_test.c)
target_compile_options(rd_mirror_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_mirror_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_mirror_test PRIVATE ico_render)

# rd_msaa_test: the anti-aliasing's MSAA epoch: MSAA4 against no
# anti-aliasing on a synthetic scene (identical away from edges, integer
# sprites identical), a DATE draw closing the epoch, the fog per sample and
# through the resolved depth, the Original preset unchanged; 77 without a
# device that takes 4 samples.
add_executable(rd_msaa_test test/rd_msaa_test.c)
target_compile_options(rd_msaa_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_msaa_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_msaa_test PRIVATE ico_render)

# rd_display_change_test: a display change while the game runs (rd_core.c
# rd_begin_frame, rd_pipeline.c rd__display_pipelines): from Original with
# the start-up set to Enhanced at a fractional scale and back, no replay makes
# a pipeline on first use (the change made the new state's), and back at
# Original the replay's counts and DISPLAY are a fresh start's, byte for byte.
add_executable(rd_display_change_test test/rd_display_change_test.c)
target_compile_options(rd_display_change_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_display_change_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_display_change_test PRIVATE ico_render)

# rd_perf_test: a synthetic game-shaped frame replayed 200
# times and recorded 200 times: no resource is created or destroyed and no
# texture or static mesh uploaded again in steady state, and the CPU time of
# a replay stays under 20 ms on lavapipe; with --dump FILE it times a dump's
# replay phase by phase.
add_executable(rd_perf_test test/rd_perf_test.c)
target_compile_options(rd_perf_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_perf_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_perf_test PRIVATE ico_render)

# rd_alpha_plan_test, rd_fx_frame_test, rd_vu_frac_test: the pixel diet's
# planners (rd_alpha_plan.c, rd_fx_plan.c, rd_pixel_plan.c), CPU only: the
# alpha range's truth table and the blend identities, the frame-size fold and
# the premultiplied identity, the planes rule's truth table and the VS->PS
# prefix match.
foreach(_t rd_alpha_plan rd_fx_frame rd_vu_frac)
    add_executable(${_t}_test test/${_t}_test.c)
    target_compile_options(${_t}_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_include_directories(${_t}_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi"
                               "${CMAKE_SOURCE_DIR}/port/shaders")
    target_link_libraries(${_t}_test PRIVATE ico_render)
endforeach()

# rd_boot_present_test: a saved start-up (rd-NNNNN.rddump, dump_every=1)
# through the window's present path in order, several presents a tick, each
# read back: the frames before the first sign must present black, and draw
# no scenery (keep frames).  The dump folder is ICO_BOOT_DUMP_DIR (or the
# ICO_BOOT_DUMPS environment variable); skipped without it, as in CI.  A
# start-up dumped before the card check kept its frames to the first sign
# (v0.4.5 and older: frames 118 to 120 draw stage 1) needs ICO_BOOT_DUMP_OLD
# (the test's ICO_BOOT_OLD=1).
set(ICO_BOOT_DUMP_DIR "" CACHE PATH "A start-up's frame dumps for rd_boot_present_test")
option(ICO_BOOT_DUMP_OLD "ICO_BOOT_DUMP_DIR is a start-up from v0.4.5 or older" OFF)
add_executable(rd_boot_present_test test/rd_boot_present_test.c)
target_compile_options(rd_boot_present_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_boot_present_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_boot_present_test PRIVATE ico_render)

# rd_mesh_test: the mesh path.  Packet.c, RegistPacket.c,
# MicroCode.c, DisplayP2O.c and Primitive.c with the 2D layer and Matrix.c,
# as the window build has them, fed synthetic p2o-decoded models, a grid and
# a particle batch: the meshes, the recorded draws and VuCB, the GS state;
# on a device each case against the VU reference drawn as sprites (77
# without one); the title logo's strips Packet.c draws in another order,
# their geometry from the disc image when there is one.
add_executable(rd_mesh_test test/rd_mesh_test.c
    "${CMAKE_SOURCE_DIR}/ico2/common/src/PObj.c"
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
# (df_pack.h); without it that part is skipped.  PObj.c: the boxes case loads
# every model on the disc through the game's own loader
target_link_libraries(rd_mesh_test PRIVATE ico_render ico_vu1_ref ico_math ico_port_data)

# modelpack_test: model packs.  The same game files as
# rd_mesh_test (test/mesh_env.h stubs the rest): the folder index on a fake
# tree, the dump of a strip stream read back and converted (the triangles
# the same), the bone packing, a cluster packet from vu_models.h dumped,
# read back and drawn in the original's place as RDC_SKINNED; on a device
# the original and the round-tripped replacement rendered alike (77
# without one, after the CPU checks).  Its files under the build tree.
add_executable(modelpack_test test/modelpack_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Packet.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/RegistPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/MicroCode.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayP2O.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Primitive.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Matrix.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(modelpack_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/render/vu1_ref"
                           "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(modelpack_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(modelpack_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(modelpack_test PRIVATE ico_render ico_vu1_ref ico_math)

# rd_shadow_test: the shadow count on the stencil.  Shadow.c
# with the 2D layer and Matrix.c, as the window build has them, the rest of
# the game stubbed: the count against the wrapped colour sum, the blur chain
# and the composites against CPU references, the DATE receiver mask, and
# the commands Shadow.c's host path records (77 without a device, after the
# recording checks).
add_executable(rd_shadow_test test/rd_shadow_test.c test/above1x_ref.c
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

# rd_fog_test: the depth fog.  ZFog.c with the 2D layer, as
# the window build has it, the rest of the game stubbed: the Z byte the
# PSMT8H read of the Z copy selects (a CPU model of the GS swizzle), the
# CLUT order, the recording, and on a device quads at known Z fogged
# against a CPU reference (77 without one, after the CPU checks).
add_executable(rd_fog_test test/rd_fog_test.c test/above1x_ref.c
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

# rd_fog_selftest_test: the fog's self-test and probe (rd_fog_path.c) on a
# wide, scaled scene: the path it picks, a fog frame recorded through the
# renderer's calls fogged and probed, and both again at a new scene size
# (the log lines are matched by test/rd_fog_selftest.cmake).
add_executable(rd_fog_selftest_test test/rd_fog_selftest_test.c)
target_compile_options(rd_fog_selftest_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_fog_selftest_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_fog_selftest_test PRIVATE ico_render)

# rd_local_probe_test: the desktop's local-read probe's verdict
# (rd_local_probe.c rd__local_probe_keep) on synthetic times, no device.
add_executable(rd_local_probe_test test/rd_local_probe_test.c)
target_compile_options(rd_local_probe_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_local_probe_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_local_probe_test PRIVATE ico_render)

# rd_crack_test: the crack filler above 1x (rd_crack_fill.c): a 1/16 GS
# pixel crack between two triangles filled before the fog at 2x, 4x and a
# fractional scale, a slit wider than the reach kept, nothing else changed,
# nothing drawn at 1x, under Original, under MSAA or switched off (the log
# lines are matched by test/rd_crack.cmake).
add_executable(rd_crack_test test/rd_crack_test.c)
target_compile_options(rd_crack_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_crack_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_crack_test PRIVATE ico_render)

# rd_blend_selftest_test: the exact blend's self-test (rd_blend_selftest.c)
# and the attachment read's Qualcomm rule (vk_device.c) on a wide, scaled
# scene: the mode left in force, the forest's tint under it, the targets
# without the attachment read where it is off, and the self-test again at a
# new scene size (the log lines are matched by test/rd_blend_selftest.cmake).
add_executable(rd_blend_selftest_test test/rd_blend_selftest_test.c)
target_compile_options(rd_blend_selftest_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_blend_selftest_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_blend_selftest_test PRIVATE ico_render)

# rd_vuprepass_selftest_test: the VU vertex pre-pass's self-test
# (rd_vuprepass_selftest.c) in either VU shader variant: the pre-pass left
# on or off, the summary, the reachable set made after it, SCENE left to its
# clear (the log line is matched by test/rd_vuprepass_selftest.cmake).
add_executable(rd_vuprepass_selftest_test test/rd_vuprepass_selftest_test.c)
target_compile_options(rd_vuprepass_selftest_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_PORT_WARNINGS})
target_include_directories(rd_vuprepass_selftest_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_vuprepass_selftest_test PRIVATE ico_render)

# fog_lut_test: the depth fog's game side alone, ZFog.c with the renderer
# calls stubbed: the fog tables and sprites of three player dumps byte for
# byte, and a sweep of the stage parameters against the integer formula.
# tools/fog_arm64_diff.sh builds the same program for arm64 (the Android
# build's compiler and flags), runs it under qemu-aarch64 and compares its
# output with this build's (77 without an NDK or qemu).
add_executable(fog_lut_test test/fog_lut_test.c "${CMAKE_SOURCE_DIR}/ico2/seki/src/ZFog.c")
target_include_directories(fog_lut_test PRIVATE ${_ico_seki_incs} "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(fog_lut_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(fog_lut_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})

# rd_blur_test: the full-screen effects of staticBlur.c.
# staticBlur.c with the 2D layer and Matrix.c, as the window build has
# them, the rest of the game stubbed: the commands each entry point records,
# then on a device every effect against a CPU model of the same GS
# arithmetic, and 600 frames of the motion blur and aura feedback, and
# the aura at Enhanced 4x against the model at scale with the above-1x
# kernels (test/above1x_ref.c) (77 without a device, after the recording
# checks).
ico_include_dirs(sugipon _ico_sugipon_incs)
add_executable(rd_blur_test test/rd_blur_test.c test/above1x_ref.c
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

# rd_water_test: the render-to-texture surfaces.  puddle.c,
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

# rd_raw_test: the raw packet builders outside seki.
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

# rd_debug_test: common/src/debug.c on the host, with the 2D
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

# rd_passring_test: the pass ring the device-loss report prints
# (rd_passring.c): order, wrap, the replay table, the replays printed, the line
# format; pure.
add_executable(rd_passring_test test/rd_passring_test.c rd_passring.c)
target_include_directories(rd_passring_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_passring_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# rd_mip_test: rdtex_mip_chain_bytes against the level sum and
# rdtex_build_mip_chain inside it for non-square textures (128x4, 512x2).
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

# texpack_test: the pack's index on a folder tree of fake
# files (both roots, the tolerant layouts, duplicates, -mip and region
# files, malformed names) and the loader thread against a fake texture
# cache (request, pump, a changed generation dropped, the RAM cache, the
# budget, shutdown joining).  texpack.c on its own with the real names
# (ico_texpack_name); the cache and the file loaders are the test's.
add_executable(texpack_test test/texpack_test.c texpack.c)
target_include_directories(texpack_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(texpack_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(texpack_test PRIVATE ico_host_fs ico_texpack_name ico_region)
if(TARGET SDL3::SDL3-shared)
    target_link_libraries(texpack_test PRIVATE SDL3::SDL3-shared)
    target_compile_definitions(texpack_test PRIVATE ICO_TEXPACK_THREAD=1)
endif()

# texpack_e2e_test: the disc's names
# (texpack_disc_test's texpack_names.txt) looked up in the index texpack_init
# builds from a real pack's folder, in the hook's order, and every file
# reached read with the real loaders.  77 without the disc's names or the
# pack (build-host/tmp/texpack, never committed).
add_executable(texpack_e2e_test test/texpack_e2e_test.c)
target_compile_options(texpack_e2e_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(texpack_e2e_test PRIVATE ico_render)

# texpack_export_test: the texture export's core on TIM2 files it makes
# (the names, the PNG sizes, a file already there, the refusals, duplicates,
# a second run, a stopped one, a dry one, the manifest); with --disc a dry
# export of the disc against texpack_disc_test's texpack_names.txt.
add_executable(texpack_export_test test/texpack_export_test.c)
target_compile_options(texpack_export_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(texpack_export_test PRIVATE ico_texpack_export)

# rd_stream_audit_test: the VU stream bounds audit (docs/port/RENDER.md, "The
# stream bounds audit"): every stream index the VU vertex shaders can read for
# every VU draw of the dumps, computed on the CPU and checked against the
# draw's stream; a self-test (a stream one vertex short fails) always runs.
# The dumps are machine-local (tools/native_frame_dumps.txt, the boot
# corpus's title frame): 77 when none is there.
add_executable(rd_stream_audit_test test/rd_stream_audit_test.c ../platform/dump_zip.c)
target_compile_options(rd_stream_audit_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_stream_audit_test PRIVATE ../platform
                           "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_stream_audit_test PRIVATE ico_render ico_vu1_ref ico_miniz ico_host_fs)

# Windows: SDL3.dll beside the programs
ico_copy_sdl3_dll(texpack_test texpack_e2e_test texpack_export_test rd_replay_tool rd_state_test rd_pixel_test
                  rd_layout_test rd_tex_test rd_gsbase_test rd_mesh_test modelpack_test
                  rd_shadow_test rd_fog_test rd_fog_selftest_test rd_blend_selftest_test
                  rd_vuprepass_selftest_test rd_blur_test rd_water_test rd_raw_test rd_debug_test
                  rd_present_test rd_filter_test rd_interp_test rd_still_test rd_mirror_test rd_msaa_test rd_mip_test
                  rd_perf_test rd_display_change_test rd_boot_present_test rd_crt_test
                  texpack_load_test gsb_cull_test
                  rd_native_test rd_passring_test rd_stream_audit_test rd_alpha_plan_test
                  rd_fx_frame_test rd_vu_frac_test rd_crack_test rd_local_probe_test)

if(ICO_RUN_TESTS)
    add_test(NAME rd_state COMMAND rd_state_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME rd_pixel COMMAND rd_pixel_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME rd_native COMMAND rd_native_test)
    set_tests_properties(rd_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500)
    add_test(NAME rd_layout COMMAND rd_layout_test)
    set_tests_properties(rd_layout PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_mip COMMAND rd_mip_test)
    add_test(NAME rd_passring COMMAND rd_passring_test)
    add_test(NAME rd_stream_audit
             COMMAND rd_stream_audit_test --list "${CMAKE_SOURCE_DIR}/tools/native_frame_dumps.txt"
                     "${CMAKE_SOURCE_DIR}/build-host/tmp/corpus/dumps/boot/rd-00200.rddump"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_stream_audit PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
    add_test(NAME rd_tex COMMAND rd_tex_test)
    add_test(NAME texpack COMMAND texpack_test "${CMAKE_CURRENT_BINARY_DIR}")
    get_filename_component(_ico_texpack_textures "${ICO_TEXPACK_DIR}/../.." ABSOLUTE)
    add_test(NAME texpack_e2e
             COMMAND texpack_e2e_test "${CMAKE_CURRENT_BINARY_DIR}/texpack_names.txt"
                     "${_ico_texpack_textures}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(texpack_e2e PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED texpack_names)
    add_test(NAME texpack_export COMMAND texpack_export_test "${CMAKE_CURRENT_BINARY_DIR}")
    # a dry export of the disc names what texpack_disc wrote: 77 without the
    # disc or the names
    add_test(NAME texpack_export_disc
             COMMAND texpack_export_test --disc "${ICO_DISC_IMAGE}"
                     "${CMAKE_CURRENT_BINARY_DIR}/texpack_names.txt" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(texpack_export_disc PROPERTIES SKIP_RETURN_CODE 77
                         FIXTURES_REQUIRED texpack_names)
    set_tests_properties(rd_tex PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_gsbase COMMAND rd_gsbase_test)
    set_tests_properties(rd_gsbase PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME gsb_cull COMMAND gsb_cull_test)
    # st18a's parts from the disc against the cameras about the dump i76's:
    # 77 without the disc image
    add_test(NAME gsb_cull_stage COMMAND gsb_cull_test --stage "${ICO_DISC_IMAGE}")
    set_tests_properties(gsb_cull_stage PROPERTIES SKIP_RETURN_CODE 77)
    # 50 s of it is the disc's models, in four slices: four shards (port/test/ico_shard.h)
    ico_add_sharded_test(rd_mesh 4 PROGRAM rd_mesh_test ARGS "${ICO_DISC_IMAGE}"
                         PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME modelpack COMMAND modelpack_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(modelpack PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_shadow COMMAND rd_shadow_test)
    set_tests_properties(rd_shadow PROPERTIES SKIP_RETURN_CODE 77)
    # the shadow composites' two kernels above 1x (rd_shadow_test.c case x):
    # the same frames at scale 2 with one bilinear tap (the default) and with
    # the B-spline ([dev] shadow_bspline), SCENE equal byte for byte but
    # within N texels of a shadow edge (the kernels' joint support)
    add_test(NAME rd_shadow_edges COMMAND rd_shadow_test edges)
    set_tests_properties(rd_shadow_edges PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_fog COMMAND rd_fog_test WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_fog PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME fog_lut COMMAND fog_lut_test)
    if(CMAKE_HOST_UNIX AND NOT CMAKE_CROSSCOMPILING)
        find_program(ICO_QEMU_AARCH64 qemu-aarch64)
        add_test(NAME fog_lut_arm64
                 COMMAND "${CMAKE_SOURCE_DIR}/tools/fog_arm64_diff.sh" $<TARGET_FILE:fog_lut_test>
                         "${CMAKE_CURRENT_BINARY_DIR}/fog-arm64")
        set_tests_properties(fog_lut_arm64 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
        if(ICO_QEMU_AARCH64)
            set_tests_properties(fog_lut_arm64 PROPERTIES ENVIRONMENT "QEMU=${ICO_QEMU_AARCH64}")
        endif()
    endif()
    # 255 s in one process: four shards of its cases (port/test/ico_shard.h)
    ico_add_sharded_test(rd_blur 4 PROGRAM rd_blur_test
                         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                         PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500)
    set_tests_properties(rd_pixel PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP rd_dump)
    # the dump rd_pixel leaves behind, through the tool
    add_test(NAME rd_replay_tool
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool.png" --present 640x480)
    set_tests_properties(rd_replay_tool PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED rd_dump)
    # Downsampling (issue 149): 2x DISPLAY box-averaged to its GS size (the
    # dump's 512x512 GS frame at half height) before the present, named by --stats
    add_test(NAME rd_replay_tool_downsample
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_downsample.png" --present 640x480
                     --enhanced --resolution 2x --downsample --stats)
    set_tests_properties(rd_replay_tool_downsample PROPERTIES
                         PASS_REGULAR_EXPRESSION "present: downsample 1024x512 -> 512x256, window reduce off"
                         SKIP_RETURN_CODE 77 FIXTURES_REQUIRED rd_dump)
    # the same dump listed without a device (--list --no-device)
    add_test(NAME rd_replay_tool_list
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_list.cmake")
    set_tests_properties(rd_replay_tool_list PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                        FIXTURES_REQUIRED rd_dump)
    # --pass-dump (every target and the present from one run), the unknown
    # --target and the wider-than-recorded --aspect errors
    add_test(NAME rd_replay_tool_pass_dump
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_pass_dump"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_pass_dump.cmake")
    set_tests_properties(rd_replay_tool_pass_dump PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             FIXTURES_REQUIRED rd_dump TIMEOUT 300)
    # --loss-bisect (the frame one pass longer each time, clean on this
    # device) and --passes (the pass window: the whole frame's window draws
    # the plain picture; one pass stops the walk at the next)
    add_test(NAME rd_replay_tool_loss_bisect
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_loss_bisect"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_loss_bisect.cmake")
    set_tests_properties(rd_replay_tool_loss_bisect PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                               FIXTURES_REQUIRED rd_dump TIMEOUT 300)
    # ICO_RD_PASS_SPLIT=8 (a render pass ended and begun again after every 8
    # VU draws) draws the plain replay's DISPLAY picture byte for byte
    add_test(NAME rd_replay_tool_pass_split
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_pass_split"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_pass_split.cmake")
    set_tests_properties(rd_replay_tool_pass_split PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                              FIXTURES_REQUIRED rd_dump TIMEOUT 300)
    # the storage pass merge (issue 103): under ICO_RD_BLEND=storage the
    # replay draws ICO_RD_PASS_MERGE=0's DISPLAY picture byte for byte, at 1x
    # and 2x, with no more pass switches; skips without the storage read
    # (lavapipe has no interlock)
    add_test(NAME rd_replay_tool_pass_merge
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_pass_merge"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_pass_merge.cmake")
    set_tests_properties(rd_replay_tool_pass_merge PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                              FIXTURES_REQUIRED rd_dump TIMEOUT 300)
    # the staticBlur sprites' snapshot swap (issue 103): the copy's DISPLAY
    # picture byte for byte (ICO_RD_SNAP_SWAP=0), each blend path, 1x and 2x
    add_test(NAME rd_replay_tool_snap_swap
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_snap_swap"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_snap_swap.cmake")
    set_tests_properties(rd_replay_tool_snap_swap PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             FIXTURES_REQUIRED rd_dump TIMEOUT 600)
    # every pipeline a replay asks for is in the start-up set (rd_replay_tool --keys): each
    # dump of ICO_DUMPS at 1x, --enhanced 2x and --enhanced 2340x1080, on the fake phone
    # limits and with the tile-based GPU's paths forced; skips without ICO_DUMPS or a device.
    # The union of the asked keys is left in rd_replay_tool_keys/keys-<config>-<size>.txt
    add_test(NAME rd_replay_tool_keys
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_keys"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_keys.cmake")
    set_tests_properties(rd_replay_tool_keys PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                        TIMEOUT 3600)
    # the native frame gates (tools/native_frame_check.py, docs/port/TESTING.md): gate-a
    # re-renders the dump list ICO_DUMPS names and compares with
    # docs/native_frame_baseline.json; gate-b checks the aspect sweeps ICO_SWEEPS names.
    # Both skip (77) without the variable, the dumps, a device or Pillow; the selftest
    # of the gate-b logic always runs.
    if(Python3_FOUND)
        set(ICO_NFC "${CMAKE_SOURCE_DIR}/tools/native_frame_check.py")
        add_test(NAME native_frame_gate_a
                 COMMAND "${Python3_EXECUTABLE}" -I "${ICO_NFC}" gate-a --ctest
                         --tool $<TARGET_FILE:rd_replay_tool>)
        add_test(NAME native_frame_gate_b
                 COMMAND "${Python3_EXECUTABLE}" -I "${ICO_NFC}" gate-b --ctest)
        add_test(NAME native_frame_check_selftest
                 COMMAND "${Python3_EXECUTABLE}" -I "${ICO_NFC}" selftest)
        set_tests_properties(native_frame_gate_a native_frame_gate_b native_frame_check_selftest
                             PROPERTIES SKIP_RETURN_CODE 77)
        set_tests_properties(native_frame_gate_a PROPERTIES TIMEOUT 3600)
        # the regression renders (tools/coverage_check.py, tools/coverage_corpus.txt): every
        # corpus dump at its own aspect, per-mesh draw counts and the pixel change against the
        # previous run's pictures in tools/.coverage.  Seed the baseline after a build with
        #   python3 -I tools/coverage_check.py seed --tool <rd_replay_tool>
        # Skips (77) when the dumps, the tool or Pillow are absent.
        set(ICO_COV "${CMAKE_SOURCE_DIR}/tools/coverage_check.py")
        add_test(NAME coverage_corpus
                 COMMAND "${Python3_EXECUTABLE}" -I "${ICO_COV}" check --ctest
                         --tool $<TARGET_FILE:rd_replay_tool>)
        add_test(NAME coverage_check_selftest
                 COMMAND "${Python3_EXECUTABLE}" -I "${ICO_COV}" selftest)
        set_tests_properties(coverage_corpus coverage_check_selftest PROPERTIES SKIP_RETURN_CODE 77)
        set_tests_properties(coverage_corpus PROPERTIES TIMEOUT 3600)
    endif()
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
    add_test(NAME rd_still COMMAND rd_still_test)
    add_test(NAME rd_mirror COMMAND rd_mirror_test)
    set_tests_properties(rd_mirror PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_msaa COMMAND rd_msaa_test)
    set_tests_properties(rd_msaa PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_perf COMMAND rd_perf_test)
    set_tests_properties(rd_perf PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_display_change COMMAND rd_display_change_test)
    set_tests_properties(rd_display_change PROPERTIES SKIP_RETURN_CODE 77)
    foreach(_t rd_alpha_plan rd_fx_frame rd_vu_frac)
        add_test(NAME ${_t} COMMAND ${_t}_test)
        set_tests_properties(${_t} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    add_test(NAME rd_boot_present COMMAND rd_boot_present_test "${ICO_BOOT_DUMP_DIR}")
    set_tests_properties(rd_boot_present PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500)
    if(ICO_BOOT_DUMP_OLD)
        set_tests_properties(rd_boot_present PROPERTIES ENVIRONMENT "ICO_BOOT_OLD=1")
    endif()
    add_test(NAME texpack_load COMMAND texpack_load_test)
    add_test(NAME texpack_pack_files COMMAND texpack_load_test --pack "${ICO_TEXPACK_DIR}")
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
        set_tests_properties(rd_pixel rd_replay_tool rd_layout rd_tex rd_gsbase ${rd_mesh_SHARDS} modelpack
                             rd_shadow rd_shadow_edges rd_fog rd_raw rd_debug rd_present rd_crt rd_interp rd_mirror rd_msaa rd_perf rd_display_change rd_native PROPERTIES ENVIRONMENT
                                                                           "${_env}")
        set_tests_properties(${rd_blur_SHARDS} PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # the same tests and expectations in the two-pass blend
    # fallback (ICO_RD_NO_DUAL=1: lavapipe has dualSrcBlend, so it is forced)
    set(_env_nodual ${_env} "ICO_RD_NO_DUAL=1")
    set(_nd "${CMAKE_CURRENT_BINARY_DIR}/nodual") # their files apart from the one-pass runs
    file(MAKE_DIRECTORY "${_nd}")
    add_test(NAME rd_pixel_nodual COMMAND rd_pixel_test "${_nd}")
    add_test(NAME rd_present_nodual COMMAND rd_present_test "${_nd}")
    add_test(NAME rd_fog_nodual COMMAND rd_fog_test)
    add_test(NAME rd_shadow_nodual COMMAND rd_shadow_test)
    add_test(NAME rd_water_nodual COMMAND rd_water_test)
    add_test(NAME rd_raw_nodual COMMAND rd_raw_test)
    add_test(NAME rd_mirror_nodual COMMAND rd_mirror_test)
    add_test(NAME rd_interp_nodual COMMAND rd_interp_test)
    add_test(NAME rd_msaa_nodual COMMAND rd_msaa_test)
    set_tests_properties(rd_pixel_nodual rd_present_nodual rd_fog_nodual rd_shadow_nodual
                         rd_water_nodual rd_raw_nodual rd_mirror_nodual rd_interp_nodual
                         rd_msaa_nodual
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_nd}"
                                    ENVIRONMENT "${_env_nodual}")
    # the exact GS blend (rd_dst_read.c) by each path: lavapipe has both
    # destination reads and takes the attachment read by default, so the
    # storage path (fragment shader interlock, the Deck's and the desktop
    # drivers') and the fixed-function blend (devices without either) are
    # forced; the fixed-function runs also compare rd_present's and rd_crt's
    # recorded hashes, which are that blend's
    set(_env_blend_storage ${_env} "ICO_RD_BLEND=storage")
    set(_env_blend_fixed ${_env} "ICO_RD_BLEND=fixed")
    set(_bs "${CMAKE_CURRENT_BINARY_DIR}/blend_storage")
    set(_bf "${CMAKE_CURRENT_BINARY_DIR}/blend_fixed")
    file(MAKE_DIRECTORY "${_bs}" "${_bf}")
    add_test(NAME rd_pixel_storage COMMAND rd_pixel_test "${_bs}")
    add_test(NAME rd_fog_storage COMMAND rd_fog_test)
    add_test(NAME rd_present_storage COMMAND rd_present_test "${_bs}")
    set_tests_properties(rd_pixel_storage rd_fog_storage rd_present_storage
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_bs}"
                                    ENVIRONMENT "${_env_blend_storage}")
    add_test(NAME rd_pixel_fixed COMMAND rd_pixel_test "${_bf}")
    add_test(NAME rd_fog_fixed COMMAND rd_fog_test)
    add_test(NAME rd_present_fixed COMMAND rd_present_test "${_bf}")
    add_test(NAME rd_crt_fixed COMMAND rd_crt_test "${_bf}")
    set_tests_properties(rd_pixel_fixed rd_fog_fixed rd_present_fixed rd_crt_fixed
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_bf}"
                                    ENVIRONMENT "${_env_blend_fixed}")
    # the fog on the 24-bit depth buffer a phone GPU without a sampled
    # D32S8 gets (ICO_VK_FAKE_D24S8=1, port/rhi/vk/vk_device.c): the same
    # pixels as on D32S8, one-pass and two-pass blends
    # (each in its own directory: the test writes its dump file there)
    # (started with the depth read in place, ICO_RD_DEPTH_COPY=0, so the
    # test's compare against the depth copy D24S8 takes by default runs on
    # the 24-bit depth too)
    set(_env_d24s8 ${_env} "ICO_VK_FAKE_D24S8=1" "ICO_RD_DEPTH_COPY=0")
    set(_env_d24s8_nodual ${_env_d24s8} "ICO_RD_NO_DUAL=1")
    set(_d24 "${CMAKE_CURRENT_BINARY_DIR}/d24s8")
    set(_d24nd "${CMAKE_CURRENT_BINARY_DIR}/d24s8_nodual")
    file(MAKE_DIRECTORY "${_d24}" "${_d24nd}")
    add_test(NAME rd_fog_d24s8 COMMAND rd_fog_test)
    add_test(NAME rd_fog_nodual_d24s8 COMMAND rd_fog_test)
    set_tests_properties(rd_fog_d24s8 PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_d24}"
                                                 ENVIRONMENT "${_env_d24s8}")
    set_tests_properties(rd_fog_nodual_d24s8
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_d24nd}"
                                    ENVIRONMENT "${_env_d24s8_nodual}")
    # the fog and the effects depth on a copy of the scene depth, with the
    # stencil kept by every pass (ICO_RD_DEPTH_COPY=1: what a tile-based
    # phone GPU and D24S8 get, rd_core.c rd__sampled_depth): the same
    # expectations
    set(_env_dcopy ${_env} "ICO_RD_DEPTH_COPY=1")
    set(_dc "${CMAKE_CURRENT_BINARY_DIR}/depth_copy")
    file(MAKE_DIRECTORY "${_dc}")
    add_test(NAME rd_fog_depth_copy COMMAND rd_fog_test)
    add_test(NAME rd_present_depth_copy COMMAND rd_present_test "${_dc}")
    set_tests_properties(rd_fog_depth_copy rd_present_depth_copy
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_dc}"
                                    ENVIRONMENT "${_env_dcopy}")
    # the crack filler above 1x (rd_crack_fill.c): rd_crack_test at 2x, 4x,
    # the window's 1800x1080 (2.8125 x 2.41), 1x, Original, MSAA4, switched
    # off, on the depth copy, the buffer path, D24S8 and the two-pass
    # blend; each run's state line (rd: crack fill: ...) matched; and the
    # replay tool's --no-crack-fill and --stats line on rd_pixel's dump
    set(_crk "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    set(_crk_on "rd: crack fill: on above 1x \\(scale")
    set(_crk_tail "texels: a run of texels no draw covered, or drawn over 1/64 of the depth behind both its ends, with a texel in front at each end takes the nearer end\\). \\[dev\\] crack_fill = false turns it off")
    set(_crk_one "rd: crack fill: the reach-1 shader \\(crack_fill1_ps: the four neighbours first\\) \\(scale 2x2, reach 1x1 texels\\)")
    foreach(_c "2|2x2, reach 1x1" "4|4x4, reach 2x2" "frac|2.81[23]x2.411, reach 2x2")
        string(REPLACE "|" ";" _cl "${_c}")
        list(GET _cl 0 _a)
        list(GET _cl 1 _x)
        set(_n rd_crack_${_a})
        if(_a STREQUAL "2")
            set(_n rd_crack)
        elseif(_a STREQUAL "4")
            set(_n rd_crack_4x)
        endif()
        # a reach of 1x1 draws crack_fill1_ps (and the test replays the general shader on the
        # same frame and compares every byte)
        set(_e2 "")
        if(_x MATCHES "reach 1x1")
            set(_e2 "${_crk_one}")
        endif()
        add_test(NAME ${_n}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> -DARGS=${_a}
                         "-DEXPECT=${_crk_on} ${_x} ${_crk_tail}" "-DEXPECT2=${_e2}" -P "${_crk}")
    endforeach()
    # over a backdrop (a far plane writing Z first): the cracks hold its
    # depth and colour and are filled by the discontinuity form
    foreach(_c "2|2x2, reach 1x1|rd_crack_backdrop" "4|4x4, reach 2x2|rd_crack_backdrop_4x"
               "frac|2.81[23]x2.411, reach 2x2|rd_crack_backdrop_frac")
        string(REPLACE "|" ";" _cl "${_c}")
        list(GET _cl 0 _a)
        list(GET _cl 1 _x)
        list(GET _cl 2 _n)
        set(_e2 "")
        if(_x MATCHES "reach 1x1")
            set(_e2 "${_crk_one}")
        endif()
        add_test(NAME ${_n}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=${_a}|backdrop"
                         "-DEXPECT=${_crk_on} ${_x} ${_crk_tail}" "-DEXPECT2=${_e2}" -P "${_crk}")
    endforeach()
    add_test(NAME rd_crack_1x
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> -DARGS=1
                     "-DEXPECT=rd: crack fill: on above 1x, not drawn at scale 1x1 \\(the GS's own grid\\)"
                     -P "${_crk}")
    add_test(NAME rd_crack_original
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> -DARGS=original
                     "-DEXPECT=rd: crack fill: on above 1x, not drawn under the Original preset"
                     -P "${_crk}")
    add_test(NAME rd_crack_msaa
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|msaa"
                     "-DEXPECT=rd: crack fill: not drawn on SCENE at scale 2x2: the MSAA epoch is still open at the fog \\(a frame without a shadow: the fog may read the multisampled scene; the filler runs after the MSAA resolve"
                     -P "${_crk}")
    add_test(NAME rd_crack_off
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> -DARGS=2
                     "-DEXPECT=rd: crack fill: off \\(\\[dev\\] crack_fill = false, ICO_RD_CRACK_FILL=0\\)"
                     "-DDENY=rd: crack fill: on above 1x \\(" -P "${_crk}")
    foreach(_v depth_copy buffer d24s8 nodual)
        add_test(NAME rd_crack_${_v}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> -DARGS=2
                         "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail}" -P "${_crk}")
    endforeach()
    set_tests_properties(rd_crack rd_crack_4x rd_crack_frac rd_crack_backdrop rd_crack_backdrop_4x
                         rd_crack_backdrop_frac rd_crack_1x rd_crack_original rd_crack_msaa PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                  ENVIRONMENT "${_env}")
    set(_crk_env_off ${_env} "ICO_RD_CRACK_FILL=0")
    set(_crk_env_depth_copy ${_env} "ICO_RD_DEPTH_COPY=1")
    set(_crk_env_buffer ${_env} "ICO_RD_FOG_PATH=buffer")
    set(_crk_env_d24s8 ${_env} "ICO_VK_FAKE_D24S8=1")
    set(_crk_env_nodual ${_env} "ICO_RD_NO_DUAL=1")
    foreach(_v off depth_copy buffer d24s8 nodual)
        set_tests_properties(rd_crack_${_v} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                       ENVIRONMENT "${_crk_env_${_v}}")
    endforeach()
    # a tile-based GPU (ICO_VK_FAKE_LIMITS=moltenvk: the Apple row of the
    # device profile): the filler on by default as on every class, the old
    # "off on this tile-based GPU until measured" line denied; [dev]
    # crack_fill = true (ICO_RD_CRACK_FILL=1) the same.  rd_crack_tiler_fold
    # (rd_crack_test 2 fold, 77 without local read): on the fog's local read
    # the fill is drawn in the scene pass the fog keeps (one pass more than
    # without the filler, not two; the fog by the local read in the same
    # replay) and SCENE equals the depth copy path's byte for byte; _mali
    # the same under a phone GPU's limits (ICO_VK_FAKE_LIMITS=mali, the fog's
    # local read by ICO_RD_FOG_PATH=local on the desktop row)
    set(_crk_old_off "rd: crack fill: off on this tile-based GPU")
    add_test(NAME rd_crack_tiler
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> -DARGS=2
                     "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail}"
                     "-DDENY=${_crk_old_off}" -P "${_crk}")
    add_test(NAME rd_crack_tiler_on
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> -DARGS=2
                     "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail}"
                     "-DDENY=${_crk_old_off}" -P "${_crk}")
    foreach(_v tiler_fold tiler_fold_mali)
        add_test(NAME rd_crack_${_v}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|fold"
                         "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail} \\(drawn inside the scene pass with the local fog\\)"
                         -P "${_crk}")
    endforeach()
    set_tests_properties(rd_crack_tiler rd_crack_tiler_fold PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                   ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk")
    set_tests_properties(rd_crack_tiler_on PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                      ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk;ICO_RD_CRACK_FILL=1")
    set_tests_properties(rd_crack_tiler_fold_mali PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=mali;ICO_RD_FOG_PATH=local")
    add_test(NAME rd_replay_tool_crack
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DARGS=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump|${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_crack.png|--enhanced|--resolution|2x|--stats|--no-crack-fill"
                     "-DEXPECT=rd: crack fill: off \\(the replay tool's --no-crack-fill\\)"
                     "-DEXPECT2=stats: crack fill off: 0 passes, 0 pixels filled"
                     -P "${_crk}")
    set_tests_properties(rd_replay_tool_crack PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                         FIXTURES_REQUIRED rd_dump)
    # the filler after the MSAA resolve (issue 26 with MSAA on): a corpus
    # room (lightning rd-00300, machine-local, SKIP without it) whose MSAA
    # epoch closes at the shadow count's pass in list 3, before the fog, at
    # Enhanced 2x with --aa msaa2 and msaa4: the state line says the fill
    # was drawn after the resolve, the --stats line counts one fill pass
    # with more than 0 pixels filled, none left unfilled, that fill after
    # the resolve; SKIP where the device does not offer the count (no
    # "rd: msaa: N samples" line: lavapipe offers 4 only)
    set(_crk_room "${CMAKE_SOURCE_DIR}/build-host/tmp/corpus/dumps/lightning/rd-00300.rddump")
    foreach(_s 2 4)
        add_test(NAME rd_crack_resolved_msaa${_s}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DARGS=${_crk_room}|${CMAKE_CURRENT_BINARY_DIR}/rd_crack_resolved_msaa${_s}.png|--enhanced|--resolution|2x|--aa|msaa${_s}|--stats"
                         "-DNEED_FILE=${_crk_room}"
                         "-DNEED=rd: msaa: ${_s} samples"
                         "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail} \\(drawn after the MSAA resolve\\)"
                         "-DEXPECT2=stats: crack fill on: 1 passes, [1-9][0-9]* pixels filled \\(1 replays counted; scale 2x2, reach 1x1 texels; 0 fogged replays above 1x not filled; 0 fills inside the scene pass with the local fog; 1 fills after the MSAA resolve\\)"
                         "-DDENY=rd: crack fill: not drawn on SCENE" -P "${_crk}")
        set_tests_properties(rd_crack_resolved_msaa${_s} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                                     TIMEOUT 600)
    endforeach()

    # issue 106: the reporter's drained-room dumps (copied into the corpus,
    # build-host/tmp/corpus/dumps/ripple) at their settings with Full pixel
    # on: the ripples' read of the puddle's block keeps REPEAT (rd_water.c
    # rd__block_keeps_repeat), so the ring area holds no near-white pixel
    # (2089 and 3014 before the fix), and the water line names the clamp's
    # counts; the ripple wholly past the first period (10:04:36) and the
    # mirror floor (the pool's grids over its scene copy, corpus puddle
    # rd-00200) byte for byte as before on lavapipe.  Not Run without the dumps
    set(_rip "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_ripple.cmake")
    set(_rip_dir "${CMAKE_SOURCE_DIR}/build-host/tmp/corpus/dumps")
    set(_rip_args "--enhanced|--aspect|16:9|--resolution|1920x1080|--full-height|--full-pixel|--aa|ssaa2|--present|1920x1080|--list")
    set(_rip_py "")
    if(Python3_FOUND)
        set(_rip_py "-DPYTHON=${Python3_EXECUTABLE}")
    endif()
    add_test(NAME rd_replay_tool_ripple
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_rip_dir}/ripple/frame-20261024-094512-v13782.zip"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_ripple.png"
                     "-DARGS=${_rip_args}"
                     "-DEXPECT=Full pixel on, border mask off: [0-9]+ render-target reads clamped, 2 block reads kept on REPEAT"
                     "-DEXPECT2=block 344x256 GS with depth [^;]*REPEAT 2, 2 of them kept on REPEAT"
                     -DBOX=870,430,1210,890 -DMAXBRIGHT=0 ${_rip_py} -P "${_rip}")
    add_test(NAME rd_replay_tool_ripple_447
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_rip_dir}/ripple/frame-20261024-100447-v84109.zip"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_ripple_447.png"
                     "-DARGS=${_rip_args}" "-DEXPECT=REPEAT 2, 2 of them kept on REPEAT"
                     -DBOX=90,700,400,1080 -DMAXBRIGHT=0 ${_rip_py} -P "${_rip}")
    add_test(NAME rd_replay_tool_ripple_436
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_rip_dir}/ripple/frame-20261024-100436-v83439.zip"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_ripple_436.png"
                     "-DARGS=${_rip_args}" "-DEXPECT=REPEAT 2, 2 of them kept on REPEAT"
                     -DSHA=984695adef5d6d26cc065d7bff996b0234b600cc2c0fd87cd50c365a3cc083aa
                     ${_rip_py} -P "${_rip}")
    add_test(NAME rd_replay_tool_mirror_floor
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_rip_dir}/puddle/rd-00200.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_mirror_floor.png"
                     "-DARGS=--enhanced|--resolution|2x|--full-pixel|--present|1024x896|--list"
                     "-DEXPECT=Full pixel on, border mask off: [0-9]+ render-target reads clamped, 0 block reads kept on REPEAT"
                     -DSHA=613d1be7e8d7c1437ffa90c237eed6dfb7db2995e39a708270403afee18e1d28
                     ${_rip_py} -P "${_rip}")
    set_tests_properties(rd_replay_tool_ripple rd_replay_tool_ripple_447 rd_replay_tool_ripple_436
                         rd_replay_tool_mirror_floor
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 600)
    # the still re-present (rd_reshow.c): a corpus dump as its own tick before
    # (nothing moves), 4 presents at Enhanced 4:3 2x at 1280x960: presents
    # 1..3 shown again, all 4 pictures byte for byte the ones rendered with
    # ICO_RD_STILL_REPRESENT=0 (the lavapipe A/B; Not Run without the dump)
    add_test(NAME rd_replay_tool_still
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_SOURCE_DIR}/build-host/tmp/pcsx2cmp/godrays/port2/keep/rd-00428.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_still" -DPRESENTS=4
                     "-DARGS=--enhanced|--aspect|4:3|--resolution|2x|--present|1280x960"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_still.cmake")
    set_tests_properties(rd_replay_tool_still PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 600)
    # the glow held between ticks (rd_glow_hold.c) against its chain drawn at
    # every present (ICO_RD_GLOW_REDRAW=1), 2 presents a tick at Enhanced 4:3
    # 2x, every present rendered: a still tick (rd-01076 as its own tick
    # before) the same bytes both ways, SCENE and the presented picture; a
    # moving one (rd-01076 then rd-01077) the first present the same bytes and
    # the later one differing only within 128 texels of the glow's mask (WORK2)
    # (the lavapipe A/B; Not Run without the dumps)
    set(_gh "${CMAKE_SOURCE_DIR}/build-host/tmp/pcsx2cmp/godrays/port2/keep")
    set(_gh_py "")
    if(Python3_FOUND)
        set(_gh_py "-DPYTHON=${Python3_EXECUTABLE}")
    endif()
    add_test(NAME rd_replay_tool_glow_hold
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DSTILL=${_gh}/rd-01076.rddump" "-DPREV=${_gh}/rd-01076.rddump"
                     "-DMOVE=${_gh}/rd-01077.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_glow_hold" -DPRESENTS=2
                     -DREACH=128 "-DARGS=--enhanced|--aspect|4:3|--resolution|2x" ${_gh_py}
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_glow_hold.cmake")
    set_tests_properties(rd_replay_tool_glow_hold PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             TIMEOUT 900)
    # the fog on the depth's words copied out through a buffer
    # (ICO_RD_FOG_PATH=buffer, rd_fog_path.c: the fallback of a device whose
    # depth reads fail), on D32S8 and on D24S8 (the top byte masked off):
    # the same expectations, and case d's replays on the other paths the
    # same pixels byte for byte
    set(_env_buf ${_env} "ICO_RD_FOG_PATH=buffer")
    set(_env_buf_d24 ${_env_buf} "ICO_VK_FAKE_D24S8=1")
    set(_fb "${CMAKE_CURRENT_BINARY_DIR}/fog_buffer")
    set(_fb24 "${CMAKE_CURRENT_BINARY_DIR}/fog_buffer_d24s8")
    file(MAKE_DIRECTORY "${_fb}" "${_fb24}")
    add_test(NAME rd_fog_buffer COMMAND rd_fog_test)
    add_test(NAME rd_fog_buffer_d24s8 COMMAND rd_fog_test)
    set_tests_properties(rd_fog_buffer PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_fb}"
                                                  ENVIRONMENT "${_env_buf}")
    set_tests_properties(rd_fog_buffer_d24s8
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_fb24}"
                                    ENVIRONMENT "${_env_buf_d24}")
    # the fog's self-test and probe (rd_fog_path.c): the depth read in place
    # passes on lavapipe and is taken, the other two not tried; with
    # ICO_RD_FOG_PATH=test every path is tried and passes, in place taken;
    # with the in-place and copy reads made to give 0 (ICO_RD_FOG_SABOTAGE,
    # a phone whose depth reads come back empty) both fail and the buffer
    # path is taken, its fogged grid matching
    add_test(NAME rd_fog_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=inplace
                     "-DRESULTS=inplace ok [0-2], copy not tried, buffer not tried"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    add_test(NAME rd_fog_selftest_all
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=inplace "-DRESULTS=inplace ok [0-2], copy ok [0-2], buffer ok [0-2]"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    add_test(NAME rd_fog_selftest_fallback
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=buffer
                     "-DRESULTS=inplace fail [0-9]+, copy fail [0-9]+, buffer ok [0-2]"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    # the blend's self-test (rd_blend_selftest.c) on lavapipe, which has the
    # attachment read (the interlock faked off: Mesa 25.2's lavapipe has it).
    # By default the fixed-function blend is in force and only its
    # reference runs (the line says how to opt in); read white
    # (ICO_RD_BLEND_SABOTAGE=fixed) the two-pass fallback is taken; under the
    # fake Adreno (ICO_VK_FAKE_ADRENO) the device line says the attachment
    # read is off, and ICO_VK_ATTACHMENT_READ=1 with ICO_RD_BLEND=attachment
    # offers and opts into it; without a destination read the reference
    # still runs; ICO_RD_BLEND attachment (kept though sabotaged), exact (the
    # device's best: the attachment read), storage (not on this device: the
    # default kept) and fixed honoured; the fake MoltenVK with
    # ICO_RD_BLEND=storage tries the interlock where the device has one.
    # The results' "; " is matched as ", " (rd_blend_selftest.cmake)
    set(_bst "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_blend_selftest.cmake")
    set(_bst_env_mvk "${_bst_env};ICO_VK_FAKE_LIMITS=moltenvk")
    set(_bst_env "${_bst_env};ICO_VK_FAKE_NO_INTERLOCK=1")
    set(_bst_att "exact \\(attachment read\\)")
    set(_bst_fix "fixed-function")
    set(_bst_hint ", exact modes not asked for: \\[dev\\] blend = attachment\\|storage tries one")
    set(_bst_ok "fixed-function ok [0-2]${_bst_hint}")
    set(_bst_kept "fixed-function ok [0-2], kept by \\[dev\\] blend")
    add_test(NAME rd_blend_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}"
                     "-DEXTRA=rd: blend: fixed-function \\(the default. \\[dev\\] blend = attachment\\|storage opts into an exact mode\\)"
                     "-DEXTRA2=rd: profile: immediate-mode GPU \\(Mesa, Vulkan\\): blend fixed-function. "
                     -P "${_bst}")
    add_test(NAME rd_blend_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}"
                     "-DRESULTS=fixed-function fail [0-9]+ -> two-pass ok [0-2]${_bst_hint}"
                     "-DFAIL=fixed-function fail [0-9]+"
                     "-DEXTRA=blend: the fixed-function blend through the two-pass fallback"
                     -P "${_bst}")
    add_test(NAME rd_blend_selftest_adreno
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}" -DDENY=1 -P "${_bst}")
    add_test(NAME rd_blend_selftest_adreno_forced
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=attachment
                     "-DNAME=${_bst_att}" "-DRESULTS=attachment ok [01], ${_bst_kept}"
                     "-DEXTRA=attachment read yes( \\(ICO_VK_FAKE_NO_INTERLOCK\\))? \\(Qualcomm driver: ICO_VK_ATTACHMENT_READ=1\\)"
                     -P "${_bst}")
    add_test(NAME rd_blend_selftest_no_dst
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}" -P "${_bst}")
    add_test(NAME rd_blend_selftest_blend_attachment
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=attachment
                     "-DNAME=${_bst_att}" "-DRESULTS=attachment fail [0-9]+, ${_bst_kept}"
                     "-DFAIL=attachment fail [0-9]+" -P "${_bst}")
    add_test(NAME rd_blend_selftest_blend_exact
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=attachment
                     "-DNAME=${_bst_att}" "-DRESULTS=attachment ok [01], ${_bst_kept}"
                     "-DEXTRA=blend: \\[dev\\] blend = exact: the exact blend is an opt-in. attachment tried"
                     -P "${_bst}")
    add_test(NAME rd_blend_selftest_blend_storage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}"
                     "-DEXTRA=ICO_RD_BLEND=storage is not on this device" -P "${_bst}")
    add_test(NAME rd_blend_selftest_blend_fixed
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}"
                     "-DEXTRA=rd: blend: fixed-function \\(\\[dev\\] blend = fixed\\)" -P "${_bst}")
    # the fake MoltenVK (no attachment read; the interlock the device's own,
    # skipped without it) opting into the storage read: tried and kept as on
    # any device (no MoltenVK rule)
    add_test(NAME rd_blend_selftest_moltenvk_storage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=storage
                     "-DNAME=exact \\(interlock\\)"
                     "-DRESULTS=storage (ok|fail) [0-9]+, ${_bst_kept}" -P "${_bst}")
    set(_bst_tests rd_blend_selftest rd_blend_selftest_sabotage rd_blend_selftest_adreno
                   rd_blend_selftest_adreno_forced rd_blend_selftest_no_dst
                   rd_blend_selftest_blend_attachment rd_blend_selftest_blend_exact
                   rd_blend_selftest_blend_storage rd_blend_selftest_blend_fixed
                   rd_blend_selftest_moltenvk_storage)
    set_tests_properties(${_bst_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_blend_selftest PROPERTIES ENVIRONMENT "${_bst_env}")
    set_tests_properties(rd_blend_selftest_sabotage
                         PROPERTIES ENVIRONMENT "${_bst_env};ICO_RD_BLEND_SABOTAGE=fixed")
    set_tests_properties(rd_blend_selftest_adreno PROPERTIES ENVIRONMENT "${_bst_env};ICO_VK_FAKE_ADRENO=1")
    set_tests_properties(rd_blend_selftest_adreno_forced
                         PROPERTIES ENVIRONMENT
                                    "${_bst_env};ICO_VK_FAKE_ADRENO=1;ICO_VK_ATTACHMENT_READ=1;ICO_RD_BLEND=attachment")
    set_tests_properties(rd_blend_selftest_no_dst PROPERTIES ENVIRONMENT "${_bst_env};ICO_VK_FAKE_NO_DST=1")
    set_tests_properties(rd_blend_selftest_blend_attachment
                         PROPERTIES ENVIRONMENT
                                    "${_bst_env};ICO_RD_BLEND=attachment;ICO_RD_BLEND_SABOTAGE=attachment")
    set_tests_properties(rd_blend_selftest_blend_exact PROPERTIES ENVIRONMENT "${_bst_env};ICO_RD_BLEND=exact")
    set_tests_properties(rd_blend_selftest_blend_storage
                         PROPERTIES ENVIRONMENT "${_bst_env};ICO_RD_BLEND=storage")
    set_tests_properties(rd_blend_selftest_blend_fixed PROPERTIES ENVIRONMENT "${_bst_env};ICO_RD_BLEND=fixed")
    set_tests_properties(rd_blend_selftest_moltenvk_storage
                         PROPERTIES ENVIRONMENT "${_bst_env_mvk};ICO_RD_BLEND=storage")
    # the VU vertex pre-pass's self-test (rd_vuprepass_selftest.c): on
    # lavapipe the pre-pass draws the self-test's meshes byte for byte as
    # the programs' entries do and stays on, in the extended variant (a wide
    # scaled scene) and the plain one (Original at 4:3 and 1x); with its
    # first command's records drawn a texel off (ICO_RD_VU_PREPASS_SABOTAGE,
    # a driver whose record goes wrong) the pictures differ and it is
    # turned off before the set; with it off by the settings
    # (ICO_RD_VU_PREPASS=0) nothing runs
    set(_vst "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vuprepass_selftest.cmake")
    set(_vst_on "on \\(self-test: prelit, lit, skin(, the tick cameras)? and particles byte for byte, [0-9]+ vertices and 4 sprites in [0-9]+ pre-pass draws \\(2 passes, [0-9]+ triangle slots\\), [0-9]+ of [0-9]+ pixels drawn\\) on SCENE [0-9]+x[0-9]+ at [0-9]+x[0-9]+ texels in [0-9.]+ ms")
    set(_vst_off "off \\(self-test: the pictures differ: [0-9]+ of [0-9]+ bytes, the first at texel [0-9]+,[0-9]+: [0-9,]+ want [0-9,]+\\) on SCENE")
    add_test(NAME rd_vuprepass_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_plain
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     -DVARIANT=plain "-DLINE=${_vst_on}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                     "-DLINE=${_vst_off}"
                     "-DEXTRA=rd: VU pre-pass: ICO_RD_VU_PREPASS_SABOTAGE: " -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_off
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test>
                     -DMODE=notrun "-DLINE=off \\(self-test not run\\)" -P "${_vst}")
    # the same with the pre-pass's draws one an entry and stage
    # (ICO_RD_VU_PREPASS_BATCH=0: the vu_X_pre_vs builds, as before the
    # batches; rd_replay_vu.c vuPrepassRun)
    add_test(NAME rd_vuprepass_selftest_unbatched
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" -P "${_vst}")
    set(_vst_tests rd_vuprepass_selftest rd_vuprepass_selftest_plain
                   rd_vuprepass_selftest_sabotage rd_vuprepass_selftest_off
                   rd_vuprepass_selftest_unbatched)
    set_tests_properties(${_vst_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    if(_env)
        set_tests_properties(rd_vuprepass_selftest rd_vuprepass_selftest_plain
                             PROPERTIES ENVIRONMENT "${_env}")
    endif()
    set_tests_properties(rd_vuprepass_selftest_sabotage
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS_SABOTAGE=1")
    set_tests_properties(rd_vuprepass_selftest_unbatched
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS_BATCH=0")
    set_tests_properties(rd_vuprepass_selftest_off
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS=0")
    # odd texel counts under MSAA (1935x1080 is 645x360 at scale 3;
    # 2341x1080 at scale 1) and the big scenes: in place passes as at an even
    # width.  Sample counts the device lacks are skipped.  With the buffer path
    # forced (it cannot read multisampled depth) MSAA is off while the mask
    # still lists it
    set(_odd_msaa)
    macro(ico_fog_selftest_msaa tw th aa)
      set(_n rd_fog_selftest_${tw}_aa${aa})
      add_test(NAME ${_n}
               COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                       -DFOG_PATH=inplace "-DARGS=688|448|1|${aa}|${tw}|${th}"
                       "-DRESULTS=inplace ok [0-2], copy not tried, buffer not tried"
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
      list(APPEND _odd_msaa ${_n})
    endmacro()
    foreach(_aa 3 4 5)
      ico_fog_selftest_msaa(1935 1080 ${_aa})
      ico_fog_selftest_msaa(2341 1080 ${_aa})
    endforeach()
    ico_fog_selftest_msaa(3870 2160 4)
    ico_fog_selftest_msaa(7740 4320 4)
    add_test(NAME rd_fog_selftest_7740
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=inplace "-DARGS=688|448|1|0|7740|4320"
                     "-DRESULTS=inplace ok [0-2], copy not tried, buffer not tried"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    add_test(NAME rd_fog_selftest_msaa_fallback
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=buffer "-DARGS=688|448|1|4|1935|1080"
                     "-DRESULTS=buffer ok [0-2], inplace not tried, copy not tried"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(${_odd_msaa} rd_fog_selftest_7740 rd_fog_selftest_msaa_fallback
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_fog_selftest_msaa_fallback PROPERTIES ENVIRONMENT "${_env};ICO_RD_FOG_PATH=buffer")
    if(_env)
      set_tests_properties(${_odd_msaa} rd_fog_selftest_7740 PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # Full pixel at 1920 x 1080 16:9 (the window's exact pixels: SCENE 1920
    # texels and a guard texel a side, the frame's 688 GS pixels cropped to
    # its exact 682.67), with
    # and without MSAA: the depth read in place passes and is taken
    set(_fullpixel)
    foreach(_aa 0 3 4 5)
      set(_n rd_fog_selftest_fullpixel_aa${_aa})
      add_test(NAME ${_n}
               COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                       -DFOG_PATH=inplace "-DARGS=688|448|0|${_aa}|0|0|1920|1080"
                       "-DRESULTS=inplace ok [0-2], copy not tried, buffer not tried"
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
      list(APPEND _fullpixel ${_n})
    endforeach()
    set_tests_properties(${_fullpixel} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    if(_env)
      set_tests_properties(${_fullpixel} PROPERTIES ENVIRONMENT "${_env}")
    endif()
    set_tests_properties(rd_fog_selftest rd_fog_selftest_all rd_fog_selftest_fallback
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set(_env_sab ${_env} "ICO_RD_FOG_SABOTAGE=inplace,copy")
    set_tests_properties(rd_fog_selftest_fallback PROPERTIES ENVIRONMENT "${_env_sab}")
    set(_env_all ${_env} "ICO_RD_FOG_PATH=test")
    set_tests_properties(rd_fog_selftest_all PROPERTIES ENVIRONMENT "${_env_all}")
    if(_env)
        set_tests_properties(rd_fog_selftest PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # a player dump with the fog (the dark hall) replayed with the depth in
    # place and with its copy: SCENE byte for byte the same, on D32S8 and
    # on D24S8.  Not Run without the dump (dist/dumps is not in the tree)
    set(ICO_FOG_DUMP "${CMAKE_SOURCE_DIR}/dist/dumps/frame-20261009-083248-v13836.rddump"
        CACHE FILEPATH "a player dump with the depth fog, for rd_fog_depth_copy_dump")
    add_test(NAME rd_fog_depth_copy_dump
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${ICO_FOG_DUMP}" "-DOUT=${_dc}/dump"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_depth_copy_dump.cmake")
    add_test(NAME rd_fog_depth_copy_dump_d24s8
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${ICO_FOG_DUMP}" "-DOUT=${_dc}/dump_d24s8"
                     "-DEXTRA_ENV=ICO_VK_FAKE_D24S8=1" -DNO_BUFFER=1
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_depth_copy_dump.cmake")
    set_tests_properties(rd_fog_depth_copy_dump rd_fog_depth_copy_dump_d24s8
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 600)
    if(_env)
        set_tests_properties(rd_fog_depth_copy_dump rd_fog_depth_copy_dump_d24s8
                             PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # the same tests and expectations on a device with a
    # phone GPU's limits (ICO_VK_FAKE_LIMITS=mali, port/rhi/vk/vk_device.c:
    # four descriptor sets, 16 sampled images a stage, 256-byte storage
    # offsets, 4096 memory allocations): a pipeline or an allocation the
    # renderer needs past them would be refused and its draws missing.
    # rd_blur is left out (three and a half minutes on lavapipe): its
    # pipelines are in the reachable set rd_perf_mali creates, on the same
    # bind group layouts as every other draw's
    set(_env_mali ${_env} "ICO_VK_FAKE_LIMITS=mali")
    set(_ml "${CMAKE_CURRENT_BINARY_DIR}/mali")
    file(MAKE_DIRECTORY "${_ml}")
    set(_ico_mali_tests "")
    foreach(t rd_layout rd_tex rd_gsbase rd_shadow rd_fog rd_water rd_raw rd_interp rd_mirror
              rd_perf)
        add_test(NAME ${t}_mali COMMAND ${t}_test)
        list(APPEND _ico_mali_tests ${t}_mali)
    endforeach()
    foreach(t rd_pixel rd_debug rd_present rd_crt)
        add_test(NAME ${t}_mali COMMAND ${t}_test "${_ml}")
        list(APPEND _ico_mali_tests ${t}_mali)
    endforeach()
    ico_add_sharded_test(rd_mesh_mali 4 PROGRAM rd_mesh_test ARGS "${ICO_DISC_IMAGE}")
    list(APPEND _ico_mali_tests ${rd_mesh_mali_SHARDS})
    set_tests_properties(${_ico_mali_tests} PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_ml}"
                                                       ENVIRONMENT "${_env_mali}")
    # the same tests and expectations at the limits every Vulkan device is
    # required to meet (ICO_VK_FAKE_LIMITS=min, vk_device.c: 256-byte offset
    # alignment, 2^27-byte storage ranges, 4096-texel images), where phone
    # GPUs sit and lavapipe does not
    set(_env_minlimits ${_env} "ICO_VK_FAKE_LIMITS=min")
    set(_mn "${CMAKE_CURRENT_BINARY_DIR}/minlimits")
    file(MAKE_DIRECTORY "${_mn}")
    add_test(NAME rd_pixel_minlimits COMMAND rd_pixel_test "${_mn}")
    add_test(NAME rd_present_minlimits COMMAND rd_present_test "${_mn}")
    ico_add_sharded_test(rd_mesh_minlimits 4 PROGRAM rd_mesh_test ARGS "${ICO_DISC_IMAGE}")
    add_test(NAME rd_interp_minlimits COMMAND rd_interp_test)
    set_tests_properties(rd_pixel_minlimits rd_present_minlimits ${rd_mesh_minlimits_SHARDS}
                         rd_interp_minlimits
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_mn}"
                                    ENVIRONMENT "${_env_minlimits}")
endif()

# G1: the per-triangle record's tests.
if(ICO_RUN_TESTS)
    # the pre-passes' self-test with the tick stage's verdicts wrong (tick 1
    # tested at tick 0's camera: the self-test's ticks copy is dropped) and
    # with the triangles' slot A drawn a slot off: the pictures differ and the
    # pre-passes are turned off before the set
    add_test(NAME rd_vuprepass_selftest_ticks
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                     "-DLINE=${_vst_off}"
                     "-DEXTRA=rd: VU pre-pass: ICO_RD_VU_PREPASS_SABOTAGE: in the self-test the tick stage"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_sabotage_tri
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                     "-DLINE=${_vst_off}"
                     "-DEXTRA=rd: VU pre-pass: ICO_RD_VU_PREPASS_SABOTAGE: in the self-test the first command's triangles"
                     -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_ticks rd_vuprepass_selftest_sabotage_tri
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_ticks
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS_SABOTAGE=ticks")
    set_tests_properties(rd_vuprepass_selftest_sabotage_tri
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS_SABOTAGE=tri")
    # rd_vustage_ab (test/rd_vustage_ab.cmake): every dump of the
    # machine-local list tools/vustage_dumps.txt replayed the old way
    # (ICO_RD_VU_PREPASS=0, the programs' entries) and through the record,
    # at Original 4:3 1x and Enhanced 4:3 2x under ICO_RD_BLEND=fixed: every
    # named target byte for byte; SKIP without the list, its dumps or a
    # device
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vustage_ab")
    add_test(NAME rd_vustage_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vustage_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vustage_ab.cmake")
    set_tests_properties(rd_vustage_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_vustage_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vubatch_ab (test/rd_vubatch_ab.cmake): the same dumps through the
    # record one point draw an entry and stage (ICO_RD_VU_PREPASS_BATCH=0)
    # and in the pre-pass's batches (rd_replay_vu.c vuPrepassRun), at
    # Original 4:3 1x, Enhanced 4:3 2x and Enhanced 16:9 2x under
    # ICO_RD_BLEND=fixed: every named target byte for byte, the same
    # vertices, fewer pre-pass draws batched; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vubatch_ab")
    add_test(NAME rd_vubatch_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vubatch_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vubatch_ab.cmake")
    set_tests_properties(rd_vubatch_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_vubatch_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
endif()
# The blend policy (rd_dst_read.c)
# rd_dst_usage_test: the fixed-function blend by default makes no
# destination-read layout, shader twin, target usage or start-up key; an
# ICO_RD_BLEND=attachment opt-in makes the attachment read's alone.
add_executable(rd_dst_usage_test test/rd_dst_usage_test.c)
target_compile_options(rd_dst_usage_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_dst_usage_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_dst_usage_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_dst_usage COMMAND rd_dst_usage_test)
    set_tests_properties(rd_dst_usage PROPERTIES SKIP_RETURN_CODE 77)
    if(_env)
        set_tests_properties(rd_dst_usage PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # the exact blend's attachment read, now an opt-in, by the tests that
    # drew it by default before (lavapipe has the read): the same
    # expectations and recorded hashes (rd_present's and rd_crt's *_EXACT),
    # rd_pixel's comparison of the modes, rd_perf's destination-read counts
    set(_env_blend_attachment ${_env} "ICO_RD_BLEND=attachment")
    set(_bat "${CMAKE_CURRENT_BINARY_DIR}/blend_attachment")
    file(MAKE_DIRECTORY "${_bat}")
    add_test(NAME rd_pixel_attachment COMMAND rd_pixel_test "${_bat}")
    add_test(NAME rd_fog_attachment COMMAND rd_fog_test)
    add_test(NAME rd_present_attachment COMMAND rd_present_test "${_bat}")
    add_test(NAME rd_crt_attachment COMMAND rd_crt_test "${_bat}")
    add_test(NAME rd_perf_attachment COMMAND rd_perf_test)
    set_tests_properties(rd_pixel_attachment rd_fog_attachment rd_present_attachment
                         rd_crt_attachment rd_perf_attachment
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_bat}"
                                    ENVIRONMENT "${_env_blend_attachment}")
endif()

# The tiler passes (rd_replay.c: the fog's local read, DATE through
# the stencil bit, the blur pair fold)
# The fog's depth read inside the scene pass (ICO_RD_FOG_PATH=local: the
# profile's local read on lavapipe, which has it): rd_fog_test's grid, edge
# and above-1x cases on the local path, and case d's replays on the other
# paths the same pixels byte for byte; the fog's self-test tries local first
# and takes it (a scaled, wide Full pixel SCENE), its probe logged; DATE's
# self-test (ICO_RD_DATE_PATH=stencil) finds the stencil bit and the
# snapshot right on every sample.  rd_pixel's DATE case and rd_blur's fold
# case (b) run in their own tests.
if(ICO_RUN_TESTS)
    set(_env_fog_local ${_env} "ICO_RD_FOG_PATH=local")
    set(_fl "${CMAKE_CURRENT_BINARY_DIR}/fog_local")
    file(MAKE_DIRECTORY "${_fl}")
    add_test(NAME rd_fog_local COMMAND rd_fog_test)
    set_tests_properties(rd_fog_local PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_fl}"
                                                 ENVIRONMENT "${_env_fog_local}")
    add_test(NAME rd_fog_selftest_local
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=local
                     "-DRESULTS=local ok [0-2], copy not tried, inplace not tried, buffer not tried"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(rd_fog_selftest_local PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                          ENVIRONMENT "${_env_fog_local}")
    set(_env_date_stencil ${_env} "ICO_RD_DATE_PATH=stencil")
    add_test(NAME rd_date_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DPATH=stencil
                     "-DRESULTS=stencil ok 4, snapshot ok 4"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
    set_tests_properties(rd_date_selftest PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                     ENVIRONMENT "${_env_date_stencil}")
    # the shadow count's self-test (rd_shadow_selftest.c): the one draw
    # equal to the seven draws byte for byte at both scene sizes of the
    # tool (a wide scene at scale 2 with Full pixel's left-out texels, then
    # another size), every band centre the count's colour; sabotaged (the
    # one draw's table wrong at m = 1), the one draw differs, the profile's
    # flag goes off and the seven draws resolve the count (no second line:
    # nothing to check then); [dev] shadow_one = false, no self-test and the
    # profile's words "shadow count seven draws ([dev])"
    add_test(NAME rd_shadow_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=one draw"
                     "-DRESULTS=equal to the seven draws over [0-9]+x[0-9]+, 142 band centres ok"
                     "-DEXTRA=profile: .*shadow count one draw"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    set_tests_properties(rd_shadow_selftest PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                       ENVIRONMENT "${_env}")
    add_test(NAME rd_shadow_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=seven draws"
                     "-DRESULTS=one draw differs at [0-9]+ of [0-9]+ texels, the first \\([0-9]+,[0-9]+\\): [0-9A-F]+ for [0-9A-F]+"
                     "-DEXTRA=profile: shadow count one draw off \\(self-test\\)"
                     "-DEXTRA2=shadow count: ICO_RD_SHADOW_SABOTAGE: "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    set_tests_properties(rd_shadow_selftest_sabotage
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_RD_SHADOW_SABOTAGE=1")
endif()

# The desktop's local reads and their start-up cost probe (rd_local_probe.c):
# the verdict's rule on synthetic times (rd_local_probe_rule, no device), then
# the probe on lavapipe (the desktop class, with local read) through
# rd_fog_selftest_test with ICO_RD_LOCAL_PROBE=1 (a headless run keeps the
# snapshots without it), the local reads' time biased by
# ICO_RD_LOCAL_PROBE_SABOTAGE so both verdicts run: fast, the local reads
# kept (the fog's depth path local at both scene sizes, "kept (probe x vs y
# ms)"); slow, turned off ("DATE stencil off (probe)", every named target
# made again, the fog's self-test run again: in place at both sizes)
if(ICO_RUN_TESTS)
    add_test(NAME rd_local_probe_rule COMMAND rd_local_probe_test)
    add_test(NAME rd_local_probe_kept
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=local
                     "-DRESULTS=local ok [0-2], inplace not tried, copy not tried, buffer not tried"
                     "-DEXTRA=profile: local read on the desktop: kept \\(probe [0-9.]+ vs [0-9.]+ ms\\): "
                     "-DEXTRA2=profile: ICO_RD_LOCAL_PROBE_SABOTAGE: "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(rd_local_probe_kept
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_RD_LOCAL_PROBE=1;ICO_RD_LOCAL_PROBE_SABOTAGE=fast")
    add_test(NAME rd_local_probe_off
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=inplace
                     "-DRESULTS=inplace ok [0-2], copy not tried, buffer not tried"
                     "-DEXTRA=profile: local read on the desktop: off \\(probe: slower, [0-9.]+ vs [0-9.]+ ms\\): "
                     "-DEXTRA2=profile: DATE stencil off \\(probe\\)"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(rd_local_probe_off
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_RD_LOCAL_PROBE=1;ICO_RD_LOCAL_PROBE_SABOTAGE=slow")
endif()
# ---- FIXT: the tiler self-tests' failure paths ----
# Under the MoltenVK report (ICO_VK_FAKE_LIMITS=moltenvk: the Apple row of the
# device profile, fogLocal and dateStencil on as the profile's own, not
# [dev]'s), each self-test made to fail by its sabotage switch: DATE's
# setup writes 0 into the stencil bit (ICO_RD_DATE_SABOTAGE=1), so the
# stencil replay fails, the snapshot draws DATE and the profile's flag goes
# off (the fog in place, ICO_RD_FOG_PATH=inplace, so the tool's path
# check holds); the fog's local read gives 0 (ICO_RD_FOG_SABOTAGE=local),
# so local fails, the copy is taken, fogLocal goes off, and the run after
# the new scene size tries the local order without local (copy first).
if(ICO_RUN_TESTS)
    add_test(NAME rd_date_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DPATH=snapshot
                     "-DRESULTS=stencil fail [1-4] at sample [0-3]: [0-9A-F]+ for [0-9A-F]+, snapshot ok 4"
                     "-DEXTRA=profile: DATE stencil off \\(self-test\\)"
                     "-DEXTRA2=DATE: ICO_RD_DATE_SABOTAGE: "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
    set_tests_properties(rd_date_selftest_sabotage
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk;ICO_RD_FOG_PATH=inplace;ICO_RD_DATE_SABOTAGE=1")
    add_test(NAME rd_fog_selftest_local_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DFOG_PATH=copy
                     "-DRESULTS=(local fail [0-9]+, )?copy ok [0-2], inplace not tried, buffer not tried"
                     "-DEXTRA=local fail [0-9]+, copy ok [0-2]"
                     "-DEXTRA2=profile: fog local off \\(self-test\\)"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(rd_fog_selftest_local_sabotage
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk;ICO_RD_FOG_SABOTAGE=local")
endif()
# ---- FIXT end ----

# FIXP: the static pipeline path pinned.  Lavapipe has Vulkan 1.3 and
# VK_EXT_extended_dynamic_state3, so every other run draws with the depth and
# blend state dynamic and the start-up set's membership checks
# (rd__pipe_key_equal) compare modulo that normalisation; with
# ICO_VK_FAKE_NO_DYNAMIC=1 (vk_device.c, [dev] dynamic_state = false) every
# pipeline is static and each check is exact: rd_pixel's whole run, and
# rd_mesh's shard 1 of 4 (gpu_prims, the shard that draws the VU cases).
if(ICO_RUN_TESTS)
    set(_env_static ${_env} "ICO_VK_FAKE_NO_DYNAMIC=1")
    set(_st "${CMAKE_CURRENT_BINARY_DIR}/static")
    file(MAKE_DIRECTORY "${_st}")
    add_test(NAME rd_pixel_static COMMAND rd_pixel_test "${_st}")
    add_test(NAME rd_mesh_static.1of4 COMMAND rd_mesh_test "${ICO_DISC_IMAGE}" --shard 1/4)
    set_tests_properties(rd_pixel_static rd_mesh_static.1of4
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_st}"
                                    ENVIRONMENT "${_env_static}")
endif()
# FIXP end

# VK11: the render tests on a Vulkan 1.1 driver's absences (ICO_VK_FAKE_API=1.1,
# port/rhi/vk/vk_device.c; issue 145): the submissions tracked by the fence ring
# instead of the timeline semaphore (port/rhi/vk/vk_sync.c), the render passes as
# VkRenderPass and VkFramebuffer objects instead of dynamic rendering
# (port/rhi/vk/vk_renderpass.c), no local read, no driver properties, no image
# format list.  The _vk11ext pair runs ICO_VK_FAKE_API=1.1ext (a 1.1 driver with
# the extensions: the KHR timeline semaphore and dynamic rendering).  The
# expectations are the Vulkan runs' own, byte for byte (neither path changes a
# pixel); rd_perf_vk11's pass and barrier counts are rd_perf's.  Label vk11:
# ctest -j1 -L vk11 (with port/rhi's rhi_vk_vk11 and rhi_vk_swapchain_vk11).
if(ICO_RUN_TESTS)
    set(_env_vk11 ${_env} "ICO_VK_FAKE_API=1.1")
    set(_v11 "${CMAKE_CURRENT_BINARY_DIR}/vk11")
    file(MAKE_DIRECTORY "${_v11}")
    add_test(NAME rd_pixel_vk11 COMMAND rd_pixel_test "${_v11}")
    add_test(NAME rd_present_vk11 COMMAND rd_present_test "${_v11}")
    add_test(NAME rd_crt_vk11 COMMAND rd_crt_test "${_v11}")
    add_test(NAME rd_debug_vk11 COMMAND rd_debug_test "${_v11}")
    add_test(NAME rd_raw_vk11 COMMAND rd_raw_test)
    add_test(NAME rd_fog_vk11 COMMAND rd_fog_test)
    add_test(NAME rd_shadow_vk11 COMMAND rd_shadow_test)
    add_test(NAME rd_interp_vk11 COMMAND rd_interp_test)
    add_test(NAME rd_msaa_vk11 COMMAND rd_msaa_test)
    add_test(NAME rd_perf_vk11 COMMAND rd_perf_test)
    set(_vk11_tests rd_pixel_vk11 rd_present_vk11 rd_crt_vk11 rd_debug_vk11 rd_raw_vk11
                    rd_fog_vk11 rd_shadow_vk11 rd_interp_vk11 rd_msaa_vk11 rd_perf_vk11)
    # rd_mesh's four shards (the disc's models; 77 without the disc image)
    foreach(_k RANGE 1 4)
        add_test(NAME rd_mesh_vk11.${_k}of4 COMMAND rd_mesh_test "${ICO_DISC_IMAGE}" --shard ${_k}/4)
        list(APPEND _vk11_tests rd_mesh_vk11.${_k}of4)
    endforeach()
    set_tests_properties(${_vk11_tests}
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_v11}"
                                    ENVIRONMENT "${_env_vk11}")
    set(_env_vk11ext ${_env} "ICO_VK_FAKE_API=1.1ext")
    set(_v11x "${CMAKE_CURRENT_BINARY_DIR}/vk11ext")
    file(MAKE_DIRECTORY "${_v11x}")
    add_test(NAME rd_pixel_vk11ext COMMAND rd_pixel_test "${_v11x}")
    add_test(NAME rd_present_vk11ext COMMAND rd_present_test "${_v11x}")
    set_tests_properties(rd_pixel_vk11ext rd_present_vk11ext
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_v11x}"
                                    ENVIRONMENT "${_env_vk11ext}")
    foreach(_t ${_vk11_tests} rd_pixel_vk11ext rd_present_vk11ext)
        get_test_property(${_t} LABELS _labels)
        set_tests_properties(${_t} PROPERTIES LABELS "${_labels};vk11")
    endforeach()
endif()
# VK11 end

# ---- The render suite a second time, on the OpenGL backend (issue 126) ----
# ico_rhi_vk links every backend and rhi_backend.c takes ICO_RHI_BACKEND from the
# environment as the default one, so the same test programs run on OpenGL with no
# rebuild: for each test named in _ico_rd_gl_tests below, ico_rd_gl_twin makes
# <name>_gl from the test as registered above (its command line, skip switches
# and timeout), with the Vulkan layer variables dropped, ICO_RHI_BACKEND=opengl on
# Mesa's software GL (llvmpipe) on SDL's offscreen driver (as rhi_gl in
# port/rhi/CMakeLists.txt), three times the timeout (llvmpipe compiles the VU
# vertex shaders' GLSL slower than lavapipe takes their SPIR-V), and its own
# folder, <build>/gl[/...], for the files a test writes (the Vulkan runs' are
# left alone). The label `gl` selects them: ctest -j1 -L gl.
#
# Not twinned:
#  * a test whose environment holds a Vulkan fake (ICO_VK_FAKE_*, ICO_VK_ATTACHMENT_READ),
#    which OpenGL ignores: *_mali, *_minlimits, *_d24s8, *_static, rd_crack_d24s8,
#    rd_crack_tiler(_on, _fold, _fold_mali), rd_blend_selftest*, rd_date_selftest_sabotage,
#    rd_fog_selftest_local_sabotage (ico_rd_gl_twin skips these by itself)
#  * the tests that start the game (model_viewer_headless, gallery_sweep,
#    credits_headless) or work on the replay tool's files (rd_replay_tool*,
#    rd_crack's replay, rd_fog_depth_copy_dump*, rd_vustage_ab, rd_vubatch_ab, the native frame
#    gates, coverage_corpus; tools/native_frame_check.py compare runs the dumps
#    on both backends)
#  * tests with no device (rd_alpha_plan, rd_fx_frame, rd_vu_frac, rd_mip, rd_passring,
#    rd_filter, rd_bisect_plan, rd_stream_audit, fog_lut*, gsb_cull*, texpack*, gltf)
#
# ICO_RD_GL_TOLERANCE: "<test>_gl|<VARIABLE>=<value>" entries add a variable to one
# twin's environment, for the test's own tolerance switch where it has one. Only
# filtered or float paths may be named here (the presenter's bilinear blit, the fog
# LUT, the CRT's filtering), each with its reason; the integer paths (DISPLAY,
# blend_int, UINT targets, DATE) are bit-exact. No test has such a switch yet (their
# tolerances are constants in the sources), so the table is empty: a mismatch is a
# bug to report, not a tolerance to add.
set(ICO_RD_GL_TOLERANCE)

function(ico_rd_gl_twin name)
    # the command as add_test was given it (cmake/IcoTests.cmake keeps it: ctest
    # has no COMMAND property to read back)
    get_property(_cmd GLOBAL PROPERTY "ICO_TEST_COMMAND_${name}")
    if(NOT _cmd)
        message(WARNING "ico: ${name} is not a test, no OpenGL twin")
        return()
    endif()
    get_test_property(${name} ENVIRONMENT _tenv)
    set(_genv "ICO_RHI_BACKEND=opengl" "LIBGL_ALWAYS_SOFTWARE=1" "SDL_VIDEO_DRIVER=offscreen")
    if(_tenv AND NOT _tenv STREQUAL "NOTFOUND")
        foreach(_e IN LISTS _tenv)
            if(_e MATCHES "^ICO_VK_(FAKE_|ATTACHMENT_READ)")
                set_property(GLOBAL APPEND PROPERTY ICO_RD_GL_SKIPPED ${name})
                return()
            endif()
            if(_e MATCHES "^(VK_ADD_LAYER_PATH|VK_KHRONOS_VALIDATION_|ICO_VK_VALIDATION)")
                continue()
            endif()
            list(APPEND _genv "${_e}")
        endforeach()
    endif()
    foreach(_t IN LISTS ICO_RD_GL_TOLERANCE)
        if(_t MATCHES "^${name}_gl\\|(.*)$")
            list(APPEND _genv "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    # the folder the test writes in: the build folder's gl/ (or the test's own
    # folder under it), for the working directory and for any argument that is one
    set(_bin "${CMAKE_CURRENT_BINARY_DIR}")
    get_property(_wd GLOBAL PROPERTY "ICO_TEST_WORKING_DIRECTORY_${name}")
    if(NOT _wd)
        set(_wd "${_bin}")
    endif()
    string(REPLACE "${_bin}" "${_bin}/gl" _wd "${_wd}")
    file(MAKE_DIRECTORY "${_wd}")
    set(_gcmd)
    foreach(_a IN LISTS _cmd)
        if(IS_DIRECTORY "${_a}")
            # a folder argument (the variants' nodual/, blend_fixed/, ...): made
            # under gl/ as the base one is made here
            string(REPLACE "${_bin}" "${_bin}/gl" _a "${_a}")
            file(MAKE_DIRECTORY "${_a}")
        else()
            string(REPLACE "${_bin}" "${_bin}/gl" _a "${_a}")
        endif()
        list(APPEND _gcmd "${_a}")
    endforeach()
    add_test(NAME ${name}_gl COMMAND ${_gcmd} WORKING_DIRECTORY "${_wd}")
    set_tests_properties(${name}_gl PROPERTIES ENVIRONMENT "${_genv}")
    foreach(_p SKIP_RETURN_CODE SKIP_REGULAR_EXPRESSION)
        get_test_property(${name} ${_p} _v)
        if(_v AND NOT _v STREQUAL "NOTFOUND")
            set_tests_properties(${name}_gl PROPERTIES ${_p} "${_v}")
        endif()
    endforeach()
    # three times the timeout (ctest's default is 1500 s)
    get_test_property(${name} TIMEOUT _to)
    if(NOT _to OR _to STREQUAL "NOTFOUND")
        set(_to 1500)
    endif()
    math(EXPR _to "${_to} * 3")
    set_tests_properties(${name}_gl PROPERTIES TIMEOUT ${_to})
    get_test_property(${name}_gl LABELS _labels)
    set_tests_properties(${name}_gl PROPERTIES LABELS "${_labels};gl")
    set_property(GLOBAL APPEND PROPERTY ICO_RD_GL_DONE ${name}_gl)
endfunction()

if(ICO_RUN_TESTS AND ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
    set(_ico_rd_gl_tests
        # the programs
        rd_state rd_pixel rd_native rd_layout rd_tex rd_gsbase ${rd_mesh_SHARDS} modelpack
        rd_shadow rd_shadow_edges rd_fog ${rd_blur_SHARDS} rd_water rd_raw rd_debug rd_present
        rd_crt rd_mirror rd_msaa rd_perf rd_display_change rd_interp rd_boot_present rd_dst_usage
        # the two-pass blend (ICO_RD_NO_DUAL=1 is the renderer's own switch, not a Vulkan fake)
        rd_pixel_nodual rd_present_nodual rd_fog_nodual rd_shadow_nodual rd_water_nodual
        rd_raw_nodual rd_mirror_nodual rd_interp_nodual rd_msaa_nodual rd_crack_nodual
        # the blend paths, the depth copy, the fog's paths
        rd_pixel_storage rd_fog_storage rd_present_storage rd_pixel_fixed rd_fog_fixed
        rd_present_fixed rd_crt_fixed rd_pixel_attachment rd_fog_attachment rd_present_attachment
        rd_crt_attachment rd_perf_attachment rd_fog_depth_copy rd_present_depth_copy
        rd_fog_buffer rd_fog_local
        # the crack filler
        rd_crack rd_crack_4x rd_crack_frac rd_crack_backdrop rd_crack_backdrop_4x
        rd_crack_backdrop_frac rd_crack_1x rd_crack_original rd_crack_msaa rd_crack_off
        rd_crack_depth_copy rd_crack_buffer
        # the self-tests
        rd_fog_selftest rd_fog_selftest_all rd_fog_selftest_fallback ${_odd_msaa}
        rd_fog_selftest_7740 rd_fog_selftest_msaa_fallback ${_fullpixel}
        # not rd_fog_selftest_local and rd_date_selftest: their expected results
        # name the local-read fog path and the DATE stencil path, which OpenGL
        # has not (localRead false: the copy and inplace paths, DATE by snapshot);
        # nor rd_local_probe_kept and rd_local_probe_off (the desktop's probe of
        # Vulkan's local read: never on OpenGL)
        rd_shadow_selftest rd_shadow_selftest_sabotage
        rd_vuprepass_selftest rd_vuprepass_selftest_plain
        rd_vuprepass_selftest_sabotage rd_vuprepass_selftest_off rd_vuprepass_selftest_ticks
        rd_vuprepass_selftest_sabotage_tri rd_vuprepass_selftest_unbatched)
    foreach(_n IN LISTS _ico_rd_gl_tests)
        ico_rd_gl_twin(${_n})
    endforeach()
    get_property(_gl_done GLOBAL PROPERTY ICO_RD_GL_DONE)
    list(LENGTH _gl_done _gl_n)
    message(STATUS "ico: ${_gl_n} render tests registered again on OpenGL (ctest -L gl)")
endif()
# ---- OpenGL twins end ----
