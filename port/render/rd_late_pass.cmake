# rd_late_pass.cmake: fewer late full-size passes on SCENE, the same bytes
# (rd_late_pass.c, rd_late_pass.h; docs/port/RENDER_POST.md, "The late passes on
# SCENE"), included from rd_date_cut.cmake after ico_render, rd_replay_tool and
# rd_crack_test (port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).  _dc_env is rd_date_cut.cmake's validation layer env.
target_sources(ico_render PRIVATE rd_late_pass.c)

# rd_late_pass_test: the predicate on made-up commands (CPU, record-only): what
# the held crack fill waits past, where it lands, why it is drawn early; the
# store plan's scan over a DARK room's order with the hold, without it, and
# with a draw on SCENE before the aura's paste; rd__fx_alpha_only
add_executable(rd_late_pass_test test/rd_late_pass_test.c)
target_compile_options(rd_late_pass_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_late_pass_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi"
                                                     "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_late_pass_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_late_pass COMMAND rd_late_pass_test)
endif()

# rd_crack_held: rd_crack_test 2 held on the tile class's fakes
# (ICO_VK_FAKE_LIMITS=moltenvk): the glow's cleanUpFB, a sprite into WORK0 (the
# fill's snapshot there), the glow's mask sampling SCENE's alpha (SCENE's border
# repeat before it: the frame is 640 GS pixels wide) and the aura's paste, with a
# crack in GS column 2 (the repeat's source); the fill held to the paste's pass
# (rd__late_pass_force with the plan ignored) against the late pass off: SCENE's
# bytes the same (the repeated columns hold the source's fill), one render pass
# fewer, one held fill drawn after the repeat
if(ICO_RUN_TESTS AND TARGET rd_crack_test)
    add_test(NAME rd_crack_held
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|held"
                     "-DEXPECT=rd: crack fill: on above 1x \\(scale 2x2, reach 1x1"
                     "-DEXPECT2=rd: late pass: the crack fill's draw held from its snapshot to the aura's paste pass"
                     "-DDENY=rd: crack fill: the aura's paste cannot take the fill"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    set_tests_properties(rd_crack_held PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_dc_env};ICO_VK_FAKE_LIMITS=moltenvk")
endif()

# rd_crack_heldglow: rd_crack_test 2 heldglow, as rd_crack_held with a sprite into
# WORK2 under a Z GEQUAL test of SCENE's depth after the mask (a lamp's
# stand-in, no Z write) and the glow's paste (WORK0 added over SCENE through
# Cs * FIX + Cd, the fx plan's premultiplied add) in place of the aura's: the
# fill held into the glow's paste pass, the same checks
if(ICO_RUN_TESTS AND TARGET rd_crack_test)
    add_test(NAME rd_crack_heldglow
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|heldglow"
                     "-DEXPECT=rd: crack fill: on above 1x \\(scale 2x2, reach 1x1"
                     "-DEXPECT2=rd: late pass: the crack fill's draw held from its snapshot to the glow's paste pass"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    set_tests_properties(rd_crack_heldglow PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_dc_env};ICO_VK_FAKE_LIMITS=moltenvk")
endif()

# rd_crack_defer: rd_crack_test 2 defer, rd_crack_heldglow's frame with the aura's
# paste after the glow's and the aura's own sprite into WORK1 between them
# (without it the late pass off draws both pastes in one pass on SCENE, and the
# deferral saves nothing): the glow's paste deferred to the aura's paste pass and
# the fill held on to it (one held fill, none in the glow's pass, one paste
# deferred), the same checks with two render passes fewer than the late pass off
if(ICO_RUN_TESTS AND TARGET rd_crack_test)
    add_test(NAME rd_crack_defer
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|defer"
                     "-DEXPECT=rd: crack fill: on above 1x \\(scale 2x2, reach 1x1"
                     "-DEXPECT2=rd: late pass: the glow's paste deferred to the aura's paste pass"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    set_tests_properties(rd_crack_defer PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_dc_env};ICO_VK_FAKE_LIMITS=moltenvk")
endif()

# rd_crack_join: rd_crack_test 2 join on the tile class's fakes: the aura's frame
# (rd_crack_aura's) with two list 11 commands after the paste, each a sprite
# into SCENE under Z ALWAYS without a Z write: with the late pass both drawn in
# the paste's pass without SCENE's depth (2 draws, 1 pass), against the join off
# (rd__late_pass_force(RD_LATE_FILL)) SCENE's colour and depth byte for byte and
# one render pass fewer
if(ICO_RUN_TESTS AND TARGET rd_crack_test)
    add_test(NAME rd_crack_join
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_crack_test> "-DARGS=2|join"
                     "-DEXPECT=rd: crack fill: on above 1x \\(scale 2x2, reach 1x1"
                     "-DEXPECT2=rd: late pass: list 11's draws that use no depth drawn in the open pass on SCENE without its depth"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_crack.cmake")
    set_tests_properties(rd_crack_join PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_dc_env};ICO_VK_FAKE_LIMITS=moltenvk")
endif()

# rd_late_pass_ab (test/rd_late_pass_ab.cmake): the dumps of
# tools/late_pass_dumps.txt (machine-local; a listed dump that is absent fails)
# replayed by rd_replay_tool --pass-dump --stats with ICO_RD_LATE_PASS=0 and =1 at
# the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=moltenvk,
# ICO_RD_VU_PREPASS=0, Enhanced, 2364x1080, Full height, Full pixel) and at
# Enhanced 2x 4:3 under the same fakes (the PS2's border: no repeat): every
# target and PRESENT byte for byte, the same draws, passes fewer by the held
# fills exactly, the crack fill's own passes fewer by them too, the colour bytes
# loaded and stored fewer by SCENE's once a held fill each way, the copies the
# same; the off run holds nothing; the dumps the list marks hold one fill;
# rd_late_pass_gl: the phone's setting on OpenGL (llvmpipe) with the late pass on
# (the aura's paste reads a snapshot there: no fill held in a DARK room, one
# held into the glow's paste pass in a LAMPS room, drawn past the border repeat
# with a draw a repeated block; no list 11 join): the same bytes, the draws more
# by the blocks' the stats name.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/late_pass_ab")
    add_test(NAME rd_late_pass_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/late_pass_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/late_pass_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_late_pass_ab.cmake")
    set_tests_properties(rd_late_pass_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_dc_env)
        set_tests_properties(rd_late_pass_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/late_pass_ab")
        add_test(NAME rd_late_pass_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/late_pass_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/late_pass_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_late_pass_ab.cmake")
        set_tests_properties(rd_late_pass_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()

# the aura's paste split (rd_aura_paste.c in ico_render) and its tests
include(rd_aura_paste.cmake)
