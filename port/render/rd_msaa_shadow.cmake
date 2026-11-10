# rd_msaa_shadow.cmake: the shadow count drawn in the MSAA epoch
# (rd_msaa_shadow.c, issue 209; docs/port/MSAA.md "The shadow count in the
# epoch"), included from port/render/CMakeLists.txt after ico_render and
# rd_msaa_test (a file of its own: port/render/CMakeLists.txt is at the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_msaa_shadow.c)
# rd_msaa_test reads rd_replay.h (rd_msaa_shadow.c's calls), which needs shader_consts.h
target_include_directories(rd_msaa_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")

# rd_msaa under the profiles of issue 209's phone, on lavapipe, under the
# validation layer with its synchronisation checks where the toolchain has
# it (the render tests' environment, built here as port/render/CMakeLists.txt
# builds _env further down):
#   rd_msaa_tiler   the reporter's profile bits on the desktop class: the exact
#                   blend through the attachment read with GS state as shader
#                   data (ICO_RD_BLEND=attachment, ICO_RD_UBER=on), the fog's
#                   depth through a copy, DATE through the stencil, the
#                   volumes asked into the scene's pass, the passes cut
#                   (clear carry, clear skip, last store, pass area), the store
#                   plan's garbage where it leaves a depth undefined
#                   (ICO_RD_STORE_POISON=1), every state in the pipeline keys
#                   (ICO_VK_FAKE_NO_DYNAMIC=1, the tile-based default)
#   rd_msaa_mvk     the tile-based class itself (ICO_VK_FAKE_LIMITS=moltenvk:
#                   its fog local read, DATE stencil, pass area and cuts by the
#                   profile), every state in the keys, the store poison
#   rd_msaa_shadow_sabotage  the epoch's count sabotaged
#                   (ICO_RD_MSAA_SHADOW_SABOTAGE=1: its stencil cleared to 1):
#                   its self-test must find it and choose the count after the
#                   epoch's resolve, which must still be right
# Each: rd_msaa_test's cases, the count in the epoch (testShadowEpoch: the
# self-test's verdict, the volumes drawn multisampled, MSAA4's count the
# count without anti-aliasing away from edges).  77 without a device that
# takes 4 samples.
if(ICO_RUN_TESTS)
    set(_mss_env "")
    if(NOT WIN32 AND EXISTS "${ICO_DEPS_DIR}/vulkan-validation/VkLayer_khronos_validation.json")
        set(_mss_env "VK_ADD_LAYER_PATH=${ICO_DEPS_DIR}/vulkan-validation"
                     "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                     "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true"
                     "ICO_VK_VALIDATION=1")
    endif()
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _mss_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    set(_mss_tiler ${_mss_env} "ICO_RD_BLEND=attachment" "ICO_RD_UBER=on" "ICO_RD_FOG_PATH=copy"
                   "ICO_RD_DATE_PATH=stencil" "ICO_RD_SHADOW_MERGE=1" "ICO_RD_CLEAR_CARRY=on"
                   "ICO_RD_CLEAR_SKIP=on" "ICO_RD_LAST_STORE=on" "ICO_RD_PASS_AREA=on"
                   "ICO_RD_STORE_POISON=1" "ICO_VK_FAKE_NO_DYNAMIC=1")
    set(_mss_mvk ${_mss_env} "ICO_VK_FAKE_LIMITS=moltenvk" "ICO_VK_FAKE_NO_DYNAMIC=1"
                 "ICO_RD_STORE_POISON=1")
    set(_mss_sab ${_mss_env} "ICO_RD_MSAA_SHADOW_SABOTAGE=1")
    add_test(NAME rd_msaa_tiler COMMAND rd_msaa_test)
    add_test(NAME rd_msaa_mvk COMMAND rd_msaa_test)
    add_test(NAME rd_msaa_shadow_sabotage COMMAND rd_msaa_test)
    set_tests_properties(rd_msaa_tiler PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${_mss_tiler}")
    set_tests_properties(rd_msaa_mvk PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${_mss_mvk}")
    set_tests_properties(rd_msaa_shadow_sabotage PROPERTIES SKIP_RETURN_CODE 77
                                                            ENVIRONMENT "${_mss_sab}")
endif()
