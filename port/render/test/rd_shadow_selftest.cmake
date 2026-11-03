# rd_shadow_selftest: the shadow count's self-test (rd_shadow_selftest.c)
# through rd_fog_selftest_test, whose fog self-test runs it on its first run
# (a wide scene: 16:9, scale 2, Full pixel) and again when the scene is made
# at another size; its exit code checked and its log matched: every
#   "shadow count: <path> (self-test: <result>)"
# line matches PATH and RESULTS, and there is at least one.
# -DTOOL=<rd_fog_selftest_test> -DPATH=<one draw|seven draws>
# -DRESULTS=<regex of the self-test's result>
# -DARGS=<w|h|scale|aa|0|0|0|0|aspect> (optional: the tool's arguments after the
#   path, "|" separated; the 624-wide 16:10 frame, exact 614.40, and 2048 at 48:9)
# -DEXTRA=, -DEXTRA2=<regex> (optional: each must match the log at least once;
#   rd_shadow_selftest_sabotage's switch line and "profile: shadow count one
#   draw off (self-test)")
if(ARGS)
  string(REPLACE "|" ";" ARGS "${ARGS}")
endif()
execute_process(COMMAND "${TOOL}" inplace ${ARGS} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: no usable device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_fog_selftest_test exited ${rc}:\n${out}\n${err}")
endif()
string(REGEX MATCHALL "shadow count: [a-z ]+ \\(self-test: " all "${err}")
string(REGEX MATCHALL "shadow count: ${PATH} \\(self-test: ${RESULTS}\\)" lines "${err}")
list(LENGTH all nall)
list(LENGTH lines nlines)
if(nlines EQUAL 0 OR NOT nlines EQUAL nall)
  message(FATAL_ERROR "${nlines} of ${nall} self-test lines are \"shadow count: ${PATH} "
                      "(self-test: ${RESULTS})\", all of them (at least one) expected:\n${err}")
endif()
foreach(_x EXTRA EXTRA2)
  if(${_x})
    string(REGEX MATCHALL "${${_x}}" xs "${err}")
    list(LENGTH xs nx)
    if(nx EQUAL 0)
      message(FATAL_ERROR "no \"${${_x}}\" line:\n${err}")
    endif()
  endif()
endforeach()
message(STATUS "rd_shadow_selftest ${PATH}: ${nlines} self-test lines as expected")
