# port/fmv: the FMV player. The game's ito/mpeg player is not compiled on
# the host (ICO_EE_ONLY_SOURCES); movie_init/movie_proc/movie_end are
# movie.c's, on a PSS demuxer (pss.c) and Ittiam libmpeg2 (m2v.c).
#
# libmpeg2 comes from tools/fetch_deps.sh as a source tree
# (tools/toolchain/deps/libmpeg2, Apache-2.0) and is compiled here with each
# preset's own compiler into a static library: the generic C decoder (the
# library's riscv function selectors, which pick the generic C routines and
# nothing else), no SIMD, its pthread layer replaced by ithread_single.c.
# On arm64 Android/Linux (ICO_LIBMPEG2_NEON, the top
# CMakeLists.txt) the library's NEON assembly and its armv8 selectors
# instead, as Android's own build of the library has them (ARMV8,
# DEFAULT_ARCH D_ARCH_ARMV8_GENERIC): the phone decodes movies several times
# faster.  x86 and the rest are unchanged.
set(ICO_LIBMPEG2_DIR "${ICO_DEPS_DIR}/libmpeg2" CACHE PATH
    "Ittiam libmpeg2 source tree (tools/fetch_deps.sh)")

if(NOT EXISTS "${ICO_LIBMPEG2_DIR}/decoder/impeg2d.h")
    message(STATUS "ico: no libmpeg2 at ${ICO_LIBMPEG2_DIR} (tools/fetch_deps.sh): FMV not built")
    if(TARGET ico_pc)
        message(FATAL_ERROR "ico_pc needs port/fmv: run tools/fetch_deps.sh")
    endif()
    return()
endif()

set(_m2 "${ICO_LIBMPEG2_DIR}")
add_library(ico_libmpeg2 STATIC
    ${_m2}/common/impeg2_buf_mgr.c
    ${_m2}/common/impeg2_disp_mgr.c
    ${_m2}/common/impeg2_format_conv.c
    ${_m2}/common/impeg2_globals.c
    ${_m2}/common/impeg2_idct.c
    ${_m2}/common/impeg2_inter_pred.c
    ${_m2}/common/impeg2_job_queue.c
    ${_m2}/common/impeg2_mem_func.c
    ${_m2}/decoder/impeg2d_api_main.c
    ${_m2}/decoder/impeg2d_bitstream.c
    ${_m2}/decoder/impeg2d_debug.c
    ${_m2}/decoder/impeg2d_dec_hdr.c
    ${_m2}/decoder/impeg2d_decoder.c
    ${_m2}/decoder/impeg2d_d_pic.c
    ${_m2}/decoder/impeg2d_function_selector_generic.c
    ${_m2}/decoder/impeg2d_globals.c
    ${_m2}/decoder/impeg2d_i_pic.c
    ${_m2}/decoder/impeg2d_mc.c
    ${_m2}/decoder/impeg2d_mv_dec.c
    ${_m2}/decoder/impeg2d_pic_proc.c
    ${_m2}/decoder/impeg2d_pnb_pic.c
    ${_m2}/decoder/impeg2d_vld.c
    ${_m2}/decoder/impeg2d_vld_tables.c
    ${_m2}/decoder/impeg2d_deinterlace.c
    ${_m2}/common/icv_sad.c
    ${_m2}/common/icv_variance.c
    ${_m2}/common/ideint.c
    ${_m2}/common/ideint_cac.c
    ${_m2}/common/ideint_debug.c
    ${_m2}/common/ideint_function_selector_generic.c
    ${_m2}/common/ideint_utils.c
    ithread_single.c)
if(ICO_LIBMPEG2_NEON)
    # the armv8 selectors pick the _av8 routines (the generic ones stay for
    # what has no NEON version); the platform macros are armv8's
    target_sources(ico_libmpeg2 PRIVATE
        ${_m2}/decoder/arm/impeg2d_function_selector.c
        ${_m2}/decoder/arm/impeg2d_function_selector_av8.c
        ${_m2}/common/arm/ideint_function_selector.c
        ${_m2}/common/arm/ideint_function_selector_av8.c
        ${_m2}/common/armv8/icv_sad_av8.s
        ${_m2}/common/armv8/icv_variance_av8.s
        ${_m2}/common/armv8/ideint_cac_av8.s
        ${_m2}/common/armv8/ideint_spatial_filter_av8.s
        ${_m2}/common/armv8/impeg2_format_conv.s
        ${_m2}/common/armv8/impeg2_idct.s
        ${_m2}/common/armv8/impeg2_inter_pred.s
        ${_m2}/common/armv8/impeg2_mem_func.s)
    target_compile_definitions(ico_libmpeg2 PRIVATE ARMV8 DEFAULT_ARCH=D_ARCH_ARMV8_GENERIC)
    # "${_m2}/common/armv8" also serves the assembly's .include
    target_include_directories(ico_libmpeg2 PUBLIC "${_m2}/common" "${_m2}/decoder"
                                                  "${_m2}/common/armv8")
else()
    target_sources(ico_libmpeg2 PRIVATE ${_m2}/decoder/riscv/impeg2d_function_selector.c
                                        ${_m2}/common/riscv/ideint_function_selector.c)
    target_include_directories(ico_libmpeg2 PUBLIC "${_m2}/common" "${_m2}/decoder"
                                                  "${_m2}/common/riscv")
endif()
if(ANDROID)
    # <log/log.h> is AOSP's, not the NDK's (android/log/log.h)
    target_include_directories(ico_libmpeg2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/android")
endif()
# third-party code: its own warnings are not ours to fix
target_compile_options(ico_libmpeg2 PRIVATE -w $<$<COMPILE_LANGUAGE:C>:-fwrapv>)
# The decoder left-shifts negative coefficients (impeg2d_vld.c), defined by
# GCC and Clang but undefined in ISO C; UBSan's shift check would stop the
# sanitizer builds inside the library.
if(ICO_SANITIZE)
    target_compile_options(ico_libmpeg2 PRIVATE $<$<COMPILE_LANGUAGE:C>:-fno-sanitize=shift>)
endif()
set_target_properties(ico_libmpeg2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The demuxer and the decoder wrapper: no game symbols, so a library.
add_library(ico_fmv STATIC pss.c m2v.c field_match.c)
target_include_directories(ico_fmv PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_fmv PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_fmv PUBLIC ico_libmpeg2)

# movie.c calls back into the game (cdvd.c's directory cache, the libcdvd
# stream, the sequencer's SgStPcm*, sceGsSyncV) and the game calls it, so it
# is part of the program itself, like port/null/gfx_null.c.
if(TARGET ico_pc)
    target_sources(ico_pc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/movie.c"
                                  "${CMAKE_CURRENT_SOURCE_DIR}/movie_pace.c")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/movie.c"
                                "${CMAKE_CURRENT_SOURCE_DIR}/movie_pace.c"
                                TARGET_DIRECTORY ico_pc PROPERTIES INCLUDE_DIRECTORIES
                                "${ICO_COMPAT_DIR};${CMAKE_SOURCE_DIR}/port/platform;${CMAKE_SOURCE_DIR}/port/data;${CMAKE_SOURCE_DIR}/sce/libsndn2;${CMAKE_SOURCE_DIR}/port/render")
    # movie.c draws its pictures through rd_video.c under ICO_RD.  ico_pc's
    # own definitions carry ICO_HEADLESS only (the game's ICO_RD is in
    # ICO_GAME_DEFINITIONS, on the game libraries), so the window build
    # names it here; without it the film played its sound and showed no
    # picture.
    if(NOT ICO_HEADLESS)
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/movie.c" TARGET_DIRECTORY ico_pc
                                    PROPERTIES COMPILE_DEFINITIONS ICO_RD=1)
    endif()
    target_link_libraries(ico_pc PRIVATE ico_fmv)
endif()

# --- tests --------------------------------------------------------------------
add_executable(fmv_test test/fmv_test.c movie_pace.c)
target_include_directories(fmv_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(fmv_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(fmv_test PRIVATE ico_fmv)
if(ICO_RUN_TESTS)
    add_test(NAME fmv COMMAND fmv_test "${ICO_DISC_IMAGE}")
endif()
# The disc's movies field by field (scan headers, line-difference and comb
# measures of the first pictures): a report, skipped without the image.
add_executable(fmv_fields_test test/fmv_fields_test.c)
target_include_directories(fmv_fields_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(fmv_fields_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(fmv_fields_test PRIVATE ico_fmv)
if(ICO_RUN_TESTS)
    add_test(NAME fmv_fields COMMAND fmv_fields_test "${ICO_DISC_IMAGE}")
    set_tests_properties(fmv_fields PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The YUV blit on the GPU (port/render/rd_video.c): rd's headless device
# (lavapipe in the container), as port/render's own tests run.
if(TARGET ico_render)
    add_executable(rd_video_test test/rd_video_test.c)
    target_include_directories(rd_video_test PRIVATE "${CMAKE_SOURCE_DIR}/port/render"
                               "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/rhi")
    target_compile_options(rd_video_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(rd_video_test PRIVATE ico_render)
    if(ICO_RUN_TESTS)
        add_test(NAME rd_video COMMAND rd_video_test)
        set(_env "")
        if(NOT WIN32 AND EXISTS "${ICO_DEPS_DIR}/vulkan-validation/VkLayer_khronos_validation.json")
            set(_env "VK_ADD_LAYER_PATH=${ICO_DEPS_DIR}/vulkan-validation"
                     "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true" "ICO_VK_VALIDATION=1")
        endif()
        if(ICO_SANITIZE MATCHES "address")
            list(APPEND _env "ASAN_OPTIONS=detect_leaks=0")
        endif()
        set_tests_properties(rd_video PROPERTIES SKIP_RETURN_CODE 77)
        if(_env)
            set_tests_properties(rd_video PROPERTIES ENVIRONMENT "${_env}")
        endif()
        # on a Mali-G68's limits (ICO_VK_FAKE_LIMITS=mali), as a phone has them
        add_test(NAME rd_video_mali COMMAND rd_video_test)
        set(_env_mali ${_env} "ICO_VK_FAKE_LIMITS=mali")
        set_tests_properties(rd_video_mali PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_env_mali}")
    endif()
    # rd on a window (SDL's offscreen driver): the output follows
    # a swapchain the backend rebuilt at another size on its own
    if(TARGET ico_rhi_vulkan AND TARGET SDL3::SDL3-shared)
        add_executable(rd_video_window_test test/rd_video_window_test.c)
        target_include_directories(rd_video_window_test PRIVATE "${CMAKE_SOURCE_DIR}/port/render"
                                   "${CMAKE_SOURCE_DIR}/port/shaders" "${CMAKE_SOURCE_DIR}/port/rhi")
        target_compile_options(rd_video_window_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                               ${ICO_PORT_WARNINGS})
        target_link_libraries(rd_video_window_test PRIVATE ico_render SDL3::SDL3-shared)
        ico_copy_sdl3_dll(rd_video_window_test)
        if(ICO_RUN_TESTS)
            add_test(NAME rd_video_window COMMAND rd_video_window_test)
            set_tests_properties(rd_video_window PROPERTIES SKIP_RETURN_CODE 77)
            if(_env)
                set_tests_properties(rd_video_window PROPERTIES ENVIRONMENT "${_env}")
            endif()
        endif()
    endif()
endif()
