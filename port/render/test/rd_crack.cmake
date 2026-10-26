# rd_crack*, rd_replay_tool_crack: the crack filler above 1x
# (rd_crack_fill.c). TOOL is run with ARGS, its exit code checked (77 or a
# "SKIP:" line: skipped) and its output and log matched:
# -DTOOL=<rd_crack_test or rd_replay_tool> -DARGS=<a|b|...>
# -DEXPECT=<regex>, -DEXPECT2=<regex> (optional): each must match at least
#   once (the "rd: crack fill: ..." state line, the --stats line)
# -DDENY=<regex> (optional): must not match
# -DNEED_FILE=<path> (optional): SKIP when it does not exist (a
#   machine-local corpus dump)
# -DNEED=<regex> (optional): SKIP when the run does not log it (an MSAA
#   count the device does not offer is drawn as off)
if(NEED_FILE AND NOT EXISTS "${NEED_FILE}")
  message(STATUS "SKIP: ${NEED_FILE} is not on this machine")
  return()
endif()
if(ARGS)
  string(REPLACE "|" ";" ARGS "${ARGS}")
endif()
execute_process(COMMAND "${TOOL}" ${ARGS} RESULT_VARIABLE rc OUTPUT_VARIABLE out
                ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77 OR out MATCHES "SKIP:")
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: no usable Vulkan device or mode")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${TOOL} exited ${rc}:\n${out}\n${err}")
endif()
set(all "${out}\n${err}")
if(NEED AND NOT all MATCHES "${NEED}")
  message(STATUS "SKIP: no \"${NEED}\" line (not offered on this device)")
  return()
endif()
foreach(_x EXPECT EXPECT2)
  if(${_x})
    string(REGEX MATCHALL "${${_x}}" xs "${all}")
    list(LENGTH xs nx)
    if(nx EQUAL 0)
      message(FATAL_ERROR "no \"${${_x}}\" line:\n${err}")
    endif()
  endif()
endforeach()
if(DENY)
  string(REGEX MATCHALL "${DENY}" ds "${all}")
  list(LENGTH ds nd)
  if(NOT nd EQUAL 0)
    message(FATAL_ERROR "a \"${DENY}\" line:\n${err}")
  endif()
endif()
message(STATUS "${TOOL}: the crack filler's lines logged")
