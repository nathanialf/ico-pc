# rd_present_scene.cmake: the present from SCENE (rd_present_scene.c,
# rd_present_scene.h; docs/port/RENDER_POST.md "The present from SCENE (off by
# default until measured)"), included from port/render/CMakeLists.txt after
# ico_render and rd_replay_tool (a file of its own: port/render/CMakeLists.txt
# is at the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_present_scene.c)

# rd_present_scene_test: CPU only (rd__init_record_only): the reduction's map
# at the corpus's settings (one SCENE texel a DISPLAY texel at a whole offset:
# the Pixel's x + 1; refused at 2x and at half height), the bound's model
# (one fold step between levels before or after the filter: at most one
# level), the predicate's reason for every reader, and the debt's verdict on
# recorded frames (play drops it, a keep frame and the held copy draw
# DISPLAY first, the still re-present keeps it)
add_executable(rd_present_scene_test test/rd_present_scene_test.c)
target_compile_options(rd_present_scene_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_present_scene_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi"
                                                         "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_present_scene_test PRIVATE ico_render)

if(ICO_RUN_TESTS)
    add_test(NAME rd_present_scene COMMAND rd_present_scene_test)
endif()

# rd_present_scene_ab (test/rd_present_scene_ab.cmake): the dumps of
# tools/present_scene_dumps.txt (machine-local; a listed dump that is absent
# fails) replayed by rd_replay_tool --pass-dump with ICO_RD_PRESENT_SCENE=0
# and =1 at the PowerVR phone's profile on lavapipe (2364x1080, full height,
# Full pixel, ICO_VK_FAKE_LIMITS=moltenvk, ICO_RD_VU_PREPASS=0), a 16:9
# desktop window (1920x1080) and Enhanced 2x 4:3: every target byte for byte
# (DISPLAY drawn on demand for its read), PRESENT within the derived bound
# (two levels a channel; the levels' counts printed), the reduction not drawn
# at the phone's setting (one pass fewer), kept at 2x with its reason; on the
# first dump the readers: the CRT filter, FSR 1.0, the display layer (the
# test's upscaler), Downsampling and the effects depth keep it (each its
# reason, every target and PRESENT byte for byte), the mirror takes it.  No
# device: SKIP (77); no dump list: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool AND Python3_FOUND)
    set(_ps_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _ps_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/present_scene_ab")
    add_test(NAME rd_present_scene_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/present_scene_dumps.txt"
                     "-DPY=${Python3_EXECUTABLE}"
                     "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/present_scene_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_present_scene_ab.cmake")
    set_tests_properties(rd_present_scene_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 5400)
    if(_ps_env)
        set_tests_properties(rd_present_scene_ab PROPERTIES ENVIRONMENT "${_ps_env}")
    endif()
endif()
