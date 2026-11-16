# rd_plain_camera.cmake: the camera rule for static meshes and grids on a device that keeps the VU
# plain set (rd_plain_camera.c, rd_plain_camera.h; issue 123's right wall, issue 163's skinned
# draws; docs/port/VU1_PROGRAMS.md "The two variants", docs/port/DIVERGENCES.md VD-21), included
# at the end of port/render/CMakeLists.txt (a file of its own: port/render/CMakeLists.txt is near
# the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_plain_camera.c)

# rd_plain_camera_test: CPU only: the decision over every combination of device, rule and draw,
# the counts and the attempt ledger's row (its states, its block words), the 10 s line's words
# and the start line's
add_executable(rd_plain_camera_test test/rd_plain_camera_test.c rd_plain_camera.c)
target_include_directories(rd_plain_camera_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                        "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(rd_plain_camera_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_plain_camera_test PRIVATE ico_thread)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_plain_camera_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_plain_camera COMMAND rd_plain_camera_test)
endif()

# rd_plain_camera_ab (test/rd_plain_camera_ab.cmake): the fixtures of
# test/fixtures/lists/plain_camera.txt (the drops fixtures of lists/wide.txt, each with skinned
# draws, at their own aspect; docs/port/TESTING_FIXTURES.md; an absent one FAILS) on lavapipe
# put on the plain set as the device table's row keeps it (ICO_RD_VU_PLAIN=row) with the VU
# pre-pass off as on the issue 123 tablet (ICO_VK_FAKE_NO_PREPASS=1): (1)
# tools/region_camera_report.py at Original 1x and Enhanced 2x, the camera rule against
# region_rule = ps2 there: every SCENE pixel that changes inside the CPU oracle's mask, which
# holds no skinned triangle (rd_replay_region.c), so the skinned draws are byte for byte
# today's; (2) the path ran: the start line, the static draws by the camera rule and the skinned
# draws kept counted (--stats), triangles drawn by the camera rule (--region-stats); (3)
# region_rule = ps2 on the row gives today's bytes: every target equal to the plain set with
# the side bands off (ICO_RD_VU_PLAIN=1 ICO_RD_VU_BESIDE=0, the flags today's code gives the
# row), no draw by the camera rule.  No device: SKIP.
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool AND Python3_FOUND)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plain_camera_ab")
    add_test(NAME rd_plain_camera_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DPYTHON=${Python3_EXECUTABLE}"
                     "-DREPORT=${CMAKE_SOURCE_DIR}/tools/region_camera_report.py"
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/plain_camera.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/plain_camera_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_plain_camera_ab.cmake")
    set_tests_properties(rd_plain_camera_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 3600)
    if(_dc_env)
        set_tests_properties(rd_plain_camera_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
endif()
