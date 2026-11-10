# rd_date_subpass.cmake: DATE through the stencil bit on the Vulkan VkRenderPass
# path's subpass read (port/rhi/vk/vk_renderpass.c, RhiLimits.localReadSubpass;
# issue 179's Mali-G52 on Vulkan 1.1), included from port/render/CMakeLists.txt
# inside if(ICO_RUN_TESTS) after the tiler self-tests' failure paths (FIXT), with
# the render tests' validation environment (_env).  A file of its own:
# port/render/CMakeLists.txt is at the 256 KiB cap (tools/check_no_rom.sh).
#   rd_date_selftest_vk11          DATE's self-test (test/rd_date_selftest.cmake)
#                                  through VkRenderPass objects
#                                  (ICO_VK_FAKE_API=1.1), the stencil path
#                                  asked ([dev] date_path): stencil and snapshot
#                                  right on every sample, the device line and the
#                                  profile's words say the subpass read
#   rd_date_selftest_vk10          the same on the Vulkan 1.0 profile
#                                  (ICO_VK_FAKE_API=1.0, all-static pipelines)
#   rd_date_selftest_tiler_vk11    a tile-based profile (ICO_VK_FAKE_LIMITS=
#                                  moltenvk) on the 1.1 profile: the stencil
#                                  path is the profile's own, as on the Mali,
#                                  and the fog's local read, the profile's own
#                                  on that class with dynamic rendering, off
#                                  ("fog local off": the subpass read reads no
#                                  depth)
#   rd_date_selftest_sabotage_vk11 the same with the setup sabotaged
#                                  (ICO_RD_DATE_SABOTAGE=1): the stencil replay
#                                  fails, the profile's flag goes off
#                                  ("DATE stencil off (self-test)") and the
#                                  snapshot draws DATE for the run
#   rd_date_subpass_ab             the dumps of tools/date_subpass_dumps.txt
#                                  replayed through the bit and through the
#                                  snapshot on the 1.1 tile-based profile: every
#                                  named target byte for byte, no snapshot and
#                                  fewer passes through the bit
#                                  (test/rd_date_subpass_ab.cmake); SKIP without
#                                  the dumps
set(_ds_sub "-DEXTRA=render passes: the subpass read: a pass of one RGBA8 colour target")
set(_ds_words "-DEXTRA2=local read yes \\(the subpass read: a pass's colour with a depth target\\)")
set(_ds_env11 ${_env} "ICO_VK_FAKE_API=1.1")
set(_ds_env10 ${_env} "ICO_VK_FAKE_API=1.0" "ICO_VK_FAKE_NO_DYNAMIC=1")
set(_ds_tiler ${_ds_env11} "ICO_VK_FAKE_LIMITS=moltenvk")
add_test(NAME rd_date_selftest_vk11
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DPATH=stencil
                 "-DRESULTS=stencil ok 5, snapshot ok 5" "${_ds_sub}" "${_ds_words}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
add_test(NAME rd_date_selftest_vk10
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DPATH=stencil
                 "-DRESULTS=stencil ok 5, snapshot ok 5" "${_ds_sub}" "${_ds_words}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
add_test(NAME rd_date_selftest_tiler_vk11
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DPATH=stencil
                 "-DRESULTS=stencil ok 5, snapshot ok 5"
                 # "." for each ";" of the line (a ";" would split the argument)
                 "-DEXTRA=profile: tile-based GPU .*: blend fixed-function. fog local off. DATE stencil."
                 "${_ds_words}" -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
add_test(NAME rd_date_selftest_sabotage_vk11
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DPATH=snapshot
                 "-DRESULTS=stencil fail [1-5] at sample [0-4]: [0-9A-F]+ for [0-9A-F]+, snapshot ok 5"
                 "-DEXTRA=profile: DATE stencil off \\(self-test\\)" "${_ds_words}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
set_tests_properties(rd_date_selftest_vk11 rd_date_selftest_vk10 rd_date_selftest_tiler_vk11
                     rd_date_selftest_sabotage_vk11 PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
set_tests_properties(rd_date_selftest_vk11 PROPERTIES ENVIRONMENT
                     "${_ds_env11};ICO_RD_DATE_PATH=stencil")
set_tests_properties(rd_date_selftest_vk10 PROPERTIES ENVIRONMENT
                     "${_ds_env10};ICO_RD_DATE_PATH=stencil")
set_tests_properties(rd_date_selftest_tiler_vk11 PROPERTIES ENVIRONMENT "${_ds_tiler}")
set_tests_properties(rd_date_selftest_sabotage_vk11 PROPERTIES ENVIRONMENT
                     "${_ds_tiler};ICO_RD_DATE_SABOTAGE=1")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/date_subpass_ab")
add_test(NAME rd_date_subpass_ab
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                 "-DLIST=${CMAKE_SOURCE_DIR}/tools/date_subpass_dumps.txt"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/date_subpass_ab"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_subpass_ab.cmake")
set_tests_properties(rd_date_subpass_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600
                                                   ENVIRONMENT "${_env}")
foreach(_t rd_date_selftest_vk11 rd_date_selftest_tiler_vk11 rd_date_selftest_sabotage_vk11
           rd_date_subpass_ab)
    get_test_property(${_t} LABELS _labels)
    set_tests_properties(${_t} PROPERTIES LABELS "${_labels};vk11")
endforeach()
get_test_property(rd_date_selftest_vk10 LABELS _labels)
set_tests_properties(rd_date_selftest_vk10 PROPERTIES LABELS "${_labels};vk10")
