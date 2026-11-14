# rd_vu_audit.cmake: the VU record path's bounds audit (rd_vu_audit.c,
# rd_vu_audit.h; issue 179), included at the end of port/render/CMakeLists.txt
# (a file of its own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_vu_audit.c)

# rd_vu_audit (test/rd_vu_audit.cmake): issue 179's six Mali-G52 dumps
# (the two v0.11.6 loss dumps, the four v0.11.5 captures) under the Arm
# fakes at the reporter's settings, ICO_RD_VU_AUDIT=1, the compaction on
# (merged and not) and off: 0 violations in every run, the compacted lists
# read back and their draws audited where it is on.  No dump folder, or no
# device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_audit")
    add_test(NAME rd_vu_audit
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_audit"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_audit.cmake")
    set_tests_properties(rd_vu_audit PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(ICO_SANITIZE MATCHES "address")
        set_property(TEST rd_vu_audit APPEND PROPERTY ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    endif()
endif()
