# rd_date_selftest: DATE's self-test (rd_date_selftest.c) through
# rd_fog_selftest_test, which runs rd_fog_selftest (its first run runs DATE's
# self-test first), with the DATE path the environment asks for
# (ICO_RD_DATE_PATH=stencil: the profile's flag on wherever the device has
# local read); its exit code checked and its log matched:
#   "DATE: <path> path (self-test: stencil <result>, snapshot <result>)"
#   exactly once.
# -DTOOL=<rd_fog_selftest_test> -DPATH=<stencil|snapshot>
# -DRESULTS=<regex of the self-test's results>
execute_process(COMMAND "${TOOL}" inplace RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: no usable Vulkan device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_fog_selftest_test exited ${rc}:\n${out}\n${err}")
endif()
string(REGEX MATCHALL "DATE: ${PATH} path \\(self-test: ${RESULTS}\\)" lines "${err}")
list(LENGTH lines nlines)
if(NOT nlines EQUAL 1)
  message(FATAL_ERROR "${nlines} \"DATE: ${PATH} path (self-test: ${RESULTS})\" lines, 1 "
                      "expected:\n${err}")
endif()
message(STATUS "rd_date_selftest ${PATH}: the path and the self-test's results logged")
