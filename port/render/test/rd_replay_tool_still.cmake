# rd_replay_tool_still: the still re-present (rd_reshow.c) against rendering
# every present.  Each fixture LIST names (port/render/test/fixtures,
# docs/port/TESTING_FIXTURES.md) is its own tick before (--interp-any):
# nothing moves, so the presents of the tick after the first have the first's
# frame.  rd_replay_tool --presents N writes N pictures; run with the switch on
# (the default) and with ICO_RD_STILL_REPRESENT=0, every picture must be the
# same bytes, the default run must say "shown again" for presents 1..N-1, and
# the run with the switch off must say it for none.  The path ran: those
# "shown again" lines, and the fixture's declared quantities (its list line's
# words: draws) on the stats of one more replay of it alone (--presents prints
# none).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<dir>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<list> -DOUT=<dir>
# -DARGS=<a|b|...> -DPRESENTS=<N>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
rd_fixture_list("${LIST}" fixtures)
string(REPLACE "|" ";" ARGS "${ARGS}")
math(EXPR want "${PRESENTS} - 1")
math(EXPR last "${PRESENTS} - 1")
foreach(fx IN LISTS fixtures)
  rd_fixture_list_props(${fx})
  rd_fixture_expand(${fx} "${OUT}/fixtures" dump)
  set(o "${OUT}/${fx}")
  file(MAKE_DIRECTORY "${o}/on" "${o}/off")
  foreach(_mode on off)
    set(_env "")
    if(_mode STREQUAL "off")
      set(_env ${CMAKE_COMMAND} -E env ICO_RD_STILL_REPRESENT=0)
    endif()
    execute_process(COMMAND ${_env} "${TOOL}" "${dump}" "${o}/${_mode}/still.png"
                            --interp 0.9 "${dump}" --interp-any --presents ${PRESENTS} ${ARGS}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out_${_mode} ERROR_VARIABLE err_${_mode})
    if(rc EQUAL 77 OR out_${_mode} MATCHES "SKIP:")
      message(STATUS "SKIP: no usable Vulkan device or mode")
      return()
    endif()
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${TOOL} (${fx} ${_mode}) exited ${rc}:\n${out_${_mode}}\n${err_${_mode}}")
    endif()
  endforeach()
  # the fixture's declared quantities: one replay of it alone (--presents
  # prints no stats)
  execute_process(COMMAND "${TOOL}" "${dump}" "${o}/alone.png" ${ARGS} --stats
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out_alone ERROR_VARIABLE err_alone)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${TOOL} (${fx} alone) exited ${rc}:\n${out_alone}\n${err_alone}")
  endif()
  rd_fixture_require(${fx} "${out_alone}" draws)
  # the ";" in the tool's lines would split the match list: flattened first
  string(REPLACE ";" "," flat_on "${out_on}")
  string(REPLACE ";" "," flat_off "${out_off}")
  string(REGEX MATCHALL "[^\n]*, shown again" shown_on "${flat_on}")
  string(REGEX MATCHALL "[^\n]*, shown again" shown_off "${flat_off}")
  list(LENGTH shown_on n_on)
  list(LENGTH shown_off n_off)
  message(STATUS "${fx}: ${n_on} of ${PRESENTS} presents shown again (switch on), ${n_off} (off)")
  if(NOT n_off EQUAL 0)
    message(FATAL_ERROR "${fx}: ${n_off} presents shown again with ICO_RD_STILL_REPRESENT=0")
  endif()
  if(NOT n_on EQUAL want)
    message(FATAL_ERROR "${fx}: ${n_on} presents shown again, want ${want}:\n${out_on}")
  endif()
  foreach(k RANGE ${last})
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${o}/on/still-p${k}.png"
                            "${o}/off/still-p${k}.png" RESULT_VARIABLE same)
    if(NOT same EQUAL 0)
      message(FATAL_ERROR "${fx} present ${k}: the picture shown again differs from the "
                          "rendered one")
    endif()
  endforeach()
endforeach()
if(RDFX_FAILURES GREATER 0)
  message(FATAL_ERROR "rd_replay_tool_still: ${RDFX_FAILURES} fixtures did not take the path")
endif()
list(LENGTH fixtures n)
message(STATUS "${PRESENTS} pictures byte-identical with and without the re-show over ${n} "
               "fixtures")
