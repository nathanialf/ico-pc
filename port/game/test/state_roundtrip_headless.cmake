# port/game/test/state_roundtrip_headless.cmake: the save states' tests in
# the headless program, run as
#   cmake -DICO_PC=<headless ico_pc> -DISO=<disc image> -DPAD=<pad script>
#         -DDIR=<work folder> -DMODE=<roundtrip|settings> [-DSANITIZE=<list>]
#         [-DPYTHON=<python3> -DTOOL=<tools/state_region_diff.py> -DMAP=<ico_pc.map>]
#         -P state_roundtrip_headless.cmake
# Each run copies the headless program into an empty folder (its
# ico-pc.ini, config.toml, logs and states folder beside it), starts it in
# stage 22 ([dev] start_stage) and drives it with state_roundtrip_pad.txt
# (the ticks below are that file's).
#
# MODE roundtrip (state_roundtrip_headless): Start at tick 400 opens the
# pause menu; [dev] state_save_at=440:1 saves slot 1 there; the script
# closes the menu and plays to tick 700, where [dev] state_hash_at=700 logs
# the hash of the arena and the game's data and bss and state_load_at=700:1
# loads slot 1. The load puts the Main tick and the script's cursor back to
# 440 (both are CLOCK blocks of the state), so the same presses play again
# and the hash is logged a second time at 700. The hash is of the game's
# state: the host's pointers and handles in it are left out (the "state:
# hash masked N pointer words, M host ids" line; docs/port/PLATFORM.md
# "Save states", the hash rule). Passes when the log shows save states on,
# the [dev] line, the save, the load (saved at tick 440), exactly two
# "state: hash" lines, one before the load and one after it, with the same
# hash and tick, no "state: refused" and the run to its last tick.
# [dev] state_dump_at=700 also writes the three regions at each hash
# (states/<disc>/dump-700-1 before the load, dump-700-2 after it): when the
# hashes differ, tools/state_region_diff.py (PYTHON, TOOL, MAP given)
# compares them and the failure message lists the first ten differing
# symbols of the game's data and bss and the arena's first ten differing
# ranges. The dumps are deleted when the test passes.
#
# MODE settings (state_roundtrip_headless_settings): a first run saves
# slot 1 at 440 as above and stops at 460; its states folder is copied
# into a second run's folder whose config.toml also sets
# [gameplay] yorda_safe = true, a simulation key (port/platform/sim_keys.c),
# and that run loads slot 1 at tick 600. Passes when the second log shows
# "state: refused load slot 1: settings differ: gameplay.yorda_safe=...",
# no "state: loaded" and the run to its last tick (the game went on).
#
# Skipped without the disc image and on a sanitizer build (save states are
# off there by design, port/game/state_host.c).

set(_name "state_roundtrip_headless")
if(MODE STREQUAL "settings")
    set(_name "state_roundtrip_headless_settings")
elseif(NOT MODE STREQUAL "roundtrip")
    message(FATAL_ERROR "${_name}: MODE is roundtrip or settings, not \"${MODE}\"")
endif()
if(SANITIZE)
    message("${_name}: skipped (a sanitizer build, -fsanitize=${SANITIZE}: save states are off)")
    return()
endif()
if(NOT EXISTS "${ISO}")
    message("${_name}: skipped (no disc image at ${ISO})")
    return()
endif()

# the pad script's ticks (state_roundtrip_pad.txt)
set(_stage 22)
set(_save 440)
set(_end 700)

# one run of the headless program in <dir>: the ini's extra lines, the
# config.toml text and the tick limit; the log's text into <out>
function(state_run dir extra config ticks out)
    file(MAKE_DIRECTORY "${dir}")
    file(COPY "${ICO_PC}" DESTINATION "${dir}")
    get_filename_component(_exe "${ICO_PC}" NAME)
    file(WRITE "${dir}/config.toml" "${config}")
    file(WRITE "${dir}/ico-pc.ini"
         "iso=${ISO}\nverify=0\nuse_iso=1\naudio=0\nticks=${ticks}\nwatchdog=120\ntrace=0\n"
         "start_stage=${_stage}\npad_script=${PAD}\n${extra}")
    execute_process(COMMAND "${dir}/${_exe}" WORKING_DIRECTORY "${dir}" TIMEOUT 130
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT EXISTS "${dir}/logs/ico-pc.log")
        message(FATAL_ERROR "${_name}: no log in ${dir} (exit ${_rc})")
    endif()
    file(READ "${dir}/logs/ico-pc.log" _text)
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

# what both modes need of a log: save states on, and the run to its end
function(state_common text log bad)
    set(_b "${${bad}}")
    if(text MATCHES "state: save states are off: ([^\n]*)")
        string(APPEND _b "\n  save states are off: ${CMAKE_MATCH_1}")
    elseif(NOT text MATCHES "state: save states on: ")
        string(APPEND _b "\n  no \"state: save states on\" line")
    endif()
    if(text MATCHES "state: the game's data ranges are not bracketed[^\n]*")
        string(APPEND _b "\n  ${CMAKE_MATCH_0}")
    endif()
    if(text MATCHES "state: \\[dev\\] [a-z_]+ = \"[^\n]*; ignored")
        string(APPEND _b "\n  ${CMAKE_MATCH_0}")
    endif()
    if(NOT text MATCHES "exit: ticks= reached")
        string(APPEND _b "\n  the run did not reach its last tick (${log})")
    endif()
    set(${bad} "${_b}" PARENT_SCOPE)
endfunction()

# The two dumps of the hash's tick compared (tools/state_region_diff.py):
# its report appended to <bad>, or why there is none
function(state_diff bad)
    set(_b "${${bad}}")
    file(GLOB _d1 "${DIR}/states/*/dump-${_end}-1")
    file(GLOB _d2 "${DIR}/states/*/dump-${_end}-2")
    if(NOT _d1 OR NOT _d2)
        string(APPEND _b "\n  no dumps of tick ${_end} to compare"
               " (states/<disc>/dump-${_end}-1 and -2)")
    elseif(NOT PYTHON OR NOT TOOL OR NOT MAP OR NOT EXISTS "${MAP}")
        string(APPEND _b "\n  the dumps ${_d1} and ${_d2} are kept: compare them with"
               " tools/state_region_diff.py <a> <b> <the build's ico_pc.map>"
               " (no Python or no map here)")
    else()
        get_filename_component(_exe "${ICO_PC}" NAME)
        execute_process(COMMAND "${PYTHON}" -I "${TOOL}" "${_d1}" "${_d2}" "${MAP}"
                                --elf "${DIR}/${_exe}" --max 10
                        OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
        string(STRIP "${_out}${_err}" _out)
        string(APPEND _b "\n  ${_out}\n  (the dumps: ${_d1}, ${_d2})")
    endif()
    set(${bad} "${_b}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${DIR}")
set(_config "[video]\nvideo_mode = \"60hz\"\n")
set(_bad "")

if(MODE STREQUAL "roundtrip")
    set(_dev "state_save_at=${_save}:1\nstate_load_at=${_end}:1\n")
    string(APPEND _dev "state_hash_at=${_end}\nstate_dump_at=${_end}\n")
    state_run("${DIR}" "${_dev}" "${_config}" 760 _text)
    set(_log "${DIR}/logs/ico-pc.log")
    state_common("${_text}" "${_log}" _bad)
    if(NOT _text MATCHES
       "state: \\[dev\\] save at Main tick ${_save} slot 1, load at ${_end} slot 1, hash at ${_end}")
        string(APPEND _bad "\n  no \"state: [dev] save at Main tick ${_save} ...\" line")
    endif()
    string(REGEX MATCH "state: saved slot 1 \\([^\n]*" _saved "${_text}")
    if(NOT _saved)
        string(APPEND _bad "\n  no \"state: saved slot 1\" line")
    endif()
    string(REGEX MATCH "state: loaded slot 1 \\(saved at Main tick ([0-9]+)[^\n]*" _loaded "${_text}")
    if(NOT _loaded)
        string(APPEND _bad "\n  no \"state: loaded slot 1\" line")
    elseif(NOT CMAKE_MATCH_1 EQUAL _save)
        string(APPEND _bad "\n  the state was saved at Main tick ${CMAKE_MATCH_1}, not ${_save}")
    endif()
    string(REGEX MATCHALL "state: refused [^\n]*" _refused "${_text}")
    foreach(_r IN LISTS _refused)
        string(APPEND _bad "\n  ${_r}")
    endforeach()
    string(REGEX MATCHALL "state: hash [0-9a-f]+ of arena\\+gdata\\+gbss at tick [0-9]+" _hashes
           "${_text}")
    list(LENGTH _hashes _n)
    if(NOT _n EQUAL 2)
        string(APPEND _bad "\n  ${_n} \"state: hash\" lines, not 2: ${_hashes}")
    else()
        list(GET _hashes 0 _h1)
        list(GET _hashes 1 _h2)
        string(FIND "${_text}" "${_h1}" _at1)
        string(FIND "${_text}" "${_h2}" _at2 REVERSE)
        string(FIND "${_text}" "state: loaded slot 1 " _atl)
        if(NOT _h1 STREQUAL _h2)
            string(APPEND _bad "\n  the hashes differ: \"${_h1}\" before the load, \"${_h2}\" after")
            string(REGEX MATCHALL "state: hash masked [^\n]*" _masked "${_text}")
            foreach(_m IN LISTS _masked)
                string(APPEND _bad "\n  ${_m}")
            endforeach()
            state_diff(_bad)
        endif()
        if(_loaded AND NOT (_at1 LESS _atl AND _atl LESS _at2))
            string(APPEND _bad "\n  the hash lines are not one before the load and one after it")
        endif()
        if(NOT _h1 MATCHES " at tick ${_end}$")
            string(APPEND _bad "\n  the hash was taken at another tick: ${_h1}")
        endif()
    endif()
    if(_bad)
        message(FATAL_ERROR "${_name}: ${_bad}\n(log: ${_log})")
    endif()
    string(REGEX MATCH "state: hash masked [^\n]*" _masked "${_text}")
    file(GLOB _dumps "${DIR}/states/*/dump-*")
    if(_dumps)
        file(REMOVE_RECURSE ${_dumps})
    endif()
    message("${_name}: ${_saved}\n${_loaded}\n${_h1} (twice)\n${_masked}")
else()
    # the save: the first run stops 20 ticks after it
    math(EXPR _stop "${_save} + 20")
    state_run("${DIR}/save" "state_save_at=${_save}:1\n" "${_config}" ${_stop} _text1)
    set(_log1 "${DIR}/save/logs/ico-pc.log")
    state_common("${_text1}" "${_log1}" _bad)
    string(REGEX MATCH "state: saved slot 1 \\([^\n]*" _saved "${_text1}")
    if(NOT _saved)
        string(REGEX MATCH "state: refused save slot 1[^\n]*" _r "${_text1}")
        string(APPEND _bad "\n  the first run saved nothing ${_r} (${_log1})")
    endif()
    if(_bad)
        message(FATAL_ERROR "${_name}: ${_bad}")
    endif()
    # the load: the same folder of states, one simulation key changed
    file(MAKE_DIRECTORY "${DIR}/load")
    file(COPY "${DIR}/save/states" DESTINATION "${DIR}/load")
    state_run("${DIR}/load" "state_load_at=600:1\n" "${_config}[gameplay]\nyorda_safe = true\n"
              660 _text2)
    set(_log2 "${DIR}/load/logs/ico-pc.log")
    state_common("${_text2}" "${_log2}" _bad)
    string(REGEX MATCH "state: refused load slot 1: settings differ: gameplay\\.yorda_safe=[^\n]*"
           _refused "${_text2}")
    if(NOT _refused)
        string(REGEX MATCH "state: refused load slot 1[^\n]*" _r "${_text2}")
        string(APPEND _bad "\n  no \"state: refused load slot 1: settings differ:"
               " gameplay.yorda_safe=...\" line ${_r}")
    endif()
    if(_text2 MATCHES "state: loaded slot")
        string(APPEND _bad "\n  the second run loaded the state")
    endif()
    if(_bad)
        message(FATAL_ERROR "${_name}: ${_bad}\n(logs: ${_log1}, ${_log2})")
    endif()
    message("${_name}: ${_saved}\n${_refused}; the run went on to its last tick")
endif()
