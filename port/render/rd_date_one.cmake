# rd_date_one.cmake: one DATE set-up for the shadow composites (rd_date_one.c,
# rd_date_one.h), the empty composites skipped (rd_shadow_box.c) and a shadow
# window with no volume taken with SCENE's pass ended at the resolve
# (rd_no_volume.c's break mode; docs/port/RENDER_POST.md), included from
# rd_date_cut.cmake (port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).  _dc_env is rd_date_cut.cmake's (the validation
# layer for the device tests).
target_sources(ico_render PRIVATE rd_date_one.c)

# rd_date_one_test: the bound and the group rule (CPU: the retail stage data,
# the debug menu's 128 x 128, DATM 1, FBA 1, AFAIL FB_ONLY, a draw or another
# target between, the 10 s clause and the --stats words), then on a Vulkan
# device frames in the shadow's shape: one DATE set-up off and on (SCENE byte
# for byte, 3 snapshots against 1, 4 passes fewer), the 128 x 128 frame refused
# and with the bound ignored differing, a frame with no volume with the empty
# composites off and on (5 passes fewer, no snapshot); 77 without a device
add_executable(rd_date_one_test test/rd_date_one_test.c)
target_compile_options(rd_date_one_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_date_one_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                    "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_date_one_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_date_one_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_date_one COMMAND rd_date_one_test)
    set_tests_properties(rd_date_one PROPERTIES SKIP_RETURN_CODE 77)
    if(_dc_env)
        set_tests_properties(rd_date_one PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
endif()

# rd_date_one_ab, rd_empty_skip_ab, rd_no_volume_break_ab
# (test/rd_shadow_cuts_ab.cmake, -DITEM=dateone|emptyskip|novolbreak): the dumps
# of tools/shadow_cuts_dumps.txt (the Mali reporter's four, the Pixel's three
# opening dumps) and of tools/date_cut_dumps.txt, tools/shadow_box_dumps.txt and
# tools/no_volume_dumps.txt (machine-local; a listed dump that is absent fails)
# with the item off and on, the other two items off in both runs, at the
# profiles each item's test names: every target the same bytes, the passes,
# draws, snapshots and set-ups by what the code leaves out.  The _gl twins: the
# same at 1x on OpenGL (llvmpipe: DATE by the snapshot).  No device: SKIP (77).
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    set(_sc_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _sc_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    set(_sc_lists
        "${CMAKE_SOURCE_DIR}/tools/shadow_cuts_dumps.txt|${CMAKE_SOURCE_DIR}/tools/date_cut_dumps.txt|${CMAKE_SOURCE_DIR}/tools/shadow_box_dumps.txt|${CMAKE_SOURCE_DIR}/tools/no_volume_dumps.txt")
    foreach(_sc_item dateone emptyskip novolbreak)
        if(_sc_item STREQUAL "dateone")
            set(_sc_name rd_date_one_ab)
        elseif(_sc_item STREQUAL "emptyskip")
            set(_sc_name rd_empty_skip_ab)
        else()
            set(_sc_name rd_no_volume_break_ab)
        endif()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_sc_name}")
        add_test(NAME ${_sc_name}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLISTS=${_sc_lists}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/${_sc_name}"
                         -DBACKEND=vulkan -DITEM=${_sc_item}
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_cuts_ab.cmake")
        set_tests_properties(${_sc_name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 10800)
        if(_sc_env)
            set_tests_properties(${_sc_name} PROPERTIES ENVIRONMENT "${_sc_env}")
        endif()
        if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/${_sc_name}")
            add_test(NAME ${_sc_name}_gl
                     COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                             "-DLISTS=${_sc_lists}"
                             "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/${_sc_name}" -DBACKEND=opengl
                             -DITEM=${_sc_item}
                             -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_cuts_ab.cmake")
            set_tests_properties(${_sc_name}_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                 TIMEOUT 10800 LABELS gl
                                 ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
        endif()
    endforeach()
endif()
