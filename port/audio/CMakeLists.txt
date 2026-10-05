# port/audio: the software SPU2 (Phase 4A, docs/port/AUDIO.md). Integer-only
# code with no platform dependencies; Phase 4B adds the sndn2 host and the
# SDL output on top (ico_sndn2, below).
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

# --- Phase 4B: the Sg sequencer, the SNDN2DRV host and the output ------------
# sg/sound.c is the EE sequencer the game calls (moved from sce/libsndn2/,
# whose path keeps a one-line include of it for the PS2 build); it is
# decomp-shaped game code, so it takes the game's warning set and ICO_HOST.
# sndn2_host.c and stream.c are the IOP driver behind its RPC; audio_host.c
# renders the SPU2 once per vsync into the sinks (wav.c, and out_sdl.c in
# the window build when SDL3 is there).
add_library(ico_sndn2 STATIC
    sg/sound.c
    sndn2_host.c
    stream.c
    audio_host.c
    wav.c)
target_include_directories(ico_sndn2
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sg" "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(ico_sndn2 PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
set_source_files_properties(sg/sound.c PROPERTIES
    COMPILE_DEFINITIONS "ICO_HOST=1"
    COMPILE_OPTIONS "${ICO_GAME_WARNINGS};-Wno-sign-compare;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-pointer-sign")
target_link_libraries(ico_sndn2 PUBLIC ico_audio ico_port_data ico_platform)
if(NOT WIN32)
    target_link_libraries(ico_sndn2 PUBLIC m) # exp2 for the pitch table fallback
endif()
if(NOT ICO_HEADLESS AND TARGET SDL3::SDL3-shared)
    target_sources(ico_sndn2 PRIVATE out_sdl.c)
    target_compile_definitions(ico_sndn2 PRIVATE ICO_AUDIO_SDL=1)
    target_link_libraries(ico_sndn2 PUBLIC SDL3::SDL3-shared)
endif()
if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_sndn2)
endif()

add_executable(sndn2_test test/sndn2_test.c)
target_include_directories(sndn2_test PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(sndn2_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(sndn2_test PRIVATE ico_sndn2)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    # the disc's pitch table is checked when the image is there (else skipped)
    set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
        "The PAL disc image (SCES-50760) the disc tests read, never committed")
    add_test(NAME sndn2 COMMAND sndn2_test "${ICO_DISC_IMAGE}")
endif()
