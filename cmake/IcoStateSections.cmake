# cmake/IcoStateSections.cmake: the game's writable globals in sections of
# their own, so save states can copy and restore exactly the game's .data and
# .bss (port/platform/game_data_range.h; docs/port/PLATFORM.md, "Game data
# ranges"; docs/BUILDING.md, "Game data sections").
#
# ico_state_sections(<archive> [<compile target>...])
#   <archive>          the STATIC library the game's objects end up in
#   <compile target>   the targets that compile those objects (the OBJECT
#                      libraries of ico_game); default: the archive itself
#
# GCC (host Linux, mingw-w64): the objects are compiled -fno-data-sections
# (one .data and one .bss per object, whatever the toolchain's defaults) and
# the archive's members are renamed after it is made, by the toolchain's own
# objcopy (GNU objcopy rewrites every member of an archive and keeps its
# index; renaming a section that is not there does nothing, so running it
# twice is harmless):
#   ELF  .data, .data.rel, .data.rel.local -> ico_gdata; .bss -> ico_gbss
#        (.data.rel.ro* are constant tables made writable only while the
#        loader relocates them: they keep their names and stay out)
#   PE   .data -> .data$ico_m; .bss -> .data$ico_p with contents (GNU ld's
#        PE script sorts .data$* into .data but has no .bss$* rule: an orphan
#        .bss$ section does not stay between the brackets. The game's 5.5 MB of
#        zero-filled data is stored in the .exe as a result.)
# clang (linux-x64-clang, the NDK, llvm-mingw, Apple): the same names by
# `#pragma clang section`, forced into every source of the compile targets
# (port/platform/game_data_sections.h). llvm-objcopy cannot rename COFF
# sections and ld64 has no objcopy at all, so clang never needs one. On
# Windows the compile adds -fno-zero-initialized-in-bss: lld moves an
# uninitialised .data$/.bss$ section into an output section of its own, so
# the game's zero-filled data goes into .data$ico_m with the rest.

set(ICO_GAME_DATA_SECTIONS_H "${CMAKE_SOURCE_DIR}/port/platform/game_data_sections.h")

function(ico_state_sections archive)
    set(_targets ${ARGN})
    if(NOT _targets)
        set(_targets ${archive})
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        foreach(_t IN LISTS _targets)
            target_compile_options(${_t} PRIVATE "SHELL:-include \"${ICO_GAME_DATA_SECTIONS_H}\"")
            if(WIN32)
                target_compile_options(${_t} PRIVATE -fno-zero-initialized-in-bss)
            endif()
        endforeach()
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT APPLE)
        if(NOT CMAKE_OBJCOPY)
            message(FATAL_ERROR "ico_state_sections: no objcopy for this toolchain (CMAKE_OBJCOPY): "
                                "the game's data sections cannot be renamed")
        endif()
        foreach(_t IN LISTS _targets)
            target_compile_options(${_t} PRIVATE -fno-data-sections)
        endforeach()
        if(WIN32)
            set(_rename
                --rename-section ".data=.data$ico_m"
                --rename-section ".bss=.data$ico_p,alloc,load,contents,data")
        else()
            set(_rename
                --rename-section .data=ico_gdata
                --rename-section .data.rel=ico_gdata
                --rename-section .data.rel.local=ico_gdata
                --rename-section .bss=ico_gbss)
        endif()
        add_custom_command(TARGET ${archive} POST_BUILD
            COMMAND "${CMAKE_OBJCOPY}" ${_rename} "$<TARGET_FILE:${archive}>"
            COMMENT "Game data sections in ${archive}"
            VERBATIM)
    else()
        message(FATAL_ERROR "ico_state_sections: no way to gather the game's data sections with "
                            "${CMAKE_C_COMPILER_ID} (GCC and clang are the port's compilers)")
    endif()
endfunction()
