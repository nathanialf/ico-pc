# rd_crack_work.cmake: the crack fill's snapshot by the swap and its early
# leave (rd_crack_fill.c FILLSWAP and CRACKTAP; docs/port/RENDER_POST.md, "The
# crack fill's snapshot by the swap"), included from rd_aura_paste.cmake
# (port/render/CMakeLists.txt is at the 256 KiB cap, tools/check_no_rom.sh).
# rd_crack_test checks both in every rd_crack_* mode where the filler draws
# (checkWork: the swap's self-test, the swap and the early leave forced on
# against off, byte for byte); _dc_env is rd_date_cut.cmake's validation
# layer env.

# rd_crack_work_ab (test/rd_crack_work_ab.cmake): the dumps of
# tools/crack_work_dumps.txt (machine-local; a listed dump that is absent
# fails) with ICO_RD_CRACK_SWAP and ICO_RD_CRACK_TAP2 both 0 and both 1,
# --pass-dump --stats, at the PowerVR phone's profile (ICO_VK_FAKE_LIMITS=moltenvk)
# and lavapipe's own at 1344x1008, 3x and 4x: every target and PRESENT byte
# for byte, the same draws and passes, the copies and colour loads fewer by
# the swapped snapshots' MB, the marked dumps' swaps and early-leave draws;
# rd_crack_work_gl: the phone's setting on OpenGL (llvmpipe), the fill in a pass
# of its own there.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/crack_work_ab")
    add_test(NAME rd_crack_work_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/crack_work_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/crack_work_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack_work_ab.cmake")
    set_tests_properties(rd_crack_work_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_dc_env)
        set_tests_properties(rd_crack_work_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/crack_work_ab")
        add_test(NAME rd_crack_work_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/crack_work_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/crack_work_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack_work_ab.cmake")
        set_tests_properties(rd_crack_work_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
