# cmake/IcoTests.cmake: the test programs are not part of `all`.
#
#   cmake --build <dir> --target all tests     (what CI builds)
#   cmake --build <dir> --target tests         (every test program, no game)
#   cmake --build <dir> --target <name>_test   (one of them)
#
# `cmake --build <dir>` alone builds the game and the tools only. ctest on a
# tree whose test programs are not built reports every test as "Not Run"
# (the program is missing) and fails; nothing passes silently.
#
# Two pieces, so that a test added later is covered without touching this file:
#
#  * add_test() is wrapped (include this file BEFORE the first add_test): it
#    gives each test a LABELS entry by the directory it is registered in, so
#    `ctest -L render` runs one area, and records the executable targets the
#    test command names (the program itself, or $<TARGET_FILE:x> in an argument).
#      render   port/render                 ui        port/ui
#      platform port/platform, port/input, port/config, port/core, the top level
#      audio    port/audio                  game      port/game, port/math, port/null, port/test
#      data     port/data, port/fmv         rhi       port/rhi, port/shaders
#      save     port/save
#      tools    added to any test whose command runs a script from tools/
#
#  * ico_finalize_tests() (called deferred at the end of the top directory, after the
#    targets that other directories create in deferred calls exist) marks every
#    executable target that is a test program (a name ending in _test, or one a
#    test command names) EXCLUDE_FROM_ALL and makes the aggregate target `tests`
#    depend on all of them. ICO_KEEP_IN_ALL lists the programs that are not tests
#    and stay in `all` (the save importer is shipped by the Windows packages, ico_pc
#    is the game, the replay tool is a developer tool the packages do not ship);
#    `tests` still depends on them because tests run them.

set(ICO_KEEP_IN_ALL rd_replay_tool mc_import ico_pc)
set_property(GLOBAL PROPERTY ICO_TEST_PROGRAMS "")

# The area of the directory <dir>.
function(_ico_test_area out dir)
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${dir}")
    set(_area platform)
    if(_rel MATCHES "^port/(render|ui|audio|game|data|rhi|save)(/|$)")
        set(_area ${CMAKE_MATCH_1})
    elseif(_rel MATCHES "^port/(math|null|test)(/|$)")
        set(_area game)
    elseif(_rel MATCHES "^port/fmv(/|$)")
        set(_area data)
    elseif(_rel MATCHES "^port/shaders(/|$)")
        set(_area rhi)
    elseif(_rel MATCHES "^tools(/|$)")
        set(_area tools)
    endif()
    set(${out} ${_area} PARENT_SCOPE)
endfunction()

function(add_test)
    # forward every argument exactly as given (an empty or ;-holding argument
    # survives a bracket argument; _add_test(${ARGV}) would drop or split it)
    set(_code "_add_test(")
    math(EXPR _last "${ARGC} - 1")
    foreach(_i RANGE ${_last})
        string(APPEND _code " [==[${ARGV${_i}}]==]")
    endforeach()
    cmake_language(EVAL CODE "${_code})")
    if(NOT ARGV0 STREQUAL "NAME")
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 _t "" "WORKING_DIRECTORY" "COMMAND;CONFIGURATIONS")
    if(ARGC LESS 2 OR NOT _t_COMMAND)
        return()
    endif()
    # the command and working directory kept for a test registered again
    # elsewhere (port/render/CMakeLists.txt ico_rd_gl_twin): ctest has no
    # COMMAND test property to read them back from
    set_property(GLOBAL PROPERTY "ICO_TEST_COMMAND_${ARGV1}" "${_t_COMMAND}")
    set_property(GLOBAL PROPERTY "ICO_TEST_WORKING_DIRECTORY_${ARGV1}" "${_t_WORKING_DIRECTORY}")
    set(_progs "")
    list(GET _t_COMMAND 0 _first)
    if(TARGET "${_first}")
        list(APPEND _progs "${_first}")
    endif()
    set(_labels "")
    foreach(_a IN LISTS _t_COMMAND)
        if(_a MATCHES "^\\$<TARGET_FILE:([^>]+)>$" OR _a MATCHES "=\\$<TARGET_FILE:([^>]+)>$")
            list(APPEND _progs "${CMAKE_MATCH_1}")
        endif()
        if(_a MATCHES "^${CMAKE_SOURCE_DIR}/tools/")
            list(APPEND _labels tools)
        endif()
    endforeach()
    # A test registered from a deferred call (cmake_language(DEFER)) runs in the
    # top directory: its area is then the directory of its program's first source.
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    if(_dir STREQUAL CMAKE_SOURCE_DIR AND _progs)
        list(GET _progs 0 _p0)
        get_target_property(_srcs ${_p0} SOURCES)
        list(GET _srcs 0 _src0)
        get_filename_component(_src0 "${_src0}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        get_filename_component(_dir "${_src0}" DIRECTORY)
    endif()
    _ico_test_area(_area "${_dir}")
    list(PREPEND _labels ${_area})
    list(REMOVE_DUPLICATES _labels)
    set_tests_properties(${ARGV1} PROPERTIES LABELS "${_labels}")
    set_property(GLOBAL APPEND PROPERTY ICO_TEST_PROGRAMS ${_progs})
endfunction()

function(_ico_collect_executables dir out)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    set(_list "")
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            list(APPEND _list ${_t})
        endif()
    endforeach()
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_d IN LISTS _subdirs)
        _ico_collect_executables("${_d}" _sub)
        list(APPEND _list ${_sub})
    endforeach()
    set(${out} ${_list} PARENT_SCOPE)
endfunction()

function(ico_finalize_tests)
    get_property(_named GLOBAL PROPERTY ICO_TEST_PROGRAMS)
    _ico_collect_executables("${CMAKE_SOURCE_DIR}" _exes)
    add_custom_target(tests COMMENT "Every test program")
    foreach(_t IN LISTS _exes)
        if(_t IN_LIST ICO_KEEP_IN_ALL)
            continue()
        endif()
        if(_t MATCHES "_test$" OR _t IN_LIST _named)
            set_target_properties(${_t} PROPERTIES EXCLUDE_FROM_ALL ON)
            add_dependencies(tests ${_t})
        endif()
    endforeach()
    foreach(_t IN LISTS _named)
        if(_t IN_LIST ICO_KEEP_IN_ALL AND NOT _t STREQUAL "ico_pc" AND TARGET ${_t})
            add_dependencies(tests ${_t})
        endif()
    endforeach()
endfunction()

# ico_add_sharded_test(<name> <shards> PROGRAM <target> [ARGS <args>...]
#                      [WORKING_DIRECTORY <dir>] [PROPERTIES <prop> <value>...])
#
# A slow test program that takes `--shard k/N` and `--list-cases`
# (port/test/ico_shard.h) registered as <name>.1of<N> ... <name>.<N>of<N>, which
# together run every case once, so ctest -j runs them side by side. Each shard
# gets the PROPERTIES and the area LABELS of an ordinary test. <name>.cases (cheap,
# no device) checks that the program's cases are split over the shards without a
# case missing or in two: cmake/IcoShardCheck.cmake. The shards' names are left in
# <name>_SHARDS of the caller. "@SHARD@" in ARGS is the shard's number (a folder of its
# own to write in).
function(ico_add_sharded_test name shards)
    cmake_parse_arguments(PARSE_ARGV 2 _s "" "PROGRAM;WORKING_DIRECTORY" "ARGS;PROPERTIES")
    set(_wd "")
    if(_s_WORKING_DIRECTORY)
        set(_wd WORKING_DIRECTORY "${_s_WORKING_DIRECTORY}")
    endif()
    set(_names "")
    foreach(_k RANGE 1 ${shards})
        set(_n ${name}.${_k}of${shards})
        list(APPEND _names ${_n})
        string(REPLACE "@SHARD@" "${_k}" _args "${_s_ARGS}")
        add_test(NAME ${_n} COMMAND ${_s_PROGRAM} ${_args} --shard ${_k}/${shards} ${_wd})
        if(_s_PROPERTIES)
            set_tests_properties(${_n} PROPERTIES ${_s_PROPERTIES})
        endif()
    endforeach()
    set(${name}_SHARDS ${_names} PARENT_SCOPE) # for set_tests_properties on all shards
    add_test(NAME ${name}.cases
             COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:${_s_PROGRAM}>" -DSHARDS=${shards}
                     "-DARGS=${_s_ARGS}" -P "${CMAKE_SOURCE_DIR}/cmake/IcoShardCheck.cmake")
endfunction()
