# port/render: the native draw API (rd.h) on the RHI. README.md lists the
# files; docs/port/RENDER_API.md is the design.
#
# ico_render needs the shader table (ico_shaders, DXC) and an RHI backend
# (ico_rhi_vk); without either it is left out with a status message, like
# the other fetched dependencies.
if(NOT TARGET ico_shaders OR NOT TARGET ico_rhi_vk)
    message(STATUS "ico: port/render not built (needs ico_shaders and ico_rhi_vk; run "
                   "tools/fetch_deps.sh)")
    return()
endif()

add_library(ico_render STATIC
    rd_core.c
    rd_dump.c
    rd_pipeline.c
    rd_png.c
    rd_post.c
    rd_present.c
    rd_replay.c)
target_include_directories(ico_render
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(ico_render PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_render PUBLIC ico_rhi_vk ico_shaders)
if(NOT WIN32)
    target_link_libraries(ico_render PUBLIC m)
endif()

# rd_replay_tool: a dump to a PNG, headless.
add_executable(rd_replay_tool tools/rd_replay_tool.c)
target_compile_options(rd_replay_tool PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_replay_tool PRIVATE ico_render)

# --- Tests ----------------------------------------------------------------------
#   rd_state  lists, state leakage, keep frames, recording of the stubbed
#             commands, dump round trip of the recording, pipeline
#             enumeration (CPU only)
#   rd_pixel  GS sprite rasterisation, reduction against a CPU reference,
#             blend_int feedback over 100 frames, dump -> load -> replay,
#             pipeline count (Vulkan; exit 77 without a device)
#   rd_replay_tool  rd_pixel's dump rendered to a PNG through the presenter
add_executable(rd_state_test test/rd_state_test.c)
target_compile_options(rd_state_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_state_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_state_test PRIVATE ico_render)

add_executable(rd_pixel_test test/rd_pixel_test.c)
target_include_directories(rd_pixel_test PRIVATE "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/shaders/test" "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_pixel_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_pixel_test PRIVATE ico_render)

if(WIN32 AND SDL3_FOUND)
    foreach(t rd_replay_tool rd_state_test rd_pixel_test)
        add_custom_command(TARGET ${t} POST_BUILD
                           COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                   "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${t}>"
                           VERBATIM)
    endforeach()
endif()

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME rd_state COMMAND rd_state_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME rd_pixel COMMAND rd_pixel_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rd_pixel PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP rd_dump)
    # the dump rd_pixel leaves behind, through the tool
    add_test(NAME rd_replay_tool
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool.png" --present 640x480)
    set_tests_properties(rd_replay_tool PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED rd_dump)
    set(_env "")
    set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
    if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
        set(_env "VK_ADD_LAYER_PATH=${_ico_vvl}" "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                 "ICO_VK_VALIDATION=1")
    endif()
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    if(_env)
        set_tests_properties(rd_pixel rd_replay_tool PROPERTIES ENVIRONMENT "${_env}")
    endif()
endif()
