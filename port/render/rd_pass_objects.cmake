# rd_pass_objects.cmake: the renderer's half of Qualcomm's driver row
# (port/rhi/rhi_pass_row.h rhi_vk_pass_row_qualcomm, issue 123: every render
# pass a VkRenderPass object on Qualcomm's own driver), included at the end of
# port/render/CMakeLists.txt (a file of its own: port/render/CMakeLists.txt is
# near the 256 KiB cap, tools/check_no_rom.sh).  The row itself is tested in
# port/rhi (rhi_vk_pass_row, rhi_vk_qcom_passes, rhi_vk_qcom_passes_off).
#
# rd_pass_objects_ab (test/rd_pass_objects_ab.cmake): every dump of the
# machine-local tools/pass_objects_dumps.txt (stage 12 on the issue 123
# tablet, the Pixel's opening, issue 179's four Mali-G52 dumps; a listed dump
# that is absent fails where /primary/dev/ico-dumps exists) replayed with
# dynamic rendering (ICO_VK_LEGACY_PASSES=0) against lavapipe put on the row
# (ICO_VK_FAKE_PASS_ROW=qualcomm), at the 123 tablet's setting under a
# Qualcomm phone's tile rule and at the 179 phone's under the Arm fakes:
# every target byte for byte, the same draws and passes, each run's device
# line naming its path.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pass_objects_ab")
    add_test(NAME rd_pass_objects_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/pass_objects_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/pass_objects_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_pass_objects_ab.cmake")
    set_tests_properties(rd_pass_objects_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 7200)
    if(_dc_env)
        set_tests_properties(rd_pass_objects_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
endif()
