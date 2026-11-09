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
# the save state's build id and the simulation keys' hash are XXH3-64 too
# (port/platform/state_file.c, sim_keys.c)
target_link_libraries(ico_platform PUBLIC ico_texpack_name)

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

# ico_cache_name: the Vulkan pipeline cache file's name from the shader set
# and the pipeline format, not the build (rd_cache_name.h).  Pure C on
# ico_texpack_name's XXH3-64, built in every configuration; ico_render walks
# its own shader table through it (rd_pipeline.c rd_pipeline_cache_file).
add_library(ico_cache_name STATIC rd_cache_name.c)
target_include_directories(ico_cache_name PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_cache_name PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_cache_name PUBLIC ico_texpack_name)

# rd_cache_name_test: the same modules give the same name whatever the build,
# one byte, a length, a name, a stage, the order or the format changed gives
# another, the name's form and its buffer (CPU only)
add_executable(rd_cache_name_test test/rd_cache_name_test.c)
target_compile_options(rd_cache_name_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_cache_name_test PRIVATE ico_cache_name)

# ico_pipe_finish: the background finisher's choices (rd_pipe_finish.h: the
# next batch of the stopped start-up set, what the finisher does after a
# picture).  Pure C, built in every configuration; ico_render keeps the keys
# and the pipelines (rd_pipeline.c rd_pipelines_background_start).
add_library(ico_pipe_finish STATIC rd_pipe_finish.c)
target_include_directories(ico_pipe_finish PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_pipe_finish PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# rd_pipe_finish_test: the stopped set's remaining keys in its order, a
# batch of one destination-read kind, each key once; the steps after a
# picture (a batch in flight, the signal, the holds, the floor, the end)
add_executable(rd_pipe_finish_test test/rd_pipe_finish_test.c)
target_compile_options(rd_pipe_finish_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_pipe_finish_test PRIVATE ico_pipe_finish)

# rd_perf_gap_test: the GPU's time between two timed replays (rd_perf_gap.h,
# issue 123): kept across untimed replays and counted, refused past 60 s, the
# first replay and a restart without one, a gap under 0 kept (CPU only)
add_executable(rd_perf_gap_test test/rd_perf_gap_test.c rd_perf_gap.c)
target_include_directories(rd_perf_gap_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_perf_gap_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# rd_submit_queue_test: the submit thread's queue (rd_submit_queue.h): jobs
# in order, at most RD_SUBMIT_DEPTH queued and running, the running job
# counted until it finishes, the finished times taken oldest first (the
# oldest dropped past the depth), the present time taken once, the waits
# and the block's means (CPU only)
add_executable(rd_submit_queue_test test/rd_submit_queue_test.c rd_submit_queue.c)
target_include_directories(rd_submit_queue_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_submit_queue_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

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
    add_test(NAME rd_cache_name COMMAND rd_cache_name_test)
    add_test(NAME rd_pipe_finish COMMAND rd_pipe_finish_test)
    add_test(NAME rd_submit_queue COMMAND rd_submit_queue_test)
    add_test(NAME rd_perf_gap COMMAND rd_perf_gap_test)
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
    rd_aura_kernel.c # [dev] aura_kernel = gs: the aura's feedback read with the GS's taps (a measurement)
    rd_aura_box.c # the aura's feedback read in two passes, the same bytes ([dev] aura_box)
    rd_pass_area.c # the passes' render areas: the tiles their 2D draws touch ([dev] pass_area)
    rd_pass_cut.c # passes cut: a clear carried, a clear nothing reads, the last touch's store ([dev] clear_carry, clear_skip, last_store)
    rd_shadow_merge.c # the shadow volumes drawn in the scene's open pass (tile-based GPUs; ICO_RD_SHADOW_MERGE the tests')
    rd_blend_selftest.c # the exact blend's start-up check on the device (issue 89)
    rd_blur.c # staticBlur.c's sprites
    rd_blur_border.c # the post sprites' replay: the reduction's open border, its outer columns
    rd_blur_smooth.c # Motion blur: Smooth, the motion blur's accumulator
    rd_blur_texels.c # the texels a staticBlur sprite reads (rd_replay.c blurSnapArea's helpers)
    rd_const_read.c # a sprite whose source holds only its clear drawn in that colour
    rd_core.c
    rd_crack_fill.c # above 1x: the pixels no draw covered between drawn ones, before the fog
    rd_crt.c # the CRT filter at present time
    rd_fsr1.c # Upscaling: FSR 1.0 (AMD FidelityFX Super Resolution 1.0) at present time
    rd_jitter.c # the scene's sub-pixel jitter after the GS's snap (a temporal upscaler's; [dev] jitter_test)
    rd_upscale_temporal.c # Upscaling: FSR 3.1's rows (support query, quality ratios, exclusions)
    rd_tone.c # the colour adjustment's settings (Options > Picture: gamma, brightness, contrast, saturation)
    rd_date_selftest.c # DATE through the stencil bit: the start-up check on the device
    rd_dst_read.c # the exact GS blend: the destination read
    rd_dump.c
    rd_fades_fold.c # list 11's last full-frame lerp sprites applied by the reduction
    rd_fog_path.c # how the fog reads the depth: the paths, the probe, the self-test
    rd_frame.c
    rd_fx_plan.c # the frame-size post sprites' plan: fixed-function forms, local read
    rd_interp.c # interpolation between the retained frames
    rd_present_clock.c # the present clock the timer path takes alpha from; Motion blur: Smooth's time between presents
    rd_interp_mesh.c # the interpolation's reads of a mesh's stream, kept while it is unchanged
    rd_reshow.c # a between-tick picture the same as the last rendered one's is shown again
    rd_glow_fold.c # the glow's mask computed in its reduction: WORK2 neither drawn nor read ([dev] glow_fold)
    rd_glow_hold.c # the between-tick presents paste the glow of the tick's first present
    rd_join.c
    rd_local_probe.c # the desktop's local reads: the start-up cost probe
    rd_mesh.c
    rd_occlusion.c # the replay's GPU draw issue and the hidden-draw measurement around it
    rd_msaa.c # the MSAA epoch's resolve pass (the covered-majority rule) and its close line
    rd_passring.c # the last passes recorded, for the device-loss report
    rd_profile.c # the device profile: what this device class gets 
    rd_content_hide.c # the content table: stage geometry made for the 4:3 picture
    rd_content_extend.c # the content table's sheets: overlays continued to the frame's edges
    rd_content_chain.c # Natural chains: the chains a stage animation lets down
    modelpack.c # model packs, the index, the glTF conversion, the dump
    rd_perf.c # per-replay performance records
    rd_perf_gap.c # the GPU's time between timed replays (pure)
    rd_pixel_plan.c # the VU pixel side at a fractional scale
    rd_pipeline.c
    rd_png.c
    rd_post.c
    rd_present.c
    rd_prerotate.c # the present turned into a swapchain made in the surface's own turn
    rd_present_info.c # rd_last_present_info's record: what the last present drew
    rd_replay.c
    rd_replay_tiler.c # the replay's tiler passes: DATE through the stencil bit, the blur fold
    rd_replay_vu.c # the replay's VU draws
    rd_vuprepass_selftest.c # the VU vertex pre-pass's start-up check on the device
    rd_vu_record.c # the VU record's write: render targets and copies, or vertex stores
    rd_vu_skin_check.c # the skinned draws' bone-block check in the game (issue 163)
    rd_vu_skin_capture.c # its capture on failure: the sidecar, the frame dump asked (issue 123)
    rd_vu_bones.c # issue 123's ladder: the bones through the ring, the evidence for the window
    rd_vu_skin_judge.c # its judgement against vu1_ref (pure)
    rd_vu_skin_selftest.c # its start-up probe on the device
    rd_window.c
    rd_upload.c # the upload ring and the uploads before a replay's first pass
    rd_velocity.c # the scene's motion vectors: each VU draw's previous transform, VELOCITY
    rd_upscale.c # the upscale point: the render/display scale split, the marker's replay, UPSCALED
    rd_upscale_hook.c # the temporal upscaler's dispatch: FSR 3.1, or the test's bilinear stand-in
    rd_fsr3.c # FSR 3.1 (the FidelityFX SDK, port/rhi/ffx): support, context, dispatch
    rd_screen_planes.c # the screen-prim triangles' planes
    rd_shadow.c
    rd_shadow_one.c # the shadow count resolved in one draw from the stencil view
    rd_shadow_selftest.c # the shadow count's one draw: the start-up check on the device
    rd_shader_plan.c # the start-up shader set's size, for the progress screens' count
    rd_store_plan.c # the depth stores and loads a replay drops: no later pass touches the depth
    rd_submit.c # a replay's end: the present's pass, the submit and the present
    rd_submit_queue.c # the submit thread's queue (pure)
    rd_submit_thread.c # the pictures' submit and present on a thread of their own
    rd_thread.c # the renderer on the window's render thread: the hand-over, the retire list
    rd_tex.c
    rd_uber.c # GS state as shader data where the device orders the colour read
    rd_tex_decode.c # the texture decode pool: rdtex_store's decodes off the game thread
    rd_water.c # block targets, aliases, camera scopes
    rd_video.c # the FMV picture (port/fmv/rd_video.h)
    rd_vis.c # the scene draws' visibility test: a draw off its target replayed as a NOP ([dev] cull)
    rd_vis_hiz.c # its occlusion test: a draw of list 0 behind the depth written before it
    rd_vis_hiz_fast.c # what its collect keeps: the collect cache, the pending scans
    texpack_dds.c # texture packs: the DDS and PNG loaders (texpack.h)
    texpack_image.c
    texpack_png.c
    texpack.c) # texture packs, the index, the loader thread, the budget
target_include_directories(ico_render
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(ico_render PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_render PUBLIC ico_rhi_vk ico_shaders ico_vu1_ref ico_texpack_name
                      ico_cache_name ico_pipe_finish)
# rd_fsr3.c: FSR 3.1 (issue 99) from the FidelityFX SDK built in port/rhi/ffx
# (option ICO_FFX, docs/BUILDING.md).  ico_ffx_upscaler is EXCLUDE_FROM_ALL;
# linked here, it is built for every program and test that links the
# renderer (the program among them), and only then.  It calls volk's
# pointers, which ico_rhi_vulkan's volk.c defines (listed after it).
# Without it rd_fsr3.c builds its "not in this build" answers alone.
if(ICO_FFX AND TARGET ico_ffx_upscaler)
    target_compile_definitions(ico_render PRIVATE ICO_FFX=1)
    target_link_libraries(ico_render PRIVATE ico_ffx_upscaler ico_rhi_vulkan)
endif()
# rd_tex_decode.c: the decode pool's threads (port/platform/ico_thread.h)
target_link_libraries(ico_render PUBLIC ico_thread)
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
add_executable(rd_replay_tool tools/rd_replay_tool.c tools/rd_replay_region.c
                              ../platform/dump_zip.c)
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

# rd_sheet_bake_test: the 1x sheet texels baked on the CPU (rd_tex.c
# rd__sheet_bake_rect) against font_sheet_ps's CPU reference (ico_sheet_ref)
# at every texel of the page and its apron: every glyph of the fonts' Latin,
# punctuation and symbol ranges and the font corpus's lines rasterised at 1x
# (font.c ui__sheet_raster_line; port/ui adds ico_ui_font_rd's objects and
# port/ui's headers) on pages packed with menu_font.c's gaps, the light inks'
# levels, the faint rim and the plain style with the dither on and off, the
# grown rectangle of a change, ink on the page's edges (CPU only)
add_executable(rd_sheet_bake_test test/rd_sheet_bake_test.c)
target_include_directories(rd_sheet_bake_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(rd_sheet_bake_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_sheet_bake_test PRIVATE ico_render ico_sheet_ref)

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

# rd_tex_pool_test: the texture decode pool (rd_tex_decode.h) against the
# game thread's decode: 200 textures of every format and TEXA mode, their
# levels, updates queued behind their creates, the bounded queue's fallback,
# byte for byte; on a device (77 without one) the uploads' alpha scans
add_executable(rd_tex_pool_test test/rd_tex_pool_test.c)
target_include_directories(rd_tex_pool_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_tex_pool_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_tex_pool_test PRIVATE ico_render)

# rd_gsbase_test: GsBase.c's host path with the 2D layer
# (GifPacket.c, DisplayList.c, DmaPacket.c) as the window build has them,
# the rest of the game stubbed: frame lifecycle, field parity, camera
# packing, Z scale and the post passes against CPU references; with
# port/game/photo_view.c, photo mode's camera in the game's
# matrices (photo_mode.c from ico_options), and port/game/eye_trail.c, whose
# session line its leave writes.
add_executable(rd_gsbase_test test/rd_gsbase_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GsBase.c"
    "${CMAKE_SOURCE_DIR}/port/game/photo_view.c"
    "${CMAKE_SOURCE_DIR}/port/game/eye_trail.c"
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
# only: no device.  --sweep DISC: every stage of the disc at the cameras of
# the corpus dumps (tools/sweep_cameras.txt), against tools/sweep_baseline.txt.
add_executable(gsb_cull_test test/gsb_cull_test.c
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GsBase.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c"
    ../platform/dump_zip.c)
target_include_directories(gsb_cull_test PRIVATE ${_ico_seki_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/shaders/test"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi"
                           "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(gsb_cull_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(gsb_cull_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
# (--sweep: the region rule through ico_vu1_ref, a dump's camera from a F12 zip)
target_link_libraries(gsb_cull_test PRIVATE ico_render ico_vu1_ref ico_math ico_options
                      ico_port_data ico_miniz ico_host_fs)

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

# rd_icon_pass_test: the title's icon pass (rd.h rd_set_icon_pass): the
# menus' prims and text left out, the head's clear the pass's colour, and a
# frame recorded after it byte-identical to one before; recorded without a
# device.
add_executable(rd_icon_pass_test test/rd_icon_pass_test.c)
target_compile_options(rd_icon_pass_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_icon_pass_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_icon_pass_test PRIVATE ico_render)

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

# rd_velocity_test: the scene's motion vectors (rd_velocity.c, the velocity
# pass of rd_replay_vu.c): the source rule on synthetic pairs (a moved mesh's
# VuPrevCB and its delta, no pair, a cut, off, two presents between ticks)
# and on a device the moving mesh's pixels in VELOCITY, SCENE unchanged, the
# dump rd_replay_tool_velocity replays
add_executable(rd_velocity_test test/rd_velocity_test.c)
target_compile_options(rd_velocity_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_velocity_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_velocity_test PRIVATE ico_render)
ico_copy_sdl3_dll(rd_velocity_test)

# rd_mirror_test: the mirror mode: the present is the exact
# flip with it on (Original, Enhanced, interpolated), UI prims land on the
# same output pixels (the replay's flip cancels the present's), SCENE's UI
# flipped about the centre.
add_executable(rd_mirror_test test/rd_mirror_test.c)
target_compile_options(rd_mirror_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_mirror_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_mirror_test PRIVATE ico_render)

# rd_jitter_test: the scene's sub-pixel jitter (rd_jitter.c): the Halton
# (2, 3) table and the switch's parse on the CPU; on a device the scene's
# sub-pixel edges moved by the jitter exactly (Original 1x, Enhanced 2x),
# 0, 0 the same bytes, a whole-texel jitter moving SCENE exactly (the
# screen planes evaluated at the jittered position) while WORK1, the
# reduction and the present take none; 77 without a device.
add_executable(rd_jitter_test test/rd_jitter_test.c)
target_compile_options(rd_jitter_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_jitter_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_jitter_test PRIVATE ico_render)

# rd_upscale_test: the upscale point (rd_upscale.c): the render scale from
# the display scale on the CPU; on a device the marker before the
# reduction, the marker a no-op with the upscaler off (DISPLAY and the
# present byte for byte), the test's upscale at Enhanced 3x over 1.5
# (UPSCALED is SCENE upscaled bilinearly, the reduction reads it, the fades
# fold off), the edge guard under a jitter, the jitter per present, the
# window resolution and Auto's cap; 77 without a device.
add_executable(rd_upscale_test test/rd_upscale_test.c)
target_compile_options(rd_upscale_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_upscale_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_upscale_test PRIVATE ico_render)

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

# rd_alpha_plan_test, rd_fx_frame_test, rd_vu_frac_test, rd_fades_fold_test:
# the pixel diet's planners (rd_alpha_plan.c, rd_fx_plan.c, rd_pixel_plan.c,
# rd_fades_fold.c), CPU only: the alpha range's truth table and the blend
# identities, the frame-size fold and the premultiplied identity, the planes
# rule's truth table and the VS->PS prefix match, and the sprite planes' skip
# (issue 161); the fades fold's step against the GS blend, its bar rows and
# its plan on recorded frames (the fade, the letterbox, the brightness, and
# the frames it leaves alone).  rd_vis_test: the scene draws' visibility test
# (rd_vis.c, rd_vis_hiz.c), CPU only: the bounds of synthetic streams, the
# plane test under random matrices, the VU references (vu1_ref) as its
# oracle in both rounding modes, the shadow frame on recorded frames, and the
# occlusion test's bounds, rasterisation and verdicts.  rd_vis_hiz_test: the
# occlusion test's cheaper collect (rd_vis_hiz.c rd__vis_hiz_mesh_ex,
# rd_vis_hiz_fast.c), CPU only: the batch screen box's bound, the fast vertex
# pass against the full one on random meshes, random frames under the plain
# and the fast collect (the same verdicts and counts, the check's none
# wrong), the collect cache's hits and misses.
foreach(_t rd_alpha_plan rd_fx_frame rd_vu_frac rd_fades_fold rd_vis rd_vis_hiz)
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

# rd_store_plan_test: the store plan's keep (rd_store_plan.c): the start's
# self-tests keep no depth (the shadow count's frame reads SCENE's depth with
# no clear of its own), a fresh target read before the frame's clear is not
# kept and the next frame drops its store, a frame reading the depth the
# replay before left keeps it from then on.
add_executable(rd_store_plan_test test/rd_store_plan_test.c)
target_compile_options(rd_store_plan_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_store_plan_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_store_plan_test PRIVATE ico_render)

# rd_pass_cut_test: the passes cut (rd_pass_cut.c), each on against off on
# frames built here: a clear carried into the next pass (one pass fewer, the
# same bytes), the clear skip's decision and its lazy clear (drawn on a read
# and at a later replay that draws on the target first, forgotten after one
# that clears it), the depth store of the pass holding the last touch
# dropped (the same bytes under the poison) and kept where a command the cut
# does not take lies between.
add_executable(rd_pass_cut_test test/rd_pass_cut_test.c)
target_compile_options(rd_pass_cut_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_pass_cut_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_pass_cut_test PRIVATE ico_render)

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
# rd_vu_skin_check_test: the skinned draws' bone-block check (issue 163;
# rd_vu_skin_check.c, rd_vu_skin_judge.c, rd_vu_skin_selftest.c): the
# judgement alone, then on a device the start-up probe and a frame of one
# skinned command checked clean and under the two sabotages (the log lines
# are matched by test/rd_vu_skin_check.cmake).
add_executable(rd_vu_skin_check_test test/rd_vu_skin_check_test.c)
target_compile_options(rd_vu_skin_check_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_vu_skin_check_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_vu_skin_check_test PRIVATE ico_render)

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

# weapon_draw_test: the insect net of Gameplay > Combat's Stick row through
# the game's weapon code: sugipon/src/weapon.c with lineManager.c,
# matrixDrive.c and the 2D layer as the window build has them, the variants
# (ico_options) linked, the rest of the game stubbed; recording only. Per
# disc (its InitWeaponGeo, WeaponGeo and WeaponDL copies): the layout stick
# made as the net with no torch, found and picked up by the boy, then held
# and drawn as dispInsectNet's 17 grey lines in list 11 at his hand and no
# model; at Original the stick with its torch and its model. CPU only.
add_executable(weapon_draw_test test/weapon_draw_test.c
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/weapon.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/lineManager.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/matrixDrive.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/Matrix.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(weapon_draw_test PRIVATE ${_ico_sugipon_incs}
                           "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/math"
                           "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/platform"
                           "${CMAKE_SOURCE_DIR}/port/include")
target_compile_definitions(weapon_draw_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(weapon_draw_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(weapon_draw_test PRIVATE ico_render ico_options ico_math)

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

# rd_png_mem_test: rd_write_png_mem (rd_png.c) read back through the texture
# pack's PNG loader, and equal to the bytes rd_write_png puts in a file.
add_executable(rd_png_mem_test test/rd_png_mem_test.c)
target_compile_options(rd_png_mem_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_png_mem_test PRIVATE ico_render ico_miniz ico_host_fs)

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
# ico_thread: the loader thread puts itself back on the process's cores
# (cpu_place.h cpu_place_thread_free)
target_link_libraries(texpack_test PRIVATE ico_host_fs ico_texpack_name ico_region ico_thread)
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
ico_copy_sdl3_dll(texpack_test texpack_e2e_test texpack_export_test rd_replay_tool rd_state_test
                  rd_sheet_bake_test rd_pixel_test
                  rd_layout_test rd_tex_test rd_tex_pool_test rd_gsbase_test rd_mesh_test modelpack_test
                  rd_shadow_test rd_fog_test rd_fog_selftest_test rd_blend_selftest_test
                  rd_vuprepass_selftest_test rd_vu_skin_check_test rd_blur_test rd_water_test
                  rd_raw_test rd_debug_test
                  rd_present_test rd_filter_test rd_icon_pass_test rd_interp_test rd_still_test rd_mirror_test rd_msaa_test rd_mip_test
                  rd_jitter_test rd_upscale_test
                  rd_perf_test rd_display_change_test rd_boot_present_test rd_crt_test
                  texpack_load_test rd_png_mem_test gsb_cull_test
                  rd_native_test rd_passring_test rd_stream_audit_test rd_alpha_plan_test
                  rd_fx_frame_test rd_vu_frac_test rd_crack_test rd_local_probe_test rd_vis_test
                  rd_vis_hiz_test)

if(ICO_RUN_TESTS)
    add_test(NAME rd_state COMMAND rd_state_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME rd_sheet_bake
             COMMAND rd_sheet_bake_test "${CMAKE_SOURCE_DIR}/port/ui/test/font_corpus")
    set_tests_properties(rd_sheet_bake PROPERTIES TIMEOUT 900)
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
    add_test(NAME rd_tex_pool COMMAND rd_tex_pool_test)
    set_tests_properties(rd_tex_pool PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_gsbase COMMAND rd_gsbase_test)
    set_tests_properties(rd_gsbase PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME gsb_cull COMMAND gsb_cull_test)
    # st18a's parts from the disc against the cameras about the dump i76's:
    # 77 without the disc image
    add_test(NAME gsb_cull_stage COMMAND gsb_cull_test --stage "${ICO_DISC_IMAGE}")
    set_tests_properties(gsb_cull_stage PROPERTIES SKIP_RETURN_CODE 77)
    # every stage of the disc at the cameras of the corpus dumps (issue 29's regression gate):
    # the box cull, the blend span and the region rule's drops per stage and aspect against
    # tools/sweep_baseline.txt; eight shards, each the stages numbered k mod 8; main only (77
    # without the disc image).  A manual full run: gsb_cull_test --sweep DISC --sweep-step 1 --eye
    # --out DIR (docs/port/TESTING_PROGRAMS.md)
    ico_add_sharded_test(gsb_cull_sweep 8 PROGRAM gsb_cull_test
                         ARGS --sweep "${ICO_DISC_IMAGE}"
                              --cameras "${CMAKE_SOURCE_DIR}/tools/sweep_cameras.txt"
                              --baseline "${CMAKE_SOURCE_DIR}/tools/sweep_baseline.txt"
                         PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
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
    # Upscaling: FSR 1.0 (issue 99): the 1x picture (the dump's 512x256
    # DISPLAY line-doubled to 512x512) EASU-upscaled and RCAS-sharpened to
    # the 960x720 box, named by --stats (at 640x480 the box has fewer rows
    # than the picture and FSR 1.0 does not run)
    add_test(NAME rd_replay_tool_fsr1
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_fsr1.png" --present 960x720
                     --fsr1 --stats)
    set_tests_properties(rd_replay_tool_fsr1 PROPERTIES
                         PASS_REGULAR_EXPRESSION "present: fsr1 512x512 -> 960x720"
                         SKIP_RETURN_CODE 77 FIXTURES_REQUIRED rd_dump)
    # Upscaling: FSR 3.1 (issue 99; rd_fsr3.c): the dump's frame at Enhanced
    # in the 1280x960 box (the display scale the box's) at Quality (1.5),
    # SCENE upscaled by the FidelityFX upscaler into UPSCALED at the upscale
    # point, under the validation layer (the environment below): --stats
    # names the presents it ran and the layer's errors.  A dump holds no
    # history (each run starts FSR from empty), so this proves the dispatch
    # runs, not FSR's picture.  77 only where FSR 3.1 cannot be expected (a
    # build without the upscaler, not Vulkan, a device below Vulkan 1.2), with
    # the reason; a build with it on a 1.2 device that refuses it fails
    add_test(NAME rd_replay_tool_fsr3
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_fsr3.png" --enhanced
                     --fsr3 quality --present 1280x960 --stats)
    set_tests_properties(rd_replay_tool_fsr3 PROPERTIES
                         PASS_REGULAR_EXPRESSION "upscale: fsr3 ran [1-9][0-9]* presents \\(context render [0-9]+x[0-9]+ -> display [0-9]+x[0-9]+, [^)]*, refused 0, failed 0, [^)]*\\), validation errors 0"
                         SKIP_RETURN_CODE 77 FIXTURES_REQUIRED rd_dump)
    # the scene's jitter (rd_jitter.h; test/rd_replay_tool_jitter.cmake): at
    # Original 1x and Enhanced 2x, --jitter 0,0 draws every target and the
    # present byte for byte as without it, --jitter 0.25,0.25 moves SCENE and
    # the present, ICO_RD_JITTER_TEST ([dev] jitter_test) equals --jitter,
    # --stats names it; a jitter outside -0.5..0.5 is refused
    add_test(NAME rd_replay_tool_jitter
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_jitter"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_jitter.cmake")
    set_tests_properties(rd_replay_tool_jitter PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                          FIXTURES_REQUIRED rd_dump TIMEOUT 600)
    # the upscale point (rd_upscale.h): the dump's 512x512 frame at Enhanced
    # 3x with the test's upscaler at 1.5 (FSR's Quality): SCENE at 2x, the
    # reduction's marker dispatched once, UPSCALED at 3x read by the
    # reduction, the jitter's phase count 8 (the present takes term 0)
    add_test(NAME rd_replay_tool_upscale_test
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_upscale_test.png" --present 960x720
                     --enhanced --resolution 3x --upscale-test 1.5 --upscale-phases 8 --stats)
    set_tests_properties(rd_replay_tool_upscale_test PROPERTIES
                         PASS_REGULAR_EXPRESSION "upscale point: SCENE 1024x1024 -> UPSCALED 1536x1536 \\(render scale 2x2, display scale 3x3\\), markers [1-9][0-9]*, dispatched [1-9][0-9]*, refused 0, reductions remapped [1-9]"
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
    # the motion vectors (rd_velocity.h; test/rd_replay_tool_velocity.cmake):
    # --velocity --velocity-dump on rd_velocity's moving mesh writes a PNG
    # with moving texels; every target and the present the same bytes with
    # and without --velocity on that dump and on rd_pixel's (Off identity)
    add_test(NAME rd_replay_tool_velocity
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_velocity_moving.rddump"
                     "-DPIXEL=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_velocity"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_velocity.cmake")
    set_tests_properties(rd_replay_tool_velocity
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    FIXTURES_REQUIRED "rd_dump;rd_velocity_dump" TIMEOUT 300)
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
    # the present turned into a pre-rotated swapchain's headless twin
    # (ICO_VK_FAKE_TRANSFORM=90, 180, 270; rd_prerotate.c) and read back turned
    # back is the identity present byte for byte: rd_pixel's dump plain, wide,
    # with the overlay test pattern and under the CRT filter, and the dumps of
    # tools/prerotate_dumps.txt that are here
    add_test(NAME rd_replay_tool_prerotate
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/prerotate_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_prerotate"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_prerotate.cmake")
    set_tests_properties(rd_replay_tool_prerotate PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             FIXTURES_REQUIRED rd_dump TIMEOUT 1200)
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
    # limits, with the tile-based GPU's paths forced and under GS state as shader data
    # (ICO_RD_UBER=on, the same paths); skips without ICO_DUMPS or a device.
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
    add_test(NAME weapon_draw COMMAND weapon_draw_test pal "${CMAKE_CURRENT_BINARY_DIR}")
    foreach(_slug jpn kor cn ntsc aug6 jun26)
        add_test(NAME weapon_draw_${_slug}
                 COMMAND weapon_draw_test ${_slug} "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
    add_test(NAME rd_debug COMMAND rd_debug_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_debug PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_present COMMAND rd_present_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_present PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_crt COMMAND rd_crt_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_crt PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_filter COMMAND rd_filter_test)
    add_test(NAME rd_icon_pass COMMAND rd_icon_pass_test)
    add_test(NAME rd_interp COMMAND rd_interp_test)
    # the motion vectors; leaves rd_velocity_moving.rddump (a frame with
    # VuPrevCBs) for rd_replay_tool_velocity
    add_test(NAME rd_velocity COMMAND rd_velocity_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_velocity PROPERTIES FIXTURES_SETUP rd_velocity_dump)
    add_test(NAME rd_still COMMAND rd_still_test)
    add_test(NAME rd_mirror COMMAND rd_mirror_test)
    set_tests_properties(rd_mirror PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_msaa COMMAND rd_msaa_test)
    set_tests_properties(rd_msaa PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_jitter COMMAND rd_jitter_test)
    set_tests_properties(rd_jitter PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_upscale COMMAND rd_upscale_test)
    set_tests_properties(rd_upscale PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_perf COMMAND rd_perf_test)
    set_tests_properties(rd_perf PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME rd_display_change COMMAND rd_display_change_test)
    set_tests_properties(rd_display_change PROPERTIES SKIP_RETURN_CODE 77)
    foreach(_t rd_alpha_plan rd_fx_frame rd_vu_frac rd_fades_fold rd_vis rd_vis_hiz)
        add_test(NAME ${_t} COMMAND ${_t}_test)
        set_tests_properties(${_t} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    add_test(NAME rd_boot_present COMMAND rd_boot_present_test "${ICO_BOOT_DUMP_DIR}")
    set_tests_properties(rd_boot_present PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500)
    if(ICO_BOOT_DUMP_OLD)
        set_tests_properties(rd_boot_present PROPERTIES ENVIRONMENT "ICO_BOOT_OLD=1")
    endif()
    add_test(NAME rd_png_mem COMMAND rd_png_mem_test "${CMAKE_CURRENT_BINARY_DIR}")
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
        set_tests_properties(rd_pixel rd_replay_tool rd_layout rd_tex rd_tex_pool rd_gsbase ${rd_mesh_SHARDS} modelpack
                             rd_shadow rd_shadow_edges rd_fog rd_raw rd_debug rd_present rd_crt rd_interp rd_mirror rd_msaa rd_perf rd_display_change rd_native PROPERTIES ENVIRONMENT
                                                                           "${_env}")
        set_tests_properties(${rd_blur_SHARDS} PROPERTIES ENVIRONMENT "${_env}")
        set_tests_properties(rd_velocity PROPERTIES ENVIRONMENT "${_env}")
        # the jitter and the upscale point under the layer too; their OpenGL
        # twins inherit the environment, with ASAN_OPTIONS=detect_leaks=0
        # (llvmpipe's LLVM leaks under LeakSanitizer: CI's asan shards failed
        # rd_jitter_gl and rd_upscale_gl, 2026-11-08)
        set_tests_properties(rd_jitter rd_upscale PROPERTIES ENVIRONMENT "${_env}")
        if(TEST rd_replay_tool_fsr3)
            # FSR 3.1's dispatch under the layer (rd_replay_tool's --stats
            # counts its errors)
            set_tests_properties(rd_replay_tool_fsr3 PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endif()
    # rd_blur's glow fold case (k) alone on the tile class's fake
    # (ICO_VK_FAKE_LIMITS=moltenvk: the Apple row of the device profile):
    # the profile's glow fold off by default there (rd_profile.c, the
    # PowerVR phone's 8 ms a glow picture), so post mode 7 not forced draws
    # the mask's pass; _on with [dev] glow_fold = true (ICO_RD_GLOW_FOLD=1)
    # folds it.  The same bytes either way, and every forced frame as in
    # the shards
    set(_gft "${CMAKE_CURRENT_BINARY_DIR}/glow_fold_tile")
    file(MAKE_DIRECTORY "${_gft}")
    add_test(NAME rd_blur_glow_fold_tile
             COMMAND rd_blur_test --only glow_fold --glow-fold-default off
             WORKING_DIRECTORY "${_gft}")
    add_test(NAME rd_blur_glow_fold_tile_on
             COMMAND rd_blur_test --only glow_fold --glow-fold-default on
             WORKING_DIRECTORY "${_gft}")
    set_tests_properties(rd_blur_glow_fold_tile rd_blur_glow_fold_tile_on
                         PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
    set_tests_properties(rd_blur_glow_fold_tile PROPERTIES
                         ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk")
    set_tests_properties(rd_blur_glow_fold_tile_on PROPERTIES
                         ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk;ICO_RD_GLOW_FOLD=1")
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
    # issue 182 (the Vulkan device loss with MSAA on): rd_msaa with the
    # reporter's NVIDIA profile forced (the fog's local read, DATE through
    # the stencil, the effects' local read, the seven-draw shadow count by
    # the profile), under the validation layer where it is installed:
    # testShadowCount's counts by every form under MSAA4, no shadow reset
    # pass of its own, no validation message
    set(_env_nvpath ${_env} "ICO_RD_FOG_PATH=local" "ICO_RD_DATE_PATH=stencil"
                    "ICO_RD_FX_DST_LOCAL=1" "ICO_RD_SHADOW_ONE=0")
    add_test(NAME rd_msaa_nvpath COMMAND rd_msaa_test)
    set_tests_properties(rd_msaa_nvpath PROPERTIES SKIP_RETURN_CODE 77
                                                   ENVIRONMENT "${_env_nvpath}")
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
    # the VU record's two writes (rd_vu_record.h): rd_pixel's pictures and
    # hashes with the record stored by the vertex shaders (the default on
    # lavapipe, asked by name) and drawn into the render targets and copied
    set(_vrs "${CMAKE_CURRENT_BINARY_DIR}/vu_record_stores")
    set(_vrt "${CMAKE_CURRENT_BINARY_DIR}/vu_record_targets")
    file(MAKE_DIRECTORY "${_vrs}" "${_vrt}")
    add_test(NAME rd_pixel_stores COMMAND rd_pixel_test "${_vrs}")
    add_test(NAME rd_pixel_targets COMMAND rd_pixel_test "${_vrt}")
    set_tests_properties(rd_pixel_stores PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_vrs}"
                                                    ENVIRONMENT "${_env};ICO_RD_VU_RECORD=stores")
    set_tests_properties(rd_pixel_targets PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_vrt}"
                                                     ENVIRONMENT "${_env};ICO_RD_VU_RECORD=targets")
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
    set(_crk_tail "texels: a run of texels no draw covered, or drawn over 1/64 of the depth behind both its ends, with a texel in front at each end takes the nearer end's colour after lists 4-6\\). \\[dev\\] crack_fill = false turns it off")
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
    # (rd_crack_test 2 fold, 77 without local read; the old fold's name): on
    # the fog's local read the fog draws in the scene pass and the fill after
    # lists 4-6 (one pass more than without the filler, the fill's own; the
    # fog by the local read in the same replay) and SCENE equals the depth
    # copy path's byte for byte; _mali
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
                         "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail}"
                         "-DDENY=drawn inside the scene pass with the local fog" -P "${_crk}")
    endforeach()
    set_tests_properties(rd_crack_tiler rd_crack_tiler_fold PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                   ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk")
    set_tests_properties(rd_crack_tiler_on PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                      ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk;ICO_RD_CRACK_FILL=1")
    set_tests_properties(rd_crack_tiler_fold_mali PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=mali;ICO_RD_FOG_PATH=local")
    # the fill's place (rd_crack_fill.c rd__crack_fill_place) on the tile
    # class's fakes: rd_crack_glow after the glow's cleanUpFB (its two
    # sprites of SCENE's alpha alone stay in the fog's pass, the fill writes
    # RGB alone after them), rd_crack_aura the first draw of the aura's
    # paste pass; each against the same frame with the place pinned to the
    # start of list 7: SCENE's bytes the same, two (glow) or one (aura)
    # render passes fewer; the once lines matched
    add_test(NAME rd_crack_glow
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|glow"
                     "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail}"
                     "-DEXPECT2=rd: crack fill: drawn after the glow's sprites that write SCENE's alpha alone"
                     -P "${_crk}")
    add_test(NAME rd_crack_aura
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|aura"
                     "-DEXPECT=${_crk_on} 2x2, reach 1x1 ${_crk_tail}"
                     "-DEXPECT2=rd: crack fill: drawn as the first draw of the aura's paste pass on SCENE"
                     "-DDENY=rd: crack fill: the aura's paste cannot take the fill" -P "${_crk}")
    set_tests_properties(rd_crack_glow rd_crack_aura PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk")
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
                         "-DEXPECT2=stats: crack fill on: 1 passes, [1-9][0-9]* pixels filled \\(1 replays counted; scale 2x2, reach 1x1 texels; 0 fogged replays above 1x not filled; 1 fills after lists 4-6; 1 fills after the MSAA resolve; 0 fills in the aura's paste pass; 0 fills after the glow's alpha\\)"
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
    # the pinned SHA-256s are of the pictures without the fades fold: above 1x the
    # brightness step folded into the reduction (GPUROUND) moves the ripple frame's
    # pixels by one level (591,187 of them on v83439), inside the fold's bound, which
    # rd_fades_fold_ab tests; these tests pin the water's exact bytes
    set_tests_properties(rd_replay_tool_ripple rd_replay_tool_ripple_447 rd_replay_tool_ripple_436
                         rd_replay_tool_mirror_floor
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 600
                                    ENVIRONMENT "ICO_RD_FADES_FOLD=0")
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
    # the blended present's frame F12 saves (rd.h rd_dump_blended_arm),
    # written by --save-blended from --interp T PREV, replayed on its own:
    # SCENE and the presented picture byte for byte the --interp run's, and
    # the same --interp run with the interpolation and the replays on the
    # render thread (--render-thread sync and on: the picture and the saved
    # frame byte for byte the main thread's).
    # rd-01076 then rd-01077 at alpha 0.5, Enhanced 4:3 2x; the issue 163
    # pair (the tablet's tick and the tick before) at its present's alpha
    # 0.999000013 (the lavapipe A/B; Not Run without the dumps)
    add_test(NAME rd_replay_tool_save_blended
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DPREV=${_gh}/rd-01076.rddump" "-DCUR=${_gh}/rd-01077.rddump" -DT=0.5
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_save_blended"
                     "-DARGS=--enhanced|--aspect|4:3|--resolution|2x" "-DRT=sync|on"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_save_blended.cmake")
    set(_sb163 "${CMAKE_SOURCE_DIR}/build-host/tmp/v097/evidence/163/frame/frame-20261031-211530-v10103")
    add_test(NAME rd_replay_tool_save_blended_163
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DPREV=${_sb163}-prev.rddump" "-DCUR=${_sb163}.rddump" -DT=0.999000013
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_save_blended_163"
                     "-DARGS=--resolution|2x|--full-height|--full-pixel" "-DRT=sync|on"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_save_blended.cmake")
    set_tests_properties(rd_replay_tool_save_blended rd_replay_tool_save_blended_163
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 900)
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
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}" -DGLYPH=font_sheet_baked_ps
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
                     -DGLYPH=font_sheet_ps
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
    # GS state as shader data (rd_uber.c, ICO_RD_UBER=on: lavapipe orders the
    # attachment read): the attachment read taken for it and checked (the
    # intra-draw overlap cell among the samples), kept; read white
    # (ICO_RD_BLEND_SABOTAGE=attachment) it stays, [dev] uber = on keeping
    # it, the failure logged; ICO_RD_BLEND=fixed (the start-up retry's)
    # turns it off: the fixed-function blend
    set(_bst_uber "fixed-function ok [0-2], GS state as shader data")
    add_test(NAME rd_blend_selftest_uber
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=attachment
                     "-DNAME=${_bst_att}" "-DRESULTS=attachment ok [01], ${_bst_uber}"
                     "-DEXTRA=rd: pipelines: GS state as shader data: on \\(raster-order read, \\[dev\\] uber = on\\)"
                     "-DEXTRA2=rd: profile: [^\n]*. GS state as shader data \\(raster-order read\\)"
                     -P "${_bst}")
    add_test(NAME rd_blend_selftest_uber_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=attachment
                     "-DNAME=${_bst_att}" "-DRESULTS=attachment fail [0-9]+, ${_bst_uber}"
                     "-DFAIL=attachment fail [0-9]+" -P "${_bst}")
    add_test(NAME rd_blend_selftest_uber_blend_fixed
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}"
                     "-DEXTRA=rd: pipelines: GS state as shader data: off \\(\\[dev\\] blend = fixed\\)"
                     -P "${_bst}")
    # the glyph cell through the sheet shader where the device draws it
    # ([dev] sheet_shader, ICO_RD_SHEET_SHADER=1); the default above takes
    # the 1x bake's font_sheet_baked_ps, an exact mode asked font_sheet_ps
    # (issue 188)
    add_test(NAME rd_blend_selftest_sheet_shader
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_blend_selftest_test> -DMODE=fixed
                     "-DNAME=${_bst_fix}" "-DRESULTS=${_bst_ok}" -DGLYPH=font_sheet_ps
                     -P "${_bst}")
    set(_bst_tests rd_blend_selftest rd_blend_selftest_sabotage rd_blend_selftest_adreno
                   rd_blend_selftest_adreno_forced rd_blend_selftest_no_dst
                   rd_blend_selftest_blend_attachment rd_blend_selftest_blend_exact
                   rd_blend_selftest_blend_storage rd_blend_selftest_blend_fixed
                   rd_blend_selftest_moltenvk_storage rd_blend_selftest_uber
                   rd_blend_selftest_uber_sabotage rd_blend_selftest_uber_blend_fixed
                   rd_blend_selftest_sheet_shader)
    set_tests_properties(${_bst_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_blend_selftest PROPERTIES ENVIRONMENT "${_bst_env}")
    set_tests_properties(rd_blend_selftest_sheet_shader
                         PROPERTIES ENVIRONMENT "${_bst_env};ICO_RD_SHEET_SHADER=1")
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
    set_tests_properties(rd_blend_selftest_uber PROPERTIES ENVIRONMENT "${_bst_env};ICO_RD_UBER=on")
    set_tests_properties(rd_blend_selftest_uber_sabotage
                         PROPERTIES ENVIRONMENT
                                    "${_bst_env};ICO_RD_UBER=on;ICO_RD_BLEND_SABOTAGE=attachment")
    set_tests_properties(rd_blend_selftest_uber_blend_fixed
                         PROPERTIES ENVIRONMENT "${_bst_env};ICO_RD_UBER=on;ICO_RD_BLEND=fixed")
    # Under asan, the twins whose glyph goes through font_sheet_ps (an exact
    # mode asked, or [dev] sheet_shader) end with LeakSanitizer's 112 bytes
    # (2 x 56, calloc) on Ubuntu 24.04's lavapipe (Mesa 25.2.8): every frame
    # is in the driver, on a thread the driver started (asan_thread_start
    # with no port frame: the driver compiles its shader variants at draw
    # time, and v0.10.2's branch-free font_sheet_ps is what changed; v0.10.1,
    # whose every twin drew the glyph through the old one, was clean on the
    # same Mesa build; the dev container's Mesa 25.0.7 is clean); the roots are unscanned
    # once the loader dlcloses it at vkDestroyInstance (the rhi_vk and
    # settings_render leak, port/rhi/CMakeLists.txt). An LSan suppression
    # cannot name it (the frames are "<unknown module>" after the unload).
    # The leak is not steady: CI run 41925244605 showed it on the four
    # font_sheet_ps twins and 41937286231 on _adreno, _adreno_forced and
    # _uber (the baked-glyph ones) instead, the same 2 x 56 bytes, so it is
    # the driver's compile thread racing the exit, which any twin can hit;
    # the leak check is off for the whole family under asan
    if(ICO_SANITIZE MATCHES "address")
        set_property(TEST rd_blend_selftest rd_blend_selftest_sabotage rd_blend_selftest_adreno
                          rd_blend_selftest_adreno_forced rd_blend_selftest_no_dst
                          rd_blend_selftest_blend_attachment rd_blend_selftest_blend_exact
                          rd_blend_selftest_blend_storage rd_blend_selftest_blend_fixed
                          rd_blend_selftest_moltenvk_storage rd_blend_selftest_uber
                          rd_blend_selftest_uber_sabotage rd_blend_selftest_uber_blend_fixed
                          rd_blend_selftest_sheet_shader
                     APPEND PROPERTY ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    endif()
    # the VU vertex pre-pass's self-test (rd_vuprepass_selftest.c): on
    # lavapipe the pre-pass draws the self-test's meshes byte for byte as
    # the programs' entries do, over its four moved replays in both frame
    # slots, and stays on, in the extended variant (a wide scaled scene)
    # and the plain one (Original at 4:3 and 1x); with its first command's
    # records drawn a texel off (ICO_RD_VU_PREPASS_SABOTAGE, a driver whose
    # record goes wrong) the pictures differ and it is turned off before the
    # set; with it off by the settings (ICO_RD_VU_PREPASS=0) nothing runs
    set(_vst "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vuprepass_selftest.cmake")
    set(_vst_on "on \\(self-test: prelit, lit, skin(, the tick cameras)? and particles byte for byte over 4 replays in both frame slots, [0-9]+ vertices and 4 sprites in [0-9]+ pre-pass draws \\(2 passes, [0-9]+ triangle slots\\), [0-9]+ of [0-9]+ pixels drawn\\) on SCENE [0-9]+x[0-9]+ at [0-9]+x[0-9]+ texels in [0-9.]+ ms")
    set(_vst_off "off \\(self-test: the pictures differ: in replay [1-4] of 4 \\(frame slot [01]\\), [0-9]+ of [0-9]+ bytes, the first at texel [0-9]+,[0-9]+: [0-9,]+ want [0-9,]+\\) on SCENE")
    # a stale record (ICO_RD_VU_PREPASS_SABOTAGE=stale: the record's copies
    # left out from the second replay on, so a replay reads what an earlier
    # one left in its frame slot's buffer): the pictures differ from replay
    # 2 on, never in replay 1.  The self-test of v0.10.x replayed the frame
    # once (replay 1 alone) and passed this sabotage: only the moved replays
    # across both frame slots see a record that straddles two pictures
    set(_vst_stale "off \\(self-test: the pictures differ: in replay [2-4] of 4 \\(frame slot [01]\\), [0-9]+ of [0-9]+ bytes, the first at texel [0-9]+,[0-9]+: [0-9,]+ want [0-9,]+\\) on SCENE")
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
    add_test(NAME rd_vuprepass_selftest_sabotage_straddle
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                     "-DLINE=${_vst_stale}"
                     "-DEXTRA=rd: VU pre-pass: ICO_RD_VU_PREPASS_SABOTAGE: in the self-test the record's copies"
                     -P "${_vst}")
    # the same with the pre-pass's draws one an entry and stage
    # (ICO_RD_VU_PREPASS_BATCH=0: the vu_X_pre_vs builds, as before the
    # batches; rd_replay_vu.c vuPrepassRun)
    add_test(NAME rd_vuprepass_selftest_unbatched
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" -P "${_vst}")
    # and with every VU draw a GPU draw of its own (ICO_RD_VU_MERGE=0: the
    # programs' and the main shader's plain builds, as before the draw
    # merge; rd_replay_vu.c vuMergeInit)
    add_test(NAME rd_vuprepass_selftest_unmerged
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" -P "${_vst}")
    # the record's two writes (rd_vu_record.h; issue 179): lavapipe has
    # vertexPipelineStoresAndAtomics, so the runs above take the stores (the
    # shipped path, [dev] vu_record = auto).  _stores and _stores_plain ask
    # for them by name and must say so on the verdict line; _targets keeps
    # the render targets and copies; _stores_sabotage_straddle: a stale
    # record under the stores (replays 2 on store into the sink buffer) fails
    # the stores' run from replay 2 on, which is logged with its first
    # texel, and the targets' run after it fails the same way (the final
    # verdict); _nostores_fallback: a device made without the feature
    # (ICO_VK_FAKE_NO_VERTEX_STORES=1) says so on the start line, keeps the
    # targets and passes
    set(_vst_stores_line "rd: VU pre-pass: on \\(self-test: [^\n]* ms, record stores")
    set(_vst_targets_line "rd: VU pre-pass: on \\(self-test: [^\n]* ms, record targets")
    add_test(NAME rd_vuprepass_selftest_stores
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_vst_stores_line}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_stores_plain
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     -DVARIANT=plain "-DLINE=${_vst_on}" "-DEXTRA=${_vst_stores_line}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_targets
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_vst_targets_line}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_stores_sabotage_straddle
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                     "-DLINE=${_vst_stale}"
                     "-DEXTRA=rd: VU pre-pass: the record stored by the vertex shaders failed the self-test \\(the pictures differ: in replay [2-4] of 4"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nostores_fallback
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=the record drawn into 3 RGBA32_UINT render targets and copied into the frame slot's record buffer \\(no vertex stores: the device has no vertexPipelineStoresAndAtomics"
                     -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_stores rd_vuprepass_selftest_stores_plain
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=stores")
    set_tests_properties(rd_vuprepass_selftest_targets
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=targets")
    set_tests_properties(rd_vuprepass_selftest_stores_sabotage_straddle
                         PROPERTIES ENVIRONMENT
                                    "${_env};ICO_RD_VU_RECORD=stores;ICO_RD_VU_PREPASS_SABOTAGE=stale")
    set_tests_properties(rd_vuprepass_selftest_nostores_fallback
                         PROPERTIES ENVIRONMENT "${_env};ICO_VK_FAKE_NO_VERTEX_STORES=1")
    # the device table's no-pre-pass row (vk_device.c kVuPlainOnly, issue
    # 123: the Adreno 740 under Qualcomm's own driver), given to lavapipe by
    # ICO_VK_FAKE_NO_PREPASS=1: devtable the pre-pass off by the table and
    # the self-test not run; devtable_on the same with ICO_RD_VU_PREPASS=on
    # ([dev] vu_prepass = true written in config.toml): on over the table,
    # the self-test passed, the start line naming the override after the
    # record's words ("...; the record <why>; [dev] vu_prepass = true over
    # the device table (<reason>)")
    add_test(NAME rd_vuprepass_selftest_devtable
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test>
                     -DMODE=notrun "-DLINE=off \\(self-test not run\\)"
                     "-DEXTRA=rd: VU pre-pass: off by the device table \\(ICO_VK_FAKE_NO_PREPASS, \\[dev\\] vu_prepass = true draws with it anyway\\): every VU draw"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_devtable_on
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU pre-pass: on: each mesh [^\n]*, the record [^\n]*, \\[dev\\] vu_prepass = true over the device table \\(ICO_VK_FAKE_NO_PREPASS\\)"
                     -P "${_vst}")
    # the skin check at its every-draw mode around the self-test (issue
    # 123's v0.11.0 log: the self-test's skinned toy mesh twinned, armed the
    # capture and a frame dump): the self-test's replays are neither checked
    # nor twinned, so no check of a frame and no end-of-run count is logged
    add_test(NAME rd_vuprepass_selftest_skincheck
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU skin check: on "
                     "-DNOT_=VU skin check: (frame|at the end of the run|twins at the end of the run)"
                     -P "${_vst}")
    set(_vst_tests rd_vuprepass_selftest rd_vuprepass_selftest_plain
                   rd_vuprepass_selftest_sabotage rd_vuprepass_selftest_off
                   rd_vuprepass_selftest_unbatched rd_vuprepass_selftest_unmerged
                   rd_vuprepass_selftest_sabotage_straddle rd_vuprepass_selftest_stores
                   rd_vuprepass_selftest_stores_plain rd_vuprepass_selftest_targets
                   rd_vuprepass_selftest_stores_sabotage_straddle
                   rd_vuprepass_selftest_nostores_fallback rd_vuprepass_selftest_devtable
                   rd_vuprepass_selftest_devtable_on rd_vuprepass_selftest_skincheck)
    set_tests_properties(${_vst_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    if(_env)
        set_tests_properties(rd_vuprepass_selftest rd_vuprepass_selftest_plain
                             PROPERTIES ENVIRONMENT "${_env}")
    endif()
    set_tests_properties(rd_vuprepass_selftest_sabotage
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS_SABOTAGE=1")
    set_tests_properties(rd_vuprepass_selftest_sabotage_straddle
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS_SABOTAGE=stale")
    set_tests_properties(rd_vuprepass_selftest_unbatched
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS_BATCH=0")
    set_tests_properties(rd_vuprepass_selftest_unmerged
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_MERGE=0")
    set_tests_properties(rd_vuprepass_selftest_off
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_PREPASS=0")
    set_tests_properties(rd_vuprepass_selftest_devtable
                         PROPERTIES ENVIRONMENT "${_env};ICO_VK_FAKE_NO_PREPASS=1")
    set_tests_properties(rd_vuprepass_selftest_devtable_on
                         PROPERTIES ENVIRONMENT "${_env};ICO_VK_FAKE_NO_PREPASS=1;ICO_RD_VU_PREPASS=on")
    set_tests_properties(rd_vuprepass_selftest_skincheck
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_CHECK=2;ICO_RD_VU_SKIN_INTERVAL_MS=0")
    # the skinned draws' bone-block check (issue 163; rd_vu_skin_check.c,
    # docs/port/VU_SKIN_CHECK.md, test/rd_vu_skin_check.cmake): judge the
    # judgement alone (no device: a fake probe at the zero matrix's point
    # names address 200, the byte windows, the decision table's words);
    # clean the start-up probe right on lavapipe and a frame of one skinned
    # command checked right with its box check; probe and ring the same
    # frame with the bone at 200 read as zeros by the probe alone (H1,
    # address-dependent: c1 right) or in the ring under the GPU (H2, a port
    # race); robust the clean frame under robust access with the c2 variant
    # drawn where the device offers VK_EXT_pipeline_robustness, probe_robust
    # the probe's sabotage there with c2 right (the pipeline robustness
    # chain read back)
    set(_vsk "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_skin_check.cmake")
    set(_vsk_wrong "rd: VU skin check: frame [0-9]+ command 0:[0-9]+ mesh rd vu skin check test #[0-9]+ \\(120 vertices\\): [0-9]+ vertices wrong of 120, address 200: got \\([-0-9., ]+\\) want \\([-0-9., ]+\\) \\(a zero matrix at 200 gives it: yes")
    add_test(NAME rd_vu_skin_check_judge
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=judge
                     -P "${_vsk}")
    add_test(NAME rd_vu_skin_check
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=clean
                     "-DNOT_=vertices wrong" -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_robust
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=clean
                     "-DNOT_=vertices wrong" -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_probe
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=probe
                     "-DEXTRA=${_vsk_wrong}.*the bone block as the GPU copied it: equal[;,] as the CPU reads it after the fence: equal.*\\(c1\\) the block at a fresh ring offset \\(page phase [0-9]+, the draw's [0-9]+\\): right.*reads as: H1, address-dependent"
                     -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_probe_robust
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=probe
                     "-DEXTRA=${_vsk_wrong}.*\\(c1\\) the block at a fresh ring offset \\(page phase [0-9]+, the draw's [0-9]+\\): right.*\\(c2\\) uniform robustness off: (right|not offered)"
                     -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_ring
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=ring
                     "-DEXTRA=${_vsk_wrong}.*the bone block as the GPU copied it: differs at qwords 184..187[;,] as the CPU reads it after the fence: differs at qwords 184..187.*reads as: H2, a port race"
                     -P "${_vsk}")
    # the twins (issue 123): twin the frame replayed three times under the
    # game's mode with the twins on and the scheduled probe off, the bone at
    # 200 zeroed in the ring after its bytes were kept: the first replay's
    # twins differ (nothing probed there), the next replay's probe targeted
    # at that command is wrong at address 200; twin_nan the same with the
    # bone made NaN (the v0.10.4 picture's form): the probe's wrong vertices
    # dropped, address 200 named by its non-finite matrix; capture the same
    # with the sidecar written to a folder: its header and sections read back
    set(_vsk_twin "rd: VU skin check: frame [0-9]+ command 0:[0-9]+ mesh rd vu skin check test #[0-9]+ \\(120 vertices\\): twins differ: A \\(the draw's offset, page phase [0-9]+\\) [0-9]+ samples, B \\(fresh, phase [0-9]+\\) [0-9]+ samples(, C \\(ring read\\) [0-9]+ samples)?[;,] through [a-z0-9_]+ with colour and Z writes off[^;,]*[;,] the probe of this command: not probed")
    add_test(NAME rd_vu_skin_check_twin
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=twin
                     "-DEXTRA=${_vsk_twin}.*${_vsk_wrong}.*the probe targeted after a twin event.*twins at the end of the run: [1-9][0-9]* drawn"
                     -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_twin_nan
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=twin
                     "-DEXTRA=${_vsk_twin}.*vertices wrong of 120, address 200: .*reads as non-finite / dropped: [1-9][0-9]* of them \\(a non-finite matrix at 200 drops [1-9][0-9]*\\).*the probe targeted after a twin event"
                     -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_capture
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=capture
                     "-DARG=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_capture"
                     "-DEXTRA=rd: VU skin check: captured [^,]*vu-skin-fail-[0-9]+-0-[0-9]+\\.bin and asked for a frame dump"
                     -P "${_vsk}")
    set_tests_properties(rd_vu_skin_check_twin rd_vu_skin_check_capture
                         PROPERTIES ENVIRONMENT
                                    "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,ring;ICO_RD_VU_SKIN_INTERVAL_MS=off")
    # the twins' thinning (issue 123 after v0.10.5, rd_vu_skin_check.h
    # RD_SKIN_THIN_REPLAYS, at 4 here): thin a frame of three skinned
    # commands, three twins a replay until four clean replays, then one a
    # replay through the box check's pipeline; the ring's bone at 200 zeroed
    # in the run: the one twin differs and re-arms all three; the sabotage
    # off: thinned again after four more
    add_test(NAME rd_vu_skin_check_thin
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=thin
                     "-DEXTRA=rd: VU skin check: twins thinned to one a picture after 4 clean replays \\(through the box check's pipeline.*twins differ: .*rd: VU skin check: twins re-armed by a twin event: every skinned draw twinned again until 4 replays with none differing \\(re-armed 1 times\\).*rd: VU skin check: twins thinned to one a picture after 4 clean replays.*thinned 2 times, re-armed 1 times \\(thinned at the end\\)"
                     -P "${_vsk}")
    set_tests_properties(rd_vu_skin_check_thin
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_INTERVAL_MS=off"
                                    SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vu_skin_check_twin_nan
                         PROPERTIES ENVIRONMENT
                                    "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,nan;ICO_RD_VU_SKIN_INTERVAL_MS=off")
    # issue 123's ladder (rd_vu_bones.c, docs/port/VU_SKIN_CHECK.md "The
    # ladder"), under the plain set (ICO_RD_VU_SET=plain, the Turnip
    # profile's: the ring builds are plain variants): probe_c3 the probe's
    # sabotage (the draw's bytes right): the probe's column 3, the bones
    # through the ring at the draw's block, right; twin_c the ring's
    # sabotage: twin C drawn and reading the zeroed bone as A does (it
    # differs from B); twin_bones_ring the same with the ring builds in force
    # ([dev] vu_bones = ring): the start line, the twins (both ring reads)
    # differing and saying so, "the ring read: not healed" at the end
    add_test(NAME rd_vu_skin_check_probe_c3
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=probe
                     "-DEXTRA=${_vsk_wrong}.*\\(c3\\) the bones through the ring: right \\(0 wrong\\)[;,] reads as: H1, address-dependent"
                     -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_twin_c
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=twin
                     "-DEXTRA=twins differ: A \\(the draw's offset, page phase [0-9]+\\) [0-9]+ samples, B \\(fresh, phase [0-9]+\\) [0-9]+ samples, C \\(ring read\\) [0-9]+ samples.*the ring read's twin \\(C\\) [1-9][0-9]* drawn, [1-9][0-9]* read, [1-9][0-9]* differ from B"
                     -P "${_vsk}")
    add_test(NAME rd_vu_skin_check_twin_bones_ring
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=twin
                     "-DEXTRA=rd: VU bones: through the ring \\(storage buffer\\) on the skinned entries \\(\\[dev\\] vu_bones = ring, set without the skin check's mark\\).*twins differ: .*A and B read the bones through the ring: step 1 of the ladder in force.*rd: VU skin check: the ring read: not healed \\([1-9][0-9]* of [1-9][0-9]* twins differ"
                     -P "${_vsk}")
    set_tests_properties(rd_vu_skin_check_probe_c3 PROPERTIES ENVIRONMENT
                         "${_env};ICO_RD_VU_SKIN_SABOTAGE=200;ICO_RD_VU_SET=plain")
    # twin_record (issue 123 after v0.10.5): the twin frame with the pre-pass
    # on and its batch builds (the record's bones read through t5): the
    # twins of the record draw read the bones through the entry's ring
    # build as the record did (A and B the ring read, no C), and still see
    # the zeroed bone; twin_c is the entry path's C (the pre-pass off)
    add_test(NAME rd_vu_skin_check_twin_record
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=twin
                     "-DEXTRA=twins differ: A \\(the draw's offset, page phase [0-9]+\\) [0-9]+ samples, B \\(fresh, phase [0-9]+\\) [0-9]+ samples[;,] through [a-z0-9_]+ with colour and Z writes off \\(the program's own vertex entry's ring build: the draw read the pre-pass record and its batch builds read the bones through the ring\\).*A and B read the bones through the ring, as the pre-pass record did"
                     "-DNOT_=C \\(ring read\\) [0-9]+ samples"
                     -P "${_vsk}")
    set_tests_properties(rd_vu_skin_check_twin_record PROPERTIES ENVIRONMENT
                         "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,ring;ICO_RD_VU_SKIN_INTERVAL_MS=off;ICO_RD_VU_SET=plain;ICO_RD_VU_PREPASS=1"
                         SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vu_skin_check_twin_c PROPERTIES ENVIRONMENT
                         "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,ring;ICO_RD_VU_SKIN_INTERVAL_MS=off;ICO_RD_VU_SET=plain;ICO_RD_VU_PREPASS=0")
    set_tests_properties(rd_vu_skin_check_twin_bones_ring PROPERTIES ENVIRONMENT
                         "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,ring;ICO_RD_VU_SKIN_INTERVAL_MS=off;ICO_RD_VU_SET=plain;ICO_RD_VU_BONES=ring")
    set_tests_properties(rd_vu_skin_check_judge rd_vu_skin_check rd_vu_skin_check_robust
                         rd_vu_skin_check_probe rd_vu_skin_check_probe_robust
                         rd_vu_skin_check_ring rd_vu_skin_check_twin rd_vu_skin_check_twin_nan
                         rd_vu_skin_check_capture rd_vu_skin_check_probe_c3
                         rd_vu_skin_check_twin_c rd_vu_skin_check_twin_bones_ring
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vu_skin_check PROPERTIES ENVIRONMENT "${_env}")
    # ICO_RD_VU_SKIN_CHECK=1: the device is made with VK_EXT_pipeline_robustness
    # only where the check can run (an Adreno, or the setting: vk_device.c)
    set_tests_properties(rd_vu_skin_check_robust
                         PROPERTIES ENVIRONMENT "${_env};ICO_GPU_ROBUST=1;ICO_RD_VU_SKIN_CHECK=1")
    set_tests_properties(rd_vu_skin_check_probe_robust
                         PROPERTIES ENVIRONMENT
                                    "${_env};ICO_GPU_ROBUST=1;ICO_RD_VU_SKIN_CHECK=1;ICO_RD_VU_SKIN_SABOTAGE=200")
    set_tests_properties(rd_vu_skin_check_probe
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200")
    set_tests_properties(rd_vu_skin_check_ring
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,ring")
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
    # the upscale marker's Off identity (rd_upscale_test off: the frame
    # replayed with RDC_UPSCALE_POINT an RDC_NOP gives DISPLAY and the
    # present byte for byte) under these limits; the corpus's dumps (version
    # 16 and older) carry no marker.  rd_upscale_off_tiler, below: the same
    # on the tile-based device profile
    add_test(NAME rd_upscale_off_mali COMMAND rd_upscale_test off)
    list(APPEND _ico_mali_tests rd_upscale_off_mali)
    set_tests_properties(${_ico_mali_tests} PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_ml}"
                                                       ENVIRONMENT "${_env_mali}")
    # rd_upscale_off_tiler: rd_upscale_test off on the tile-based device
    # profile (ICO_VK_FAKE_LIMITS=moltenvk, the Apple class of rd_profile.c:
    # pass areas, the aura box, no glow fold, no UINT views; the class the
    # mali limits alone do not give on lavapipe), so the list walkers that
    # pass over the marker on a tiler (the store plan, the pass areas, the
    # folds' look-ahead, the crack fill's place) leave the picture as with
    # the marker a NOP
    add_test(NAME rd_upscale_off_tiler COMMAND rd_upscale_test off)
    set_tests_properties(rd_upscale_off_tiler PROPERTIES SKIP_RETURN_CODE 77
                                              ENVIRONMENT "${_env};ICO_VK_FAKE_LIMITS=moltenvk")
    # issue 179: the VU vertex pre-pass under a phone's limits
    # (ICO_VK_FAKE_LIMITS=mali clamps the limits; RhiLimits.tiler stays the
    # device's, so these are the desktop runs under those limits: the
    # tiler rule itself, rd__vu_prepass_init on RhiLimits.tiler, is
    # rd_vuprepass_selftest_test's step 6 in every run).
    # rd_vuprepass_selftest_mali: the
    # start-up self-test passes over its four replays under synchronisation
    # validation with the submit-time checks (a hazard between two lists of
    # the record's copies and reads: the open barrier's job across lists);
    # _mali_sabotage_straddle: a stale record fails it from replay 2 on;
    # _mali_vk11: the same self-test through VkRenderPass objects (the
    # legacy render passes of a Vulkan 1.1 phone without dynamic rendering
    # and without dynamic state, ICO_VK_FAKE_API=1.1 and
    # ICO_VK_FAKE_NO_DYNAMIC=1, vk_device.c, vk_renderpass.c): the one of
    # the three that adds coverage the desktop runs lack.
    set(_env_mali_pre ${_env_mali} "ICO_RD_VU_PREPASS=1")
    if(_env)
        list(APPEND _env_mali_pre "VK_KHRONOS_VALIDATION_SYNCVAL_SUBMIT_TIME_VALIDATION=true")
    endif()
    add_test(NAME rd_vuprepass_selftest_mali
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_mali_sabotage_straddle
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                     "-DLINE=${_vst_stale}"
                     "-DEXTRA=rd: VU pre-pass: ICO_RD_VU_PREPASS_SABOTAGE: in the self-test the record's copies"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_mali_vk11
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" -P "${_vst}")
    # the record stored by the vertex shaders under the phone's limits
    # (_stores_mali) and through VkRenderPass objects (_stores_vk11: the
    # record's passes without attachments as legacy subpasses), both named
    # on the verdict line, under the same validation
    add_test(NAME rd_vuprepass_selftest_stores_mali
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_vst_stores_line}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_stores_vk11
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_vst_stores_line}" -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_stores_mali
                         PROPERTIES ENVIRONMENT "${_env_mali_pre};ICO_RD_VU_RECORD=stores")
    set_tests_properties(
        rd_vuprepass_selftest_stores_vk11
        PROPERTIES ENVIRONMENT
                   "${_env_mali_pre};ICO_RD_VU_RECORD=stores;ICO_VK_FAKE_API=1.1;ICO_VK_FAKE_NO_DYNAMIC=1")
    set_tests_properties(rd_vuprepass_selftest_mali rd_vuprepass_selftest_mali_sabotage_straddle
                         rd_vuprepass_selftest_mali_vk11 rd_vuprepass_selftest_stores_mali
                         rd_vuprepass_selftest_stores_vk11
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_mali PROPERTIES ENVIRONMENT "${_env_mali_pre}")
    set_tests_properties(rd_vuprepass_selftest_mali_sabotage_straddle
                         PROPERTIES ENVIRONMENT "${_env_mali_pre};ICO_RD_VU_PREPASS_SABOTAGE=stale")
    set_tests_properties(
        rd_vuprepass_selftest_mali_vk11
        PROPERTIES ENVIRONMENT "${_env_mali_pre};ICO_VK_FAKE_API=1.1;ICO_VK_FAKE_NO_DYNAMIC=1")
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
    # rd_vurecord_ab (test/rd_vu_env_ab.cmake): rd_pixel's dump and the same
    # dumps with the VU record drawn into the render targets and copied
    # (ICO_RD_VU_RECORD=targets) and stored by the vertex shaders (=stores;
    # rd_vu_record.h) at Original 4:3 1x and Enhanced 16:9 2x under
    # ICO_RD_BLEND=fixed: every named target byte for byte, both runs every VU
    # command through the record, each naming its write.  rd_vuprepass_min_ab:
    # the same dumps with [dev] vu_prepass_min under every frame
    # (ICO_RD_VU_PREPASS_MIN=0) and above every frame (2147483647: each frame
    # skipped, every VU command the old way): byte for byte, the skips counted
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vurecord_ab"
         "${CMAKE_CURRENT_BINARY_DIR}/vuprepass_min_ab")
    add_test(NAME rd_vurecord_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> -DNAME=rd_vurecord_ab
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vurecord_ab"
                     "-DENV_A=ICO_RD_VU_RECORD=targets" "-DENV_B=ICO_RD_VU_RECORD=stores"
                     "-DREQ_A=0 mesh and grid draws the old way.*stats: VU record targets: drawn into 3"
                     "-DREQ_B=0 mesh and grid draws the old way.*stats: VU record stores: stored by"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
    add_test(NAME rd_vuprepass_min_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DNAME=rd_vuprepass_min_ab
                     # no fixture dump: rd_pixel_frame.rddump has no VU draws (0 passes either way);
                     # the list's dumps alone, SKIP where none exist, and a listed frame without
                     # VU commands (the corpus's 2D-only frames: 0 passes with the floor at 0,
                     # nothing to skip above it) skipped
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vuprepass_min_ab"
                     "-DSKIP_UNLESS=stats: VU pre-pass on: [1-9][0-9]* passes"
                     "-DENV_A=ICO_RD_VU_PREPASS_MIN=0" "-DENV_B=ICO_RD_VU_PREPASS_MIN=2147483647"
                     "-DREQ_A=stats: VU pre-pass on: [12] passes.*pre-pass skipped 0 frames under 0 slots"
                     "-DREQ_B=stats: VU pre-pass on: 0 passes.*pre-pass skipped [1-9][0-9]* frames under 2147483647 slots"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
    set_tests_properties(rd_vurecord_ab rd_vuprepass_min_ab
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600
                                    FIXTURES_REQUIRED rd_dump)
    if(_env)
        set_tests_properties(rd_vurecord_ab rd_vuprepass_min_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vumerge_ab (test/rd_vumerge_ab.cmake): the same dumps with every VU
    # draw a GPU draw of its own (ICO_RD_VU_MERGE=0) and with the draw merge
    # (rd_replay_vu.c vuDraw), through the record and through the programs'
    # entries (ICO_RD_VU_PREPASS=0, the tile-based GPUs' path), at Original
    # 4:3 1x, Enhanced 4:3 2x and Enhanced 16:9 2x under ICO_RD_BLEND=fixed:
    # every named target byte for byte, the GPU draws fewer by what the runs
    # joined; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab")
    add_test(NAME rd_vumerge_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vumerge_ab.cmake")
    set_tests_properties(rd_vumerge_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_vumerge_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vumerge_mali (test/rd_vumerge_mali.cmake, issue 164): the dumps of
    # the machine-local list tools/vumerge_mali_dumps.txt (the Mali-G52
    # reporter's frame, /primary/dev/ico-dumps/164/, the phone frames and a
    # capture) on the plain device and under a phone GPU's limits without
    # dual-source blending (ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DUAL=1:
    # every lerp draw two passes), under ICO_RD_BLEND=fixed: every named
    # target byte for byte, the draw merge's figures and the fused pairs the
    # plain device's (826 draws merged into 87 on the 164 frame); label
    # mali; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vumerge_mali")
    add_test(NAME rd_vumerge_mali
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vumerge_mali_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vumerge_mali"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vumerge_mali.cmake")
    set_tests_properties(rd_vumerge_mali PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800
                                                     LABELS mali)
    if(_env)
        set_tests_properties(rd_vumerge_mali PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_occlusion_tool (test/rd_occlusion_tool.cmake, docs/port/RENDER.md
    # "Culling"): rd_replay_tool --occlusion on the first dump of the
    # machine-local list tools/occlusion_corpus.txt here, at Original 4:3 1x:
    # the hidden-draw line's shape and its --nop spec, then the plain replay
    # and the replay with the hidden commands NOPs, DISPLAY and PRESENT byte
    # for byte under ICO_RD_BLEND=fixed; SKIP without the list, a dump or a
    # device
    add_test(NAME rd_occlusion_tool
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/occlusion_corpus.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/occlusion_tool"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_occlusion_tool.cmake")
    set_tests_properties(rd_occlusion_tool PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    if(_env)
        set_tests_properties(rd_occlusion_tool PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vu_skin_check_dump (test/rd_vu_skin_check_dump.cmake, issue 163,
    # docs/port/VU_SKIN_CHECK.md): rd_replay_tool --skin-check on a
    # machine-local corpus dump with 46 skinned commands (the queen's
    # among them): every command the replay draws checked (a culled one is
    # not drawn), none wrong, the bytes equal, no box sample outside on
    # lavapipe; rd_vu_skin_check_163*: the reporter's
    # blended dump (machine-local, /primary/dev/ico-dumps/163/): clean, then
    # with the bone at address 200 read as zeros by the probe (the device's
    # failure as the check would see it: address 200 named, H1
    # address-dependent) and in the ring (H2), and the deep dive's patched
    # copy (its stream's lanes on 200 moved to 252: the CPU and the GPU
    # read the same stream, so the check finds nothing there: it compares
    # the device with the CPU, not with the clean dump); SKIP without the
    # dump or a device
    set(_vskd "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_skin_check_dump.cmake")
    set(_vskd_clean "[0-9]+ skinned commands, [1-9][0-9]* checks, 0 wrong \\(address 0\\), 0 blocks whose bytes differed \\(0 changed after the draw\\), 0 of [0-9]+ box checks with samples outside")
    set(_vsk163 "/primary/dev/ico-dumps/163")
    # a wrong check's mismatch line for Yorda's mesh at bone address 200
    # (review2 S5: the H2 run's 42 changed blocks must not use up its lines)
    set(_vsk163_wrong "rd: VU skin check: frame [0-9]+ command 0:[0-9]+ mesh testmodel #[0-9]+ \\([0-9]+ vertices\\): [0-9]+ vertices wrong of [0-9]+, address 200: ")
    add_test(NAME rd_vu_skin_check_dump
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=/primary/dev/ico-dumps/frame-20261007-200706-v3086.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_dump"
                     "-DLINE=46 skinned commands, [1-9][0-9]* checks, 0 wrong \\(address 0\\), 0 blocks whose bytes differed \\(0 changed after the draw\\), 0 of [0-9]+ box checks with samples outside"
                     -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_163
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_vsk163}/frame-20261105-195124-v6171-blended.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_163"
                     "-DLINE=${_vskd_clean}" -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_163_probe
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_vsk163}/frame-20261105-195124-v6171-blended.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_163_probe"
                     "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\), 0 blocks whose bytes differed \\(0 changed after the draw\\).*reads as: H1, address-dependent"
                     "-DLOG=${_vsk163_wrong}" -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_163_ring
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_vsk163}/frame-20261105-195124-v6171-blended.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_163_ring"
                     "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\), [1-9][0-9]* blocks whose bytes differed \\([1-9][0-9]* changed after the draw\\).*reads as: H2, a port race"
                     "-DLOG=${_vsk163_wrong}" -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_163_patched
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${_vsk163}/blended-750-a200.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_163_patched"
                     "-DLINE=${_vskd_clean}" -P "${_vskd}")
    # rd_vu_skin_check_123*: issue 123's v0.10.4 blended dump (Yorda's
    # shin missing on the device; machine-local, copied into the same
    # folder): clean (the twins equal), and with the bone at 200 made NaN in
    # the ring after its bytes were kept (the device's picture's form, B0):
    # wrong at address 200 with the wrong vertices dropped, H2, the twins
    # differing
    set(_vsk123 "${_vsk163}/frame-20261106-080033-v3910-blended.rddump")
    add_test(NAME rd_vu_skin_check_123
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123"
                     "-DLINE=${_vskd_clean}.*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), 0 differ"
                     -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_123_nan
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_nan"
                     "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*reads as: H2, a port race.*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), [1-9][0-9]* differ.*[1-9][0-9]* wrong vertices dropped"
                     "-DLOG=${_vsk163_wrong}.*reads as non-finite / dropped" -P "${_vskd}")
    # _attach: the same two under the Turnip reporter's profile on lavapipe
    # (the attachment read, GS state as shader data, the VU pre-pass off as
    # on a tile-based GPU), so the draws whose first pass reads the
    # attachment with Z writes off are twinned through their own pipelines
    # with the write mask cleared: clean every twin equal with at least one
    # through the draw's own pipeline; with the NaN bone the same and the
    # twins differing (on this dump the three draws on bone 200 are not
    # among the own-pipeline ones: each event line says why)
    set(_vsk_attach "ICO_RD_VU_PREPASS=0;ICO_VK_FAKE_NO_INTERLOCK=1;ICO_RD_BLEND=attachment;ICO_RD_UBER=on")
    add_test(NAME rd_vu_skin_check_123_attach
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_attach"
                     "-DLINE=${_vskd_clean}.*twins: [1-9][0-9]* read \\([1-9][0-9]* through the draw's own pipeline\\), 0 differ"
                     -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_123_nan_attach
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_nan_attach"
                     "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*twins: [1-9][0-9]* read \\([1-9][0-9]* through the draw's own pipeline\\), [1-9][0-9]* differ"
                     "-DLOG=twins differ: A \\(the draw's offset, page phase [0-9]+\\) [0-9]+ samples, B \\(fresh, phase [0-9]+\\) [0-9]+ samples(, C \\(ring read\\) [0-9]+ samples)?[;,] through [a-z0-9_]+ with " -P "${_vskd}")
    # issue 123's ladder on the same dump under the plain set with the
    # pre-pass off (the Turnip profile's program entries): _123_c3 the
    # probe's sabotage (the draw's bytes right): wrong at 200, H1, the
    # probe's column 3 (the bones through the ring at the draw's block)
    # right and every twin C equal to B; _123_nan_c the ring's NaN: twin C
    # reads it as A does (differs from B); _123_bones_ring the ring builds in
    # force ([dev] vu_bones = ring): clean, the start line, "the ring read:
    # healed"; _123_bones_ring_nan with the NaN: the twins (both ring reads)
    # differ, "not healed"
    set(_vsk_ringenv "ICO_RD_VU_SET=plain;ICO_RD_VU_PREPASS=0")
    add_test(NAME rd_vu_skin_check_123_c3
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_c3"
                     "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\), 0 blocks whose bytes differed \\(0 changed after the draw\\).*reads as: H1, address-dependent.*the ring read: c3 right [1-9][0-9]* \\(c3 drawn in [1-9][0-9]*\\), ring twins [1-9][0-9]* read, 0 differ from B"
                     "-DLOG=\\(c3\\) the bones through the ring: right \\(0 wrong\\)" -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_123_nan_c
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_nan_c"
                     "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*reads as: H2, a port race.*ring twins [1-9][0-9]* read, [1-9][0-9]* differ from B"
                     "-DLOG=B \\(fresh, phase [0-9]+\\) [0-9]+ samples, C \\(ring read\\) [0-9]+ samples" -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_123_bones_ring
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_bones_ring"
                     "-DLINE=${_vskd_clean}.*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), 0 differ"
                     "-DLOG=rd: VU bones: through the ring \\(storage buffer\\) on the skinned entries.*rd: VU skin check: the ring read: healed \\(0 of [1-9][0-9]* twins differ" -P "${_vskd}")
    add_test(NAME rd_vu_skin_check_123_bones_ring_nan
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DDUMP=${_vsk123}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_bones_ring_nan"
                     "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), [1-9][0-9]* differ"
                     "-DLOG=A and B read the bones through the ring: step 1 of the ladder in force.*rd: VU skin check: the ring read: not healed \\([1-9][0-9]* of [1-9][0-9]* twins differ" -P "${_vskd}")
    set_tests_properties(rd_vu_skin_check_123_c3 PROPERTIES ENVIRONMENT
                         "${_env};${_vsk_ringenv};ICO_RD_VU_SKIN_SABOTAGE=200")
    set_tests_properties(rd_vu_skin_check_123_nan_c PROPERTIES ENVIRONMENT
                         "${_env};${_vsk_ringenv};ICO_RD_VU_SKIN_SABOTAGE=200,nan")
    set_tests_properties(rd_vu_skin_check_123_bones_ring PROPERTIES ENVIRONMENT
                         "${_env};${_vsk_ringenv};ICO_RD_VU_BONES=ring")
    set_tests_properties(rd_vu_skin_check_123_bones_ring_nan PROPERTIES ENVIRONMENT
                         "${_env};${_vsk_ringenv};ICO_RD_VU_BONES=ring;ICO_RD_VU_SKIN_SABOTAGE=200,nan")
    set(_vskd_tests rd_vu_skin_check_dump rd_vu_skin_check_163 rd_vu_skin_check_163_probe
                    rd_vu_skin_check_163_ring rd_vu_skin_check_163_patched rd_vu_skin_check_123
                    rd_vu_skin_check_123_nan rd_vu_skin_check_123_attach
                    rd_vu_skin_check_123_nan_attach rd_vu_skin_check_123_c3
                    rd_vu_skin_check_123_nan_c rd_vu_skin_check_123_bones_ring
                    rd_vu_skin_check_123_bones_ring_nan)
    set_tests_properties(${_vskd_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    if(_env)
        set_tests_properties(rd_vu_skin_check_dump rd_vu_skin_check_163
                             rd_vu_skin_check_163_patched rd_vu_skin_check_123
                             PROPERTIES ENVIRONMENT "${_env}")
    endif()
    set_tests_properties(rd_vu_skin_check_163_probe
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200")
    set_tests_properties(rd_vu_skin_check_163_ring
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,ring")
    set_tests_properties(rd_vu_skin_check_123_nan
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,nan")
    set_tests_properties(rd_vu_skin_check_123_attach PROPERTIES ENVIRONMENT "${_env};${_vsk_attach}")
    set_tests_properties(rd_vu_skin_check_123_nan_attach
                         PROPERTIES ENVIRONMENT
                                    "${_env};${_vsk_attach};ICO_RD_VU_SKIN_SABOTAGE=200,nan")
    # rd_vu_skin_check_ab (test/rd_vu_skin_check_ab.cmake, review2 S6): the
    # picture with the check on for every skinned command (ICO_RD_VU_SKIN_CHECK=2)
    # against off, every pass-dump PNG byte for byte, at Original, with the
    # attachment read and GS state as shader data (the Turnip reporter's
    # profile on lavapipe) and at Enhanced 2x MSAA 4, on the corpus dump of
    # tools/vustage_dumps.txt's /primary/dev/ico-dumps/*.rddump with the
    # most skinned commands (frame-20261007-200706-v3086: 46), issue 163's
    # blended dump (Yorda's draws) and issue 123's; the twins (every skinned
    # draw twinned in its pass) drawn in the plain and attach runs and, in
    # the attach runs, through the draws' own pipelines with the write mask
    # cleared; SKIP without them or a device
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_ab")
    add_test(NAME rd_vu_skin_check_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMPS=/primary/dev/ico-dumps/frame-20261007-200706-v3086.rddump|${_vsk163}/frame-20261105-195124-v6171-blended.rddump|${_vsk163}/frame-20261106-080033-v3910-blended.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_skin_check_ab.cmake")
    set_tests_properties(rd_vu_skin_check_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    if(_env)
        set_tests_properties(rd_vu_skin_check_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vu_bones_ring_ab (test/rd_vu_bones_ring_ab.cmake, issue 123's
    # ladder, docs/port/VU_SKIN_CHECK.md "The ladder"): the three dumps of
    # /primary/dev/ico-dumps/163/ (issue 163's blended dump, its patched
    # copy, issue 123's v0.10.4 blended dump) replayed under the plain set
    # with the VU pre-pass off, the bones through VuBoneCB and through the
    # ring ([dev] vu_bones = ring): every pass-dump PNG byte for byte, at
    # Original and with the attachment read and GS state as shader data;
    # SKIP without them or a device
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab")
    add_test(NAME rd_vu_bones_ring_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMPS=${_vsk163}/frame-20261105-195124-v6171-blended.rddump|${_vsk163}/blended-750-a200.rddump|${_vsk163}/frame-20261106-080033-v3910-blended.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_bones_ring_ab.cmake")
    set_tests_properties(rd_vu_bones_ring_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    if(_env)
        set_tests_properties(rd_vu_bones_ring_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vu_bones_ring_ab_prepass: the same dumps with the VU pre-pass on (every
    # tile-based GPU since v0.11.0): the ring runs byte for byte as the
    # cbuffer runs under the pre-pass's batch builds (which read the bones
    # through t5 either way) and with them off (the skinned commands then
    # drawn the old way through the ring builds); the twins of the record
    # draws read the bones through the ring as the record did
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab_prepass")
    add_test(NAME rd_vu_bones_ring_ab_prepass
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> -DPREPASS=1
                     "-DDUMPS=${_vsk163}/frame-20261105-195124-v6171-blended.rddump|${_vsk163}/blended-750-a200.rddump|${_vsk163}/frame-20261106-080033-v3910-blended.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab_prepass"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_bones_ring_ab.cmake")
    set_tests_properties(rd_vu_bones_ring_ab_prepass PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                                 TIMEOUT 2700)
    if(_env)
        set_tests_properties(rd_vu_bones_ring_ab_prepass PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_region_stats (test/rd_region_stats.cmake, docs/port/VU1_PROGRAMS.md
    # "The region rule"): rd_replay_tool --region-stats on the first two
    # dumps of the machine-local list tools/occlusion_corpus.txt here, at
    # Original 4:3 1x and Enhanced 4:3 2x: the lines' shapes, then the
    # pass dumps of the plain replay and of the replay with the draws the
    # CPU oracle says draw nothing NOPs (--region-out's spec), byte for
    # byte; the orig setting's spec the same without a device; SKIP
    # without the list, a dump or a device
    add_test(NAME rd_region_stats
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/occlusion_corpus.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/region_stats"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_region_stats.cmake")
    set_tests_properties(rd_region_stats PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    if(_env)
        set_tests_properties(rd_region_stats PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_sideband_ab (test/rd_sideband_ab.cmake, issue 29, docs/port/VU1_PROGRAMS.md
    # "The region rule"): the dumps of the machine-local list
    # tools/sideband_dumps.txt at their own aspect, Original 1x and Enhanced
    # 2x, with the PS2's drop on every VU draw (ICO_RD_VU_BESIDE=0) and with
    # the side bands: tools/cmp_dirs_band.py holds SCENE's centre 512 GS
    # pixels byte for byte and every changed side pixel to one the drop left
    # at the fog colour; SKIP without the list, a dump or a device
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sideband_ab")
    add_test(NAME rd_sideband_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DPYTHON=${Python3_EXECUTABLE}"
                     "-DCMP=${CMAKE_SOURCE_DIR}/tools/cmp_dirs_band.py"
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/sideband_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/sideband_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_sideband_ab.cmake")
    set_tests_properties(rd_sideband_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_sideband_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_sideband_cards (test/rd_sideband_cards.cmake, issue 189,
    # docs/port/VU1_PROGRAMS.md "The region rule", step 7): the reporter's
    # frame near stage 32's exit (machine-local, /primary/dev/ico-dumps/189),
    # whose only dropped triangles are the mist quads' cards, at Original
    # 4:3, Enhanced 4:3 2x, Enhanced 16:10 and the reporter's settings: every
    # target byte for byte with and without the side bands (a card keeps the
    # drop beside the picture), --stats's "side bands: N draws with cards",
    # --region-stats's three card drops and nothing beside, and
    # tools/band_seam.py's picture edges within 1.5 levels on the 16:10
    # presents (7.3 with the cards drawn beside the picture); SKIP without
    # the dump or a device
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sideband_cards")
    set(_sbc_py "")
    if(Python3_FOUND)
        set(_sbc_py "-DPYTHON=${Python3_EXECUTABLE}")
    endif()
    add_test(NAME rd_sideband_cards
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=/primary/dev/ico-dumps/189/frame-20261104-120509-v130039.rddump"
                     "-DSEAM=${CMAKE_SOURCE_DIR}/tools/band_seam.py" ${_sbc_py}
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/sideband_cards"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_sideband_cards.cmake")
    set_tests_properties(rd_sideband_cards PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 900)
    if(_env)
        set_tests_properties(rd_sideband_cards PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_region_camera_ab (test/rd_region_camera_ab.cmake, docs/port/VU1_PROGRAMS.md
    # "The region rule", DIVERGENCES.md VD-29): the camera rule against the
    # PS2's region rule on the dumps of tools/sideband_dumps.txt at their own
    # aspect, Original 1x and Enhanced 2x: tools/region_camera_report.py holds
    # every SCENE pixel the camera rule changes to the CPU oracle's mask
    # (--region-mask) and every target byte for byte where the mask is empty;
    # and --region-stats under the camera rule gives the same verdicts at the
    # Original preset and Enhanced at 1x; SKIP without the list, a dump or a
    # device
    if(Python3_FOUND)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/region_camera_ab")
        add_test(NAME rd_region_camera_ab
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DPYTHON=${Python3_EXECUTABLE}"
                         "-DREPORT=${CMAKE_SOURCE_DIR}/tools/region_camera_report.py"
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/sideband_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/region_camera_ab"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_region_camera_ab.cmake")
        set_tests_properties(rd_region_camera_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                            TIMEOUT 3600)
        if(_env)
            set_tests_properties(rd_region_camera_ab PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endif()
    # rd_noplanes_ab (test/rd_noplanes_ab.cmake, issue 161): the dumps of the
    # machine-local list tools/noplanes_dumps.txt (the title and the boot,
    # each drawing one screen-prim triangle key with planes, and a capture
    # that draws none) with the sprite planes builds and without them
    # (ICO_RD_NO_SPRITE_PLANES=1, the fallback [dev] pipeline_skip turns on):
    # every target byte for byte on all four, the title's and the boot's 2
    # commands each through the plain builds (none on the capture); SKIP as
    # rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/noplanes_ab")
    add_test(NAME rd_noplanes_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/noplanes_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/noplanes_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_noplanes_ab.cmake")
    set_tests_properties(rd_noplanes_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    if(_env)
        set_tests_properties(rd_noplanes_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vufuse_ab (test/rd_vufuse_ab.cmake): the same dumps and the phone's
    # frames (tools/vufuse_dumps.txt) with code 36's kicks under their own
    # pipelines (ICO_RD_VU_FUSE=0) and with the fused scissor pair
    # (rd_replay_vu.c vuFuseVerdict), at the phone's profile (the programs'
    # entries, 2x at 2.189 presented at 2364x1080, and Resolution Auto: the
    # no-planes twins) and Enhanced 2x 4:3 and 16:9 through the entries and
    # the record, under ICO_RD_BLEND=fixed: every named target byte for byte,
    # the pairs fused where counted (on every phone frame at both phone
    # settings), none kept apart for the pixel shader, the GPU draws fewer
    # by what the merge joined; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab")
    add_test(NAME rd_vufuse_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DEXTRA=${CMAKE_SOURCE_DIR}/tools/vufuse_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vufuse_ab.cmake")
    set_tests_properties(rd_vufuse_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 5400)
    if(_env)
        set_tests_properties(rd_vufuse_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # the same two A/Bs and rd_vustage_ab on the frames rd_pixel leaves
    # behind (every machine has them; CI's Direct3D 11 job runs them on WARP,
    # tools/ci_d3d11_warp.sh, where the pre-pass's record is copied by the
    # backend's compute shader since v0.10.0):
    # rd_pixel_vu.rddump (64 code-32 draws of one prelit mesh under one state,
    # a matrix each: the merge joins them), rd_pixel_vu36.rddump (a code-36
    # strip, its first triangle cut, its second kicked: a fused pair's
    # candidate) and rd_pixel_frame.rddump (no VU draw: both runs the same).
    # The 16:9 and phone settings skip for these 4:3 frames by design
    set(_vu_pixel_list "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_vu_dumps.txt")
    file(WRITE "${_vu_pixel_list}" "rd_pixel_vu.rddump\nrd_pixel_vu36.rddump\nrd_pixel_frame.rddump\n")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab_pixel"
         "${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab_pixel" "${CMAKE_CURRENT_BINARY_DIR}/vustage_ab_pixel")
    add_test(NAME rd_vustage_ab_pixel
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${_vu_pixel_list}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vustage_ab_pixel"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vustage_ab.cmake")
    set_tests_properties(rd_vustage_ab_pixel PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                         FIXTURES_REQUIRED rd_dump TIMEOUT 600)
    add_test(NAME rd_vumerge_ab_pixel
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${_vu_pixel_list}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab_pixel"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vumerge_ab.cmake")
    set_tests_properties(rd_vumerge_ab_pixel PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                         FIXTURES_REQUIRED rd_dump TIMEOUT 600)
    add_test(NAME rd_vufuse_ab_pixel
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${_vu_pixel_list}" "-DEXTRA="
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab_pixel"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vufuse_ab.cmake")
    set_tests_properties(rd_vufuse_ab_pixel PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                        FIXTURES_REQUIRED rd_dump TIMEOUT 600)
    # the merge and the fuse under GS state as shader data (rd_uber.c,
    # ICO_RD_UBER=on: lavapipe orders the attachment read): the VU draws
    # through the colour read, merged and fused, byte for byte as one GPU
    # draw a draw and as the kicks apart; on rd_pixel's frames and on the
    # corpus lists (SKIP without them)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab_pixel_uber"
         "${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab_pixel_uber"
         "${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab_uber" "${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab_uber")
    add_test(NAME rd_vumerge_ab_pixel_uber
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${_vu_pixel_list}" "-DBLEND_ENV=ICO_RD_UBER=on"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab_pixel_uber"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vumerge_ab.cmake")
    add_test(NAME rd_vufuse_ab_pixel_uber
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${_vu_pixel_list}" "-DEXTRA=" "-DBLEND_ENV=ICO_RD_UBER=on"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab_pixel_uber"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vufuse_ab.cmake")
    set_tests_properties(rd_vumerge_ab_pixel_uber rd_vufuse_ab_pixel_uber
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" FIXTURES_REQUIRED rd_dump
                                    TIMEOUT 600)
    add_test(NAME rd_vumerge_ab_uber
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DBLEND_ENV=ICO_RD_UBER=on"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vumerge_ab_uber"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vumerge_ab.cmake")
    add_test(NAME rd_vufuse_ab_uber
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DEXTRA=${CMAKE_SOURCE_DIR}/tools/vufuse_dumps.txt"
                     "-DBLEND_ENV=ICO_RD_UBER=on"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vufuse_ab_uber"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vufuse_ab.cmake")
    set_tests_properties(rd_vumerge_ab_uber PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                       TIMEOUT 3600)
    set_tests_properties(rd_vufuse_ab_uber PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                      TIMEOUT 5400)
    if(_env)
        set_tests_properties(rd_vumerge_ab_pixel_uber rd_vufuse_ab_pixel_uber rd_vumerge_ab_uber
                             rd_vufuse_ab_uber PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vumerge_fallback (test/rd_vumerge_fallback.cmake): a merge build the
    # driver refuses (faked: ICO_RD_FAKE_MERGE_FAIL=1) turns the merge off from
    # the next replay, logged once, and the later replays draw what
    # ICO_RD_VU_MERGE=0 draws (rd_replay_vu.c rd__vu_merge_fallback)
    add_test(NAME rd_vumerge_fallback
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_vu.rddump"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vumerge_fallback"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vumerge_fallback.cmake")
    set_tests_properties(rd_vumerge_fallback PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                         FIXTURES_REQUIRED rd_dump TIMEOUT 300)
    if(_env)
        set_tests_properties(rd_vumerge_ab_pixel rd_vufuse_ab_pixel rd_vustage_ab_pixel
                             rd_vumerge_fallback PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_store_plan_ab (test/rd_store_plan_ab.cmake): the same dumps and the
    # phone's frame with every depth store kept (ICO_RD_STORE_PLAN=0), with the
    # store plan (rd_store_plan.c) and with every depth store dropped
    # (ICO_RD_STORE_PLAN=all), under ICO_RD_STORE_POISON=1 (a load the plan
    # leaves undefined clears to garbage: lavapipe keeps the contents itself),
    # at the phone's profile and the desktop's at 4:3 and 16:9 2x, under
    # ICO_RD_BLEND=fixed: every named target byte for byte with the plan, depth
    # stores dropped, and some target changed by the wrong plan; SKIP as
    # rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/store_plan_ab")
    add_test(NAME rd_store_plan_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DEXTRA=${CMAKE_SOURCE_DIR}/tools/vufuse_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/store_plan_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_store_plan_ab.cmake")
    set_tests_properties(rd_store_plan_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_store_plan_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_cull_ab (test/rd_cull_ab.cmake): the scene draws' visibility test
    # (rd_vis.c, rd_vis_hiz.c) against every draw replayed, byte for byte: the
    # dumps of tools/occlusion_corpus.txt and tools/native_frame_dumps.txt that
    # exist, under ICO_RD_BLEND=fixed, with ICO_RD_CULL=off, frustum and full, through
    # --pass-dump --stats at Original 4:3 640x480, Enhanced 4:3 2x, Enhanced
    # 2x at the dump's own aspect where it is wider than 4:3, and MSAA 4x on
    # the first dump and on tools/cull_msaa_dumps.txt's (the epoch closed
    # inside a list by a read of SCENE): every target's PNG equal, the stats
    # line read, draws skipped over the corpus; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cull_ab")
    add_test(NAME rd_cull_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/occlusion_corpus.txt"
                     "-DEXTRA=${CMAKE_SOURCE_DIR}/tools/native_frame_dumps.txt"
                     "-DMSAA_LIST=${CMAKE_SOURCE_DIR}/tools/cull_msaa_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/cull_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_cull_ab.cmake")
    set_tests_properties(rd_cull_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 5400)
    if(_env)
        set_tests_properties(rd_cull_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_vis_hiz_corpus (test/rd_vis_hiz_corpus.cmake): the occlusion test's
    # cheaper collect over the occlusion corpus: for every row of
    # test/rd_vis_hiz_corpus.txt whose dump is here (Original 4:3, Enhanced
    # 4:3 2x, and the same with --repeat 3: the collect cache's), --cull full
    # --stats under ICO_RD_CULL_HIZ_FAST=0 and under the fast collect with
    # ICO_RD_CULL_HIZ_CHECK=1: the same counts as each other and as the
    # table, no check wrong; SKIP as rd_cull_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vis_hiz_corpus")
    add_test(NAME rd_vis_hiz_corpus
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DTABLE=${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vis_hiz_corpus.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vis_hiz_corpus"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vis_hiz_corpus.cmake")
    set_tests_properties(rd_vis_hiz_corpus PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    if(_env)
        set_tests_properties(rd_vis_hiz_corpus PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_thread_ab (test/rd_thread_ab.cmake): the same dumps through
    # rd_replay_tool --render-thread off, sync and on (the replays, the
    # interpolation and the renderer's calls on a thread of their own,
    # rd_thread.h), at Original and Enhanced 2x and, for the F12 zips, the
    # blended present at 0.5: every target's PNG byte for byte; SKIP as
    # rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/thread_ab")
    add_test(NAME rd_thread_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DEXTRA=${CMAKE_SOURCE_DIR}/tools/vufuse_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/thread_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_thread_ab.cmake")
    set_tests_properties(rd_thread_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_thread_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_constant_reads_ab (test/rd_constant_reads_ab.cmake): the same dumps
    # and the phone's frames (tools/constant_reads_dumps.txt) with every post
    # sprite reading its source (ICO_RD_CONSTANT_READS=0) and with a source
    # that holds only its clear drawn as its colour (rd_const_read.c), at the
    # phone's profile at 2x and at its Resolution Auto size and at the
    # desktop's Enhanced 2x 4:3 and 16:9, under ICO_RD_BLEND=fixed: every
    # named target byte for byte, the same target reads either way, the
    # phone's frames served; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/constant_reads_ab")
    add_test(NAME rd_constant_reads_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                     "-DEXTRA=${CMAKE_SOURCE_DIR}/tools/constant_reads_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/constant_reads_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_constant_reads_ab.cmake")
    set_tests_properties(rd_constant_reads_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_constant_reads_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_fades_fold_ab (test/rd_fades_fold_ab.cmake): the fade dumps
    # (tools/fades_fold_dumps.txt) and, with ICO_BOOT_DUMP_DIR, the start-up's
    # frames 115 to 123, with list 11's last full-frame lerp sprites drawn
    # into SCENE (ICO_RD_FADES_FOLD=0, the tests' way) and applied by the
    # reduction (rd_fades_fold.c, the default on every device), under
    # ICO_RD_BLEND=attachment and fixed, at Original, Enhanced 2x and the
    # phone's Resolution Auto: every target but SCENE byte for byte under the
    # exact blend at Original, within two levels elsewhere
    # (counted, port/render/test/png_level_diff.py); SKIP as rd_vustage_ab
    if(Python3_FOUND)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fades_fold_ab")
        add_test(NAME rd_fades_fold_ab
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/fades_fold_dumps.txt"
                         "-DBOOT=${ICO_BOOT_DUMP_DIR}" "-DPY=${Python3_EXECUTABLE}"
                         "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/fades_fold_ab"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fades_fold_ab.cmake")
        set_tests_properties(rd_fades_fold_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600)
        if(_env)
            set_tests_properties(rd_fades_fold_ab PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endif()
    # rd_crack_late_ab (test/rd_crack_late_ab.cmake): the dumps and baselines of
    # tools/crack_late_dumps.txt (machine-local: the gate's Enhanced 2x pass
    # dumps and the PowerVR phone's frames at its Resolution Auto, made by the
    # build that drew the crack fill before the fog) replayed with the fill
    # after lists 4-6 (rd_crack_fill.c): every target within the measured bound
    # (DIVERGENCES.md VD-20), the fill drawn once after lists 4-6, at the
    # phone's setting one copy fewer and no pass more; SKIP as rd_vustage_ab
    if(Python3_FOUND)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/crack_late_ab")
        add_test(NAME rd_crack_late_ab
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/crack_late_dumps.txt"
                         "-DPY=${Python3_EXECUTABLE}"
                         "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/crack_late_ab"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack_late_ab.cmake")
        set_tests_properties(rd_crack_late_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600)
        if(_env)
            set_tests_properties(rd_crack_late_ab PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endif()
    # rd_aura_box_ab (test/rd_aura_box_ab.cmake): the dumps of
    # tools/aura_box_dumps.txt (vustage_dumps.txt's and the PowerVR phone's
    # three F12 frames) with the aura's read of the picture into FEED128 in
    # one pass (ICO_RD_AURA_BOX=0) and in two (=1, rd_aura_box.c), under
    # ICO_RD_BLEND=fixed at the phone's Resolution Auto and Enhanced 2x 4:3:
    # every target byte for byte, one pass and one draw more a read in two
    # passes; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aura_box_ab")
    add_test(NAME rd_aura_box_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/aura_box_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/aura_box_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_aura_box_ab.cmake")
    set_tests_properties(rd_aura_box_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_aura_box_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_shadow_merge_ab (test/rd_shadow_merge_ab.cmake): the dumps of
    # tools/shadow_merge_dumps.txt (vustage_dumps.txt's, the PowerVR phone's
    # F12 frames and the heavy stage 6 room of 2026-11-02) with the shadow volumes in a pass
    # of their own (ICO_RD_SHADOW_MERGE=0) and in the scene's open pass (=1,
    # rd_shadow_merge.c), under ICO_RD_BLEND=fixed and ICO_RD_STORE_POISON=1
    # at the phone's Resolution Auto (the MoltenVK fakes) and Enhanced 2x
    # 4:3: every target byte for byte, the same draws, one pass fewer a reset
    # taken, some reset taken; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shadow_merge_ab")
    add_test(NAME rd_shadow_merge_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/shadow_merge_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/shadow_merge_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_merge_ab.cmake")
    set_tests_properties(rd_shadow_merge_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_shadow_merge_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_pass_area_ab (test/rd_pass_area_ab.cmake): the dumps of
    # tools/pass_area_dumps.txt (vustage_dumps.txt's, the PowerVR phone's
    # three F12 frames and its rc3 frame) with the passes' render areas off
    # (ICO_RD_PASS_AREA=0) and on (=1, rd_pass_area.c), under
    # ICO_RD_BLEND=fixed at the phone's Resolution Auto and Enhanced 2x 4:3:
    # every target byte for byte (lavapipe clips to the area, so a wrong
    # area would show), the same passes and draws but for the passes ended
    # early, and some pass with an area on the phone's frames; SKIP as
    # rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pass_area_ab")
    add_test(NAME rd_pass_area_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/pass_area_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/pass_area_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_pass_area_ab.cmake")
    set_tests_properties(rd_pass_area_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_pass_area_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # rd_glow_cheap_ab (test/rd_glow_cheap_ab.cmake): the glow dumps of
    # tools/glow_cheap_dumps.txt against the pass dumps the build before the
    # glow's cheaper passes stored (machine-local: the phone's profile under
    # build-host/tmp/v098/evidence/glow/pd-phone, Original and Enhanced 2x 4:3
    # under build-host/tmp/v095/after/w3/pd): every target within 0 levels,
    # at least two glow passes fewer at the phone's setting (WORK2's fill in
    # the mask's pass, cleanUpFB in the fog's), none more elsewhere (counted
    # in a replay without the crack fill where it is drawn: it ends the fog's
    # pass before the glow); the frames the crack fill changed against their
    # refreshed pictures (build-host/tmp/v098/evidence/glow/pd-w15); SKIP
    # without the dumps or their baselines
    if(Python3_FOUND)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/glow_cheap_ab")
        add_test(NAME rd_glow_cheap_ab
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/glow_cheap_dumps.txt"
                         "-DBASE_PHONE=${CMAKE_SOURCE_DIR}/build-host/tmp/v098/evidence/glow/pd-phone"
                         "-DBASE_DESK=${CMAKE_SOURCE_DIR}/build-host/tmp/v095/after/w3/pd"
                         "-DBASE_REFRESH=${CMAKE_SOURCE_DIR}/build-host/tmp/v098/evidence/glow/pd-w15"
                         "-DPY=${Python3_EXECUTABLE}"
                         "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/glow_cheap_ab"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_glow_cheap_ab.cmake")
        set_tests_properties(rd_glow_cheap_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600)
        if(_env)
            set_tests_properties(rd_glow_cheap_ab PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endif()
    # rd_pass_cut_ab (test/rd_pass_cut_ab.cmake): the dumps of
    # tools/pass_cut_dumps.txt with every pass cut off and each on alone and
    # all on (rd_pass_cut.c: ICO_RD_CLEAR_CARRY, ICO_RD_CLEAR_SKIP,
    # ICO_RD_LAST_STORE), under ICO_RD_BLEND=fixed, the store plan and its
    # poison, at the phone's Resolution Auto with its paths and Enhanced 2x
    # 4:3: every target and the present byte for byte, the passes fewer by
    # the clears carried and skipped, the depth stores dropped more by those
    # at the last touch, and each cut cutting something on the phone's
    # frames; SKIP as rd_vustage_ab
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pass_cut_ab")
    add_test(NAME rd_pass_cut_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/pass_cut_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/pass_cut_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_pass_cut_ab.cmake")
    set_tests_properties(rd_pass_cut_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(_env)
        set_tests_properties(rd_pass_cut_ab PROPERTIES ENVIRONMENT "${_env}")
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
    # GS state as shader data (rd_uber.c) on lavapipe, which orders the
    # attachment read (ICO_RD_UBER=on: auto takes it on tile-based GPUs
    # only): rd_pixel's cases and expectations as under the attachment read
    # (its exact-mode matrix leaves out and counts the cells the colour read
    # draws its own way: the premultiplied subtracts' floor and DATE per
    # primitive; testUberSubtractOracle holds the subtracts to the GS's
    # arithmetic, testDateStencil checks the per-primitive DATE)
    set(_env_uber ${_env} "ICO_RD_UBER=on")
    set(_bub "${CMAKE_CURRENT_BINARY_DIR}/uber")
    file(MAKE_DIRECTORY "${_bub}")
    add_test(NAME rd_pixel_uber COMMAND rd_pixel_test "${_bub}")
    set_tests_properties(rd_pixel_uber PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_bub}"
                                                  ENVIRONMENT "${_env_uber}")
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
                     "-DRESULTS=stencil ok 5, snapshot ok 5"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
    set_tests_properties(rd_date_selftest PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                     ENVIRONMENT "${_env_date_stencil}")
    # DATE under GS state as shader data (rd_uber.c, ICO_RD_UBER=on): the
    # colour read's per-primitive DATE first (its fifth sample the GS's:
    # the second sprite fails over the first), then the stencil bit and the
    # snapshot with it held off (their fifth sample the alpha before the
    # draw); the path stays the colour read
    add_test(NAME rd_date_selftest_uber
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=colour read"
                     "-DRESULTS=colour read ok 5, stencil ok 5, snapshot ok 5"
                     "-DEXTRA=pipelines: GS state as shader data: on \\(raster-order read, \\[dev\\] uber = on\\)"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
    set_tests_properties(rd_date_selftest_uber
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env_date_stencil};ICO_RD_UBER=on")
    # the shadow count's self-test (rd_shadow_selftest.c): the one draw
    # equal to the seven draws byte for byte at both scene sizes of the
    # tool (a wide scene at scale 2 with Full pixel's left-out texels, then
    # another size), every band centre the count's colour, and the line that
    # says how the stencil is read (four texels a gather, or a texel a load
    # where the gathers' count differs); sabotaged (the
    # one draw's table wrong at m = 1), the one draw differs, the profile's
    # flag goes off and the seven draws resolve the count (no second line:
    # nothing to check then); [dev] shadow_one = false, no self-test and the
    # profile's words "shadow count seven draws ([dev])"
    add_test(NAME rd_shadow_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=one draw"
                     "-DRESULTS=equal to the seven draws over [0-9]+x[0-9]+, 142 band centres ok"
                     "-DEXTRA=profile: .*shadow count one draw"
                     "-DEXTRA2=shadow count: the stencil read (four texels a gather, equal to the seven draws byte for byte|a texel a load, as the gathers' count)"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    set_tests_properties(rd_shadow_selftest PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                       ENVIRONMENT "${_env}")
    # the same on a cropped frame (624 wide at 16:10, exact 614.40: the last
    # band's centre fell in the left-out columns and turned the one draw
    # off) and on one exactly its width (2048 at 48:9)
    foreach(_c IN ITEMS "cropped|624|448|1|0|0|0|0|0|1.6" "wide|2048|448|1|0|0|0|0|0|5.3333333")
      string(REPLACE "|" ";" _p "${_c}")
      list(GET _p 0 _n)
      list(REMOVE_AT _p 0)
      list(GET _p 0 _w)
      string(REPLACE ";" "|" _a "${_p}")
      add_test(NAME rd_shadow_selftest_${_n}
               COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                       "-DPATH=one draw" "-DARGS=${_a}"
                       # the tool runs the self-test twice (its first scene, then the
                       # one made at the asked size): every line one draw with its
                       # 142 centres ok, and the asked width's line present
                       "-DRESULTS=equal to the seven draws over [0-9]+x448, 142 band centres ok"
                       "-DEXTRA=shadow count: one draw \\(self-test: equal to the seven draws over ${_w}x448, 142 band centres ok"
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
      set_tests_properties(rd_shadow_selftest_${_n} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                           ENVIRONMENT "${_env}")
    endforeach()
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
    # the store plan's keep (rd_store_plan_test.c; rd_store_plan_ab below
    # holds the drops byte for byte over the corpus)
    add_test(NAME rd_store_plan COMMAND rd_store_plan_test)
    set_tests_properties(rd_store_plan PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${_env}")
    # the passes cut (rd_pass_cut_test.c; rd_pass_cut_ab holds them byte for
    # byte over the corpus)
    add_test(NAME rd_pass_cut COMMAND rd_pass_cut_test)
    set_tests_properties(rd_pass_cut PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${_env}")
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
                     "-DRESULTS=stencil fail [1-5] at sample [0-4]: [0-9A-F]+ for [0-9A-F]+, snapshot ok 5"
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

# VK11S (issue 145): the Adreno combination.  A phone's Vulkan 1.1 driver
# (Android 11, an Adreno 618) has the fence ring and VkRenderPass objects of the
# VK11 block AND all-static pipelines (tilerDevice forces them on Android,
# vk_device.c); the VK11 twins above still offer VK_EXT_extended_dynamic_state
# on lavapipe, so that stack was never run.  ICO_VK_FAKE_API=1.1 with
# ICO_VK_FAKE_NO_DYNAMIC=1 is it.  The same shards as VK11, own folder vk11s/,
# the Vulkan runs' expectations byte for byte (neither switch changes a pixel);
# rd_perf_vk11s's counts are rd_perf's.  Label vk11: ctest -j1 -L vk11.
if(ICO_RUN_TESTS)
    set(_env_vk11s ${_env} "ICO_VK_FAKE_API=1.1" "ICO_VK_FAKE_NO_DYNAMIC=1")
    set(_v11s "${CMAKE_CURRENT_BINARY_DIR}/vk11s")
    file(MAKE_DIRECTORY "${_v11s}")
    add_test(NAME rd_pixel_vk11s COMMAND rd_pixel_test "${_v11s}")
    add_test(NAME rd_present_vk11s COMMAND rd_present_test "${_v11s}")
    add_test(NAME rd_crt_vk11s COMMAND rd_crt_test "${_v11s}")
    add_test(NAME rd_debug_vk11s COMMAND rd_debug_test "${_v11s}")
    add_test(NAME rd_raw_vk11s COMMAND rd_raw_test)
    add_test(NAME rd_fog_vk11s COMMAND rd_fog_test)
    add_test(NAME rd_shadow_vk11s COMMAND rd_shadow_test)
    add_test(NAME rd_interp_vk11s COMMAND rd_interp_test)
    add_test(NAME rd_msaa_vk11s COMMAND rd_msaa_test)
    add_test(NAME rd_perf_vk11s COMMAND rd_perf_test)
    set(_vk11s_tests rd_pixel_vk11s rd_present_vk11s rd_crt_vk11s rd_debug_vk11s rd_raw_vk11s
                     rd_fog_vk11s rd_shadow_vk11s rd_interp_vk11s rd_msaa_vk11s rd_perf_vk11s)
    foreach(_k RANGE 1 4)
        add_test(NAME rd_mesh_vk11s.${_k}of4 COMMAND rd_mesh_test "${ICO_DISC_IMAGE}" --shard ${_k}/4)
        list(APPEND _vk11s_tests rd_mesh_vk11s.${_k}of4)
    endforeach()
    set_tests_properties(${_vk11s_tests}
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_v11s}"
                                    ENVIRONMENT "${_env_vk11s}")
    foreach(_t ${_vk11s_tests})
        get_test_property(${_t} LABELS _labels)
        set_tests_properties(${_t} PROPERTIES LABELS "${_labels};vk11")
    endforeach()
endif()
# VK11S end
# VK10: the render tests on a Vulkan 1.0 driver (ICO_VK_FAKE_API=1.0,
# port/rhi/vk/vk_device.c; issue 158, Android 8): the instance created at
# Vulkan 1.0 with VK_KHR_get_physical_device_properties2 (the KHR queries),
# the device at 1.0 with VK_KHR_maintenance1 and VK_KHR_maintenance2, the
# core 1.1 entry points cleared (a core 1.1 call left unguarded stops the
# run), and what =1.1 hides hidden (the fence ring, VkRenderPass objects, no
# local read, driver properties or image format list); with
# ICO_VK_FAKE_NO_DYNAMIC=1 every pipeline state static, as on Android (the
# combination an Android 8 phone runs).  The expectations are the Vulkan
# runs' own, byte for byte; rd_perf_vk10's pass and barrier counts are
# rd_perf's.  Label vk10: ctest -j1 -L vk10 (with port/rhi's rhi_vk_vk10 and
# rhi_vk_swapchain_vk10).
if(ICO_RUN_TESTS)
    set(_env_vk10 ${_env} "ICO_VK_FAKE_API=1.0" "ICO_VK_FAKE_NO_DYNAMIC=1")
    set(_v10 "${CMAKE_CURRENT_BINARY_DIR}/vk10")
    file(MAKE_DIRECTORY "${_v10}")
    add_test(NAME rd_pixel_vk10 COMMAND rd_pixel_test "${_v10}")
    add_test(NAME rd_present_vk10 COMMAND rd_present_test "${_v10}")
    add_test(NAME rd_crt_vk10 COMMAND rd_crt_test "${_v10}")
    add_test(NAME rd_debug_vk10 COMMAND rd_debug_test "${_v10}")
    add_test(NAME rd_raw_vk10 COMMAND rd_raw_test)
    add_test(NAME rd_fog_vk10 COMMAND rd_fog_test)
    add_test(NAME rd_shadow_vk10 COMMAND rd_shadow_test)
    add_test(NAME rd_interp_vk10 COMMAND rd_interp_test)
    add_test(NAME rd_msaa_vk10 COMMAND rd_msaa_test)
    add_test(NAME rd_perf_vk10 COMMAND rd_perf_test)
    set(_vk10_tests rd_pixel_vk10 rd_present_vk10 rd_crt_vk10 rd_debug_vk10 rd_raw_vk10
                    rd_fog_vk10 rd_shadow_vk10 rd_interp_vk10 rd_msaa_vk10 rd_perf_vk10)
    # rd_mesh's four shards (the disc's models; 77 without the disc image)
    foreach(_k RANGE 1 4)
        add_test(NAME rd_mesh_vk10.${_k}of4 COMMAND rd_mesh_test "${ICO_DISC_IMAGE}" --shard ${_k}/4)
        list(APPEND _vk10_tests rd_mesh_vk10.${_k}of4)
    endforeach()
    set_tests_properties(${_vk10_tests}
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_v10}"
                                    ENVIRONMENT "${_env_vk10}")
    foreach(_t ${_vk10_tests})
        get_test_property(${_t} LABELS _labels)
        set_tests_properties(${_t} PROPERTIES LABELS "${_labels};vk10")
    endforeach()
    # the same with the D32F attachment bit hidden (ICO_VK_FAKE_NO_D32F_ATTACHMENT,
    # an Adreno 505 on a 1.0.61 driver): the start-up set without the effects
    # depth's two keys, the effects depth not drawn; the picture tests' own
    # expectations
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10nod32f")
    set(_vk10n "${CMAKE_CURRENT_BINARY_DIR}/vk10nod32f")
    add_test(NAME rd_pixel_vk10_nod32f COMMAND rd_pixel_test "${_vk10n}")
    add_test(NAME rd_present_vk10_nod32f COMMAND rd_present_test "${_vk10n}")
    add_test(NAME rd_interp_vk10_nod32f COMMAND rd_interp_test)
    set(_vk10n_tests rd_pixel_vk10_nod32f rd_present_vk10_nod32f rd_interp_vk10_nod32f)
    set_tests_properties(${_vk10n_tests}
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_vk10n}"
                                    ENVIRONMENT "${_env_vk10};ICO_VK_FAKE_NO_D32F_ATTACHMENT=1")
    foreach(_t ${_vk10n_tests})
        get_test_property(${_t} LABELS _labels)
        set_tests_properties(${_t} PROPERTIES LABELS "${_labels};vk10")
    endforeach()
endif()
# VK10 end

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
#    rd_crack_tiler(_on, _fold, _fold_mali), rd_crack_glow, rd_crack_aura, rd_blend_selftest*,
#    rd_date_selftest_sabotage,
#    rd_fog_selftest_local_sabotage (ico_rd_gl_twin skips these by itself)
#  * the tests that start the game (model_viewer_headless, gallery_sweep,
#    credits_headless, state_roundtrip_headless(_settings)) or work on the replay tool's files (rd_replay_tool*,
#    rd_crack's replay, rd_fog_depth_copy_dump*, rd_vustage_ab, rd_vubatch_ab, rd_vumerge_ab,
#    rd_vufuse_ab, rd_store_plan_ab, rd_constant_reads_ab, rd_fades_fold_ab, rd_thread_ab,
#    the native frame gates, coverage_corpus; tools/native_frame_check.py compare runs the dumps
#    on both backends; rd_cull_ab is twinned: the culler's byte identity is proven on
#    both lavapipe backends; rd_vis_hiz_corpus is not: its counts are the CPU's)
#  * tests with no device (rd_alpha_plan, rd_fx_frame, rd_vu_frac, rd_fades_fold, rd_vis,
#    rd_vis_hiz, rd_mip,
#    rd_passring,
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
        rd_jitter rd_upscale
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
        rd_shadow_selftest rd_shadow_selftest_cropped rd_shadow_selftest_wide
        rd_shadow_selftest_sabotage
        rd_vuprepass_selftest rd_vuprepass_selftest_plain
        rd_vuprepass_selftest_sabotage rd_vuprepass_selftest_off rd_vuprepass_selftest_ticks
        rd_vuprepass_selftest_sabotage_tri rd_vuprepass_selftest_unbatched
        rd_vuprepass_selftest_unmerged rd_vuprepass_selftest_sabotage_straddle
        # the scene draws' visibility test against every draw replayed, byte for
        # byte on OpenGL too (rd_cull_ab.cmake: the tool takes ICO_RHI_BACKEND)
        rd_cull_ab)
    foreach(_n IN LISTS _ico_rd_gl_tests)
        ico_rd_gl_twin(${_n})
    endforeach()
    get_property(_gl_done GLOBAL PROPERTY ICO_RD_GL_DONE)
    list(LENGTH _gl_done _gl_n)
    message(STATUS "ico: ${_gl_n} render tests registered again on OpenGL (ctest -L gl)")
endif()
# ---- OpenGL twins end ----
