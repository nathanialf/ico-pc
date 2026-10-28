# rd_replay_tool_pass_dump: rd_replay_tool's --pass-dump, --target and --aspect
# on the dump rd_pixel leaves behind.
#   - an unknown --target and a --aspect wider than the dump (a 4:3 dump
#     cannot show 16:9) exit 1 with their messages; neither needs a device
#   - --pass-dump DIR renders once and writes <TARGET>.png for the 18 named
#     targets and PRESENT.png with --present; a PNG each (the signature), and
#     the DISPLAY one equal to a run of --target DISPLAY of its own.  Exit 77
#     (no device) skips this half.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/x.png" --target NOPE --no-device
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 1 OR NOT err MATCHES "unknown target \"NOPE\"")
  message(FATAL_ERROR "an unknown --target: exit ${rc}, want 1 and a message:\n${err}")
endif()
if(EXISTS "${OUT}/x.png")
  message(FATAL_ERROR "an unknown --target wrote a picture")
endif()

execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/x.png" --aspect 16:9
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 1 OR NOT err MATCHES "a dump drawn at 4:3 cannot show 16:9: capture an aspect sweep")
  message(FATAL_ERROR "--aspect 16:9 on a 4:3 dump: exit ${rc}, want 1 and the message:\n${err}")
endif()

set(names SCENE DISPLAY SHADOW0 SHADOW1 SHADOW2 WORK0 WORK1 WORK2 WORK3 AA0 AA1 FEED128
    AURA_WORK AURA_TAP WORK2_PAD FEED_HELD DISPLAY_HELD WORK3_HELD)
execute_process(COMMAND "${TOOL}" "${DUMP}" - --pass-dump "${OUT}" --present 640x480
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: no device: ${err}")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--pass-dump exited ${rc}:\n${out}\n${err}")
endif()
foreach(n ${names} PRESENT)
  if(NOT EXISTS "${OUT}/${n}.png")
    message(FATAL_ERROR "--pass-dump wrote no ${n}.png:\n${out}")
  endif()
  file(READ "${OUT}/${n}.png" head LIMIT 8 HEX)
  if(NOT head STREQUAL "89504e470d0a1a0a")
    message(FATAL_ERROR "${n}.png is not a PNG (${head})")
  endif()
endforeach()
# one process each gives the same picture
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/single.png" --target DISPLAY
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--target DISPLAY exited ${rc}:\n${err}")
endif()
file(SHA256 "${OUT}/single.png" a)
file(SHA256 "${OUT}/DISPLAY.png" b)
if(NOT a STREQUAL b)
  message(FATAL_ERROR "DISPLAY.png of --pass-dump differs from --target DISPLAY")
endif()
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/single.png" --present 640x480
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(SHA256 "${OUT}/single.png" a)
file(SHA256 "${OUT}/PRESENT.png" b)
if(NOT rc EQUAL 0 OR NOT a STREQUAL b)
  message(FATAL_ERROR "PRESENT.png of --pass-dump differs from --present 640x480 (rc ${rc})")
endif()
message(STATUS "rd_replay_tool_pass_dump: 18 targets and PRESENT in one run")
