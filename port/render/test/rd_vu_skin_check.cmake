# rd_vu_skin_check*: rd_vu_skin_check_test run, its exit code checked and,
# on a device, its log matched (rd_vu_skin_check.c, rd_vu_skin_selftest.c):
# the start-up probe's "VU skin probe: ok (...)" line and the check's start
# line once each, EXTRA a line the log must hold, NOT a line it must not.
# -DTOOL=<rd_vu_skin_check_test> -DMODE=<judge|clean|probe|ring>
# [-DEXTRA=<regex>] [-DNOT=<regex>]
execute_process(COMMAND "${TOOL}" "${MODE}" RESULT_VARIABLE rc OUTPUT_VARIABLE out
                ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  message(STATUS "SKIP: no usable device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_vu_skin_check_test ${MODE} exited ${rc}:\n${out}\n${err}")
endif()
if(MODE STREQUAL "judge")
  return()
endif()
string(REPLACE ";" "," flat "${err}")
foreach(_line "rd: VU skin probe: ok \\(120 vertices x 64 offsets in [0-9]+ page phases, codes 20 22 24, (plain|extended) set\\) in [0-9.]+ ms"
              "rd: VU skin check: on \\(one skinned draw every 100 ms")
  string(REGEX MATCHALL "${_line}" lines "${flat}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1)
    message(FATAL_ERROR "${n} \"${_line}\" lines, 1 expected:\n${err}")
  endif()
endforeach()
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
message(STATUS "rd_vu_skin_check ${MODE}: the lines logged")
