# rd_vuprepass_selftest*: rd_vuprepass_selftest_test run, its exit code
# checked and its log matched (rd_vuprepass_selftest.c): the one line
# "VU pre-pass: on (self-test: ...)", "VU pre-pass: off (self-test:
# <reason>)" or "VU pre-pass: off (self-test not run)", LINE its regex;
# EXTRA: another line the log must hold, NOT_ a line it must not (both
# matched with the log's ";" read as ",": a ";" in a -D argument would
# split it).
# -DTOOL=<rd_vuprepass_selftest_test> -DMODE=<on|off|notrun> [-DVARIANT=plain]
# -DLINE=<regex> [-DEXTRA=<regex>] [-DNOT_=<regex>]
execute_process(COMMAND "${TOOL}" "${MODE}" ${VARIANT} RESULT_VARIABLE rc OUTPUT_VARIABLE out
                ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  message(STATUS "SKIP: no usable device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_vuprepass_selftest_test ${MODE} exited ${rc}:\n${out}\n${err}")
endif()
# counted as list items: a ";" in a line would split one
string(REPLACE ";" "," flat "${err}")
string(REGEX MATCHALL "rd: VU pre-pass: ${LINE}" lines "${flat}")
list(LENGTH lines n)
if(NOT n EQUAL 1)
  message(FATAL_ERROR "${n} \"rd: VU pre-pass: ${LINE}\" lines, 1 expected:\n${err}")
endif()
if(EXTRA)
  if(NOT flat MATCHES "${EXTRA}")
    message(FATAL_ERROR "no line matching \"${EXTRA}\":\n${err}")
  endif()
endif()
if(NOT_)
  if(flat MATCHES "${NOT_}")
    message(FATAL_ERROR "a line matching \"${NOT_}\":\n${err}")
  endif()
endif()
message(STATUS "rd_vuprepass_selftest ${MODE}: the pre-pass's verdict logged")
