# port/audio: the software SPU2 (Phase 4A, docs/port/AUDIO.md). Integer-only
# code with no platform dependencies; Phase 4B adds the sndn2 host and the
# SDL output on top.
add_library(ico_audio STATIC
    adpcm.c
    spu2.c
    spu2_sd.c
    spu2_tables.c)
target_include_directories(ico_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ico_audio PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

add_executable(adpcm_test test/adpcm_test.c)
target_compile_options(adpcm_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(adpcm_test PRIVATE ico_audio)

# spu2_test's encoder and frequency checks use libm (test-only; the checksum
# scene is integer-only).
add_executable(spu2_test test/spu2_test.c)
target_compile_options(spu2_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(spu2_test PRIVATE ico_audio)
if(NOT WIN32)
    target_link_libraries(spu2_test PRIVATE m)
endif()

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME adpcm COMMAND adpcm_test)
    add_test(NAME spu2 COMMAND spu2_test)
endif()
