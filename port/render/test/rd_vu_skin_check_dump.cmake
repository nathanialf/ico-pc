# rd_vu_skin_check_dump*, rd_vu_skin_check_163*: rd_replay_tool --skin-check
# on a dump (rd_vu_skin_check.c; docs/port/VU_SKIN_CHECK.md): the summary
# line "<dump>: skin check: ..." must match LINE; SKIP without the dump or
# a device (77).
# -DTOOL=<rd_replay_tool> -DDUMP=<dump> -DOUT=<dir> -DLINE=<regex>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: no ${DUMP}")
  return()
endif()
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/skin-check.png" --skin-check
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  message(STATUS "SKIP: no usable device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_replay_tool --skin-check exited ${rc}:\n${out}\n${err}")
endif()
string(REPLACE ";" "," flat "${out}")
if(NOT flat MATCHES "skin check: ${LINE}")
  message(FATAL_ERROR "the summary does not match \"skin check: ${LINE}\":\n${out}\n${err}")
endif()
message(STATUS "rd_replay_tool --skin-check: ${CMAKE_MATCH_0}")
