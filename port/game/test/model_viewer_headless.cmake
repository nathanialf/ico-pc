# port/game/test/model_viewer_headless.cmake: the model_viewer_headless test
# (package MV, docs/port/TESTING.md "The model viewer run"), run as
#   cmake -DICO_PC=<headless ico_pc> -DISO=<disc image> -DPAD=<pad script>
#         -DDIR=<work folder> -P model_viewer_headless.cmake
# The headless program is copied into an empty folder (its ico-pc.ini, logs
# and card folder are beside it) and driven by model_viewer_pad.txt from the
# boot through Settings > Extras > Models to the first model, an animation
# played, the list and the title again.  The log must show the host stage
# loaded with the model, the animation past its first frame, the title back,
# the run to its last tick, and no "model_viewer: failed".  Without the
# disc image the test is skipped.

if(NOT EXISTS "${ISO}")
    message("model_viewer_headless: skipped (no disc image at ${ISO})")
    return()
endif()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
file(COPY "${ICO_PC}" DESTINATION "${DIR}")
get_filename_component(_exe "${ICO_PC}" NAME)
file(WRITE "${DIR}/ico-pc.ini"
     "iso=${ISO}\nverify=0\nuse_iso=1\naudio=0\nticks=1300\nwatchdog=120\ntrace=0\n"
     "pad_script=${PAD}\n")
execute_process(COMMAND "${DIR}/${_exe}" WORKING_DIRECTORY "${DIR}" TIMEOUT 280
                RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
set(_log "${DIR}/logs/ico-pc.log")
if(NOT EXISTS "${_log}")
    message(FATAL_ERROR "model_viewer_headless: no log (exit ${_rc})")
endif()
file(READ "${_log}" _text)

set(_bad "")
string(REGEX MATCH "model_viewer: stage [0-9]+ loaded id [0-9]+ \"[^\"\n]+\"" _loaded "${_text}")
if(NOT _loaded)
    string(APPEND _bad "\n  no \"model_viewer: stage S loaded id N \\\"name\\\"\" line")
endif()
string(REGEX MATCH "model_viewer: motion \"[^\"\n]+\" frame [1-9][0-9]*/[0-9]+" _played "${_text}")
if(NOT _played)
    string(APPEND _bad "\n  no \"model_viewer: motion \\\"name\\\" frame F/N\" line with F > 0")
endif()
if(NOT _text MATCHES "model_viewer: the title is back")
    string(APPEND _bad "\n  the title did not come back")
endif()
if(_text MATCHES "model_viewer: failed")
    string(REGEX MATCH "model_viewer: failed[^\n]*" _failed "${_text}")
    string(APPEND _bad "\n  ${_failed}")
endif()
if(NOT _text MATCHES "exit: ticks= reached")
    string(APPEND _bad "\n  the run did not reach its last tick (exit ${_rc})")
endif()
if(_bad)
    message(FATAL_ERROR "model_viewer_headless: ${_bad}\n(log: ${_log})")
endif()
message("model_viewer_headless: ${_loaded}; ${_played}; the title is back")
