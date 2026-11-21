# rd_vu_share.cmake: shared vertices for clip-mode-none strips (rd_vu_share.c,
# rd_vu_share.h; docs/port/PIPELINES.md, "Shared vertices for clip mode none";
# issue 179), included at the end of port/render/CMakeLists.txt (a file of its
# own: port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_vu_share.c)

# rd_vu_share_test: the pure parts (CPU, synthetic meshes): the tests'
# switch's words, the shared list of a strip and of two batches against their
# hand-worked values and the invariants that make it exact (the same vertex
# at every place, a triangle's first value its own, every value a kick of its
# batch), the forms that get no list, a draw's verdict for every reason it
# keeps the original list (the switch, the form, a chain plane, the hide and
# later marks, a probe, a range off the batches, rule mode region or scissor,
# not recorded) and for the draws that are no candidate (skinned), the bytes on
# the device, the counts and their texts (the block's kept triangles by reason)
add_executable(rd_vu_share_test test/rd_vu_share_test.c)
target_compile_options(rd_vu_share_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_vu_share_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                    "${CMAKE_SOURCE_DIR}/port/rhi"
                                                    "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_vu_share_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_vu_share COMMAND rd_vu_share_test)
endif()

# rd_vu_share_ab (test/rd_vu_share_ab.cmake): every fixture of
# test/fixtures/lists/vu_share.txt (committed frames: issue 179's Mali-G52
# stage 42 capture, whose dock meshes are prelit code 34 strips) with the
# shared vertices (ICO_RD_VU_SHARE=1, the shipped behaviour) against the
# original lists (=0), merged and one draw a command, under the corpus
# settings (4:3 at 1x, Enhanced 16:9 at 2x) and the Mali fakes (the record by
# compute dispatches, 21:9 at 1x): every target byte for byte, the =1 runs'
# shared triangles at least the fixture's declared vu_shared_triangles (the
# path ran), the =0 runs' none.  rd_vu_share_gl: the same on OpenGL
# (llvmpipe), the corpus settings.  No device: SKIP.
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_share_ab")
    add_test(NAME rd_vu_share_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_share.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_share_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_share_ab.cmake")
    set_tests_properties(rd_vu_share_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 3600)
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/vu_share_ab")
        add_test(NAME rd_vu_share_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_share.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/vu_share_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_share_ab.cmake")
        set_tests_properties(rd_vu_share_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
