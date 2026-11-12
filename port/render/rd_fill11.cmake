# rd_fill11.cmake: the crack fill and list 11 in one pass on SCENE, the same
# bytes (rd_fill11.c, rd_fill11.h; docs/port/RENDER_POST.md, "The crack fill in
# list 11's pass"), included from rd_crack_work.cmake after ico_render,
# rd_replay_tool and rd_crack_test (port/render/CMakeLists.txt is at the 256 KiB
# cap, tools/check_no_rom.sh).  _dc_env is rd_date_cut.cmake's validation layer
# env.
target_sources(ico_render PRIVATE rd_fill11.c)

# rd_crack_fill11 (the self-test's twin): rd_crack_test 2 fill11 on lavapipe's
# own profile: the fill's depth twins' start-up self-test run and passed (its
# state line), then with the switch forced on the glow's cleanUpFB, a sprite into
# WORK0 (the fill's place, its snapshot there) and list 11's two sprites into
# SCENE under Z ALWAYS without a Z write: the fill the first draw of list 11's
# pass over the scratch depth; against the switch off SCENE's colour and depth
# byte for byte, one render pass fewer, no fill pass of its own;
# rd_crack_fill11_later: 2 fill11z, a third sprite of list 11 with a Z write: the
# held fill drawn in a pass of its own at list 11's first draw (a later draw uses
# SCENE's depth), the same passes and bytes;
# rd_crack_fill11_tiler: 2 fill11 on the tile class's fakes
# (ICO_VK_FAKE_LIMITS=moltenvk: the passes on SCENE readable, the fog's local
# read) with the pass area off (ICO_RD_PASS_AREA=0): the landing, as
# rd_crack_fill11;
# rd_crack_fill11_area: 2 fill11 area, the same fakes with the pass area on (the
# tile class's default: list 11's pass would load and store its sprites' tiles
# alone): the held fill drawn in a pass of its own at list 11's first draw, the
# same passes and bytes
if(ICO_RUN_TESTS AND TARGET rd_crack_test)
    set(_f11_on "rd: crack fill: with list 11: on \\(self-test passed")
    add_test(NAME rd_crack_fill11
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|fill11"
                     "-DEXPECT=${_f11_on}"
                     "-DEXPECT2=rd: crack fill: drawn as the first draw of list 11's pass on SCENE over a scratch depth"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    add_test(NAME rd_crack_fill11_later
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|fill11z"
                     "-DEXPECT=${_f11_on}"
                     "-DDENY=rd: crack fill: drawn as the first draw of list 11's pass"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    add_test(NAME rd_crack_fill11_tiler
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|fill11"
                     "-DEXPECT=${_f11_on}"
                     "-DEXPECT2=rd: crack fill: drawn as the first draw of list 11's pass on SCENE over a scratch depth"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    add_test(NAME rd_crack_fill11_area
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|fill11|area"
                     "-DEXPECT=${_f11_on}"
                     "-DDENY=rd: crack fill: drawn as the first draw of list 11's pass"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    # every run with ICO_RD_FILL11_POISON=1 (rd_fill11.h): lavapipe keeps a
    # DONT_CARE load's old bytes, a tile-based GPU does not
    set_tests_properties(rd_crack_fill11 rd_crack_fill11_later PROPERTIES
                         SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_dc_env};ICO_RD_FILL11_POISON=1")
    set_tests_properties(rd_crack_fill11_tiler PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_dc_env};ICO_VK_FAKE_LIMITS=moltenvk;ICO_RD_PASS_AREA=0;ICO_RD_FILL11_POISON=1")
    set_tests_properties(rd_crack_fill11_area PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_dc_env};ICO_VK_FAKE_LIMITS=moltenvk;ICO_RD_FILL11_POISON=1")
endif()

# rd_fill11_ab (test/rd_fill11_ab.cmake): the dumps of tools/fill11_dumps.txt
# (machine-local; a listed dump that is absent fails) replayed by rd_replay_tool
# --pass-dump --stats with ICO_RD_FILL11=0 against the switch on: at the PowerVR
# phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=moltenvk, ICO_RD_LATE_PASS=1,
# ICO_RD_VU_PREPASS=0, Enhanced, 2364x1080, Full height, Full pixel) and at
# Enhanced 2x 4:3 on lavapipe's own (desktop) profile, the on runs with
# ICO_RD_FILL11=1 (off by default in this release), and with the Mali-G68's limits
# on an Arm phone's vendor (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5), the
# on run with nothing set (off: nothing may change): every target and PRESENT byte for
# byte, the same draws, the passes fewer by the fills drawn in list 11's pass
# exactly, the crack fill's own passes fewer by them too, the colour MB loaded
# and stored each fewer by the MB the stats name for them, the depth and stencil
# MB not more, the copies the same; the self-test passed where it ran; the dumps
# the list marks draw that many fills in list 11's pass; at least one over the
# list; rd_fill11_gl: the 4:3 setting on OpenGL (llvmpipe, ICO_RD_FILL11=1), the
# same pairs.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fill11_ab")
    add_test(NAME rd_fill11_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/fill11_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/fill11_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fill11_ab.cmake")
    set_tests_properties(rd_fill11_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_dc_env)
        set_tests_properties(rd_fill11_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/fill11_ab")
        add_test(NAME rd_fill11_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/fill11_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/fill11_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fill11_ab.cmake")
        set_tests_properties(rd_fill11_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
