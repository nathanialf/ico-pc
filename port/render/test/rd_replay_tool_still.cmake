# rd_replay_tool_still: the still re-present (rd_reshow.c) against rendering
# every present.  A dump is its own tick before (--interp-any): nothing moves,
# so the presents of the tick after the first have the first's frame.
# rd_replay_tool --presents N writes N pictures; run with the switch on (the
# default) and with ICO_RD_STILL_REPRESENT=0, every picture must be the same
# bytes, the default run must say "shown again" for presents 1..N-1, and the
# run with the switch off must say it for none.
# -DTOOL=<rd_replay_tool> -DDUMP=<dump> -DOUT=<dir> -DARGS=<a|b|...>
# -DPRESENTS=<N>
if(NOT EXISTS "${DUMP}")
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: ${DUMP}: no dump (build-host/tmp is not in the tree)")
  return()
endif()
string(REPLACE "|" ";" ARGS "${ARGS}")
file(MAKE_DIRECTORY "${OUT}/on" "${OUT}/off")
foreach(_mode on off)
  set(_env "")
  if(_mode STREQUAL "off")
    set(_env ${CMAKE_COMMAND} -E env ICO_RD_STILL_REPRESENT=0)
  endif()
  execute_process(COMMAND ${_env} "${TOOL}" "${DUMP}" "${OUT}/${_mode}/still.png"
                          --interp 0.9 "${DUMP}" --interp-any --presents ${PRESENTS} ${ARGS}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out_${_mode} ERROR_VARIABLE err_${_mode})
  if(rc EQUAL 77 OR out_${_mode} MATCHES "SKIP:")
    message(STATUS "SKIP: no usable Vulkan device or mode")
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${TOOL} (${_mode}) exited ${rc}:\n${out_${_mode}}\n${err_${_mode}}")
  endif()
endforeach()
# the ";" in the tool's lines would split the match list: flattened first
string(REPLACE ";" "," flat_on "${out_on}")
string(REPLACE ";" "," flat_off "${out_off}")
string(REGEX MATCHALL "[^\n]*, shown again" shown_on "${flat_on}")
string(REGEX MATCHALL "[^\n]*, shown again" shown_off "${flat_off}")
list(LENGTH shown_on n_on)
list(LENGTH shown_off n_off)
math(EXPR want "${PRESENTS} - 1")
message(STATUS "${DUMP}: ${n_on} of ${PRESENTS} presents shown again (switch on), ${n_off} (off)")
if(NOT n_off EQUAL 0)
  message(FATAL_ERROR "${n_off} presents shown again with ICO_RD_STILL_REPRESENT=0")
endif()
if(NOT n_on EQUAL want)
  message(FATAL_ERROR "${n_on} presents shown again, want ${want}:\n${out_on}")
endif()
math(EXPR last "${PRESENTS} - 1")
foreach(k RANGE ${last})
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/on/still-p${k}.png"
                          "${OUT}/off/still-p${k}.png" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    message(FATAL_ERROR "present ${k}: the picture shown again differs from the rendered one")
  endif()
endforeach()
message(STATUS "${PRESENTS} pictures byte-identical with and without the re-show")
