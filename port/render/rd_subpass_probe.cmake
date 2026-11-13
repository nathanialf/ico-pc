# rd_subpass_probe.cmake: the subpass read's first-start probe
# (rd_subpass_probe.c, rd_subpass_probe.h; port/rhi/rhi_subpass_verdict.h
# decides at the device's start, tested in port/rhi: rhi_subpass_verdict,
# rhi_vk_subpass_verdict), included at the end of port/render/CMakeLists.txt
# (a file of its own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).  _dc_env is rd_date_cut.cmake's (the validation
# layer for the device tests).
target_sources(ico_render PRIVATE rd_subpass_probe.c)

# rd_subpass_probe_rule: the probe's rule on synthetic least times (no
# device), the hook doing nothing without a renderer
add_executable(rd_subpass_probe_test test/rd_subpass_probe_test.c)
target_compile_options(rd_subpass_probe_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_subpass_probe_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                         "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_subpass_probe_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_subpass_probe_test)
endif()

if(ICO_RUN_TESTS)
    add_test(NAME rd_subpass_probe_rule COMMAND rd_subpass_probe_test)
    # rd_subpass_probe_kept, _refused, _plain, _selftest, _end
    # (test/rd_subpass_probe.cmake): the probe's start on lavapipe through
    # rd_fog_selftest_test on the 1.1 profile with a tile-based profile, Arm's
    # class by its fake (ICO_VK_FAKE_VENDOR=0x13B5: off by default this
    # release) with the tests' switch over its default
    # (ICO_VK_SUBPASS_PROBE_CLASS=1), each
    # verdict's path by ICO_RD_SUBPASS_PROBE_SABOTAGE (fast, slow, plain,
    # end) or ICO_RD_DATE_SABOTAGE=1, the verdict in the file and the next
    # start decided by it
    foreach(_sp_mode kept refused plain selftest end)
        add_test(NAME rd_subpass_probe_${_sp_mode}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                         "-DFILE=${CMAKE_CURRENT_BINARY_DIR}/rd_subpass_probe_${_sp_mode}.txt"
                         -DMODE=${_sp_mode}
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_subpass_probe.cmake")
        set_tests_properties(rd_subpass_probe_${_sp_mode} PROPERTIES
                             SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "vk11"
                             ENVIRONMENT "${_dc_env};ICO_VK_FAKE_API=1.1;ICO_VK_FAKE_LIMITS=moltenvk;ICO_VK_FAKE_VENDOR=0x13B5;ICO_VK_SUBPASS_PROBE_CLASS=1")
    endforeach()
endif()

# rd_subpass_ab (test/rd_subpass_ab.cmake): the subpass read on against off,
# byte for byte, over the machine-local tools/vu_flat0_dumps.txt rows that
# name the mali or pixel profile and every tools/shadow_cuts_dumps.txt row
# (the pixel profile), at issue 179's Mali-G52 setting under the Arm fakes and
# at the Pixel's under the MoltenVK fakes, both on VkRenderPass objects: every
# target the same bytes, no DATE snapshot with the read on and at least as
# many passes fewer as the off run's snapshots.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/subpass_ab")
    add_test(NAME rd_subpass_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DFLAT=${CMAKE_SOURCE_DIR}/tools/vu_flat0_dumps.txt"
                     "-DCUTS=${CMAKE_SOURCE_DIR}/tools/shadow_cuts_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/subpass_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_subpass_ab.cmake")
    set_tests_properties(rd_subpass_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600
                                                  LABELS "vk11")
    if(_dc_env)
        set_tests_properties(rd_subpass_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
endif()
