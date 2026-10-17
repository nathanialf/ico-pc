# rd_replay_tool_loss_bisect: the loss bisect and the pass window
# (docs/port/RENDER.md, "The loss bisect") on the dump rd_pixel leaves behind.
#   - --loss-bisect runs the whole frame one pass longer each time without a
#     loss on this device: "clean", the frame's P passes counted and P + 1
#     replays (the count, then passes 1..k for k = 1..P)
#   - --passes 1-P (the whole frame) draws the same DISPLAY picture as a
#     replay without a window, and does not stop the walk
#   - --passes 1 stops the walk where pass 2 begins (when there is one)
#   - the second level's draw window: --passes 1-P --draws 1-N (every draw of
#     pass P) draws the plain picture; --loss-bisect --passes P --draws 1-2
#     and --loss-bisect --passes P --mask-marks run one step each, clean on
#     this device, and name the last draw they recorded
#   Exit 77 (no device) skips.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

execute_process(COMMAND "${TOOL}" "${DUMP}" - --loss-bisect
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: no device: ${err}")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--loss-bisect exited ${rc}:\n${out}\n${err}")
endif()
if(NOT out MATCHES "loss bisect: clean; frame [0-9]+, ([0-9]+) passes, ([0-9]+) replays")
  message(FATAL_ERROR "--loss-bisect did not report a clean frame:\n${out}")
endif()
set(passes ${CMAKE_MATCH_1})
set(replays ${CMAKE_MATCH_2})
math(EXPR want "${passes} + 1")
if(passes LESS 1 OR NOT replays EQUAL want)
  message(FATAL_ERROR "--loss-bisect: ${passes} passes in ${replays} replays, want ${want}:\n${out}")
endif()

execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/plain.png" --target DISPLAY
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--target DISPLAY exited ${rc}:\n${err}")
endif()
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/window.png" --target DISPLAY
                        --passes 1-${passes}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--passes 1-${passes} exited ${rc}:\n${err}")
endif()
if(NOT out MATCHES "passes 1-${passes} of the ${passes} begun;")
  message(FATAL_ERROR "--passes 1-${passes}: the walk stopped or the count differs:\n${out}")
endif()
file(SHA256 "${OUT}/plain.png" a)
file(SHA256 "${OUT}/window.png" b)
if(NOT a STREQUAL b)
  message(FATAL_ERROR "--passes 1-${passes} drew another picture than the plain replay")
endif()
if(passes GREATER 1)
  execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/one.png" --target DISPLAY --passes 1
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "passes 1-1 of the ${passes} begun \\(the walk stopped after the window\\)")
    message(FATAL_ERROR "--passes 1: exit ${rc}, want the walk stopped at pass 2:\n${out}\n${err}")
  endif()
endif()

# the second level: the draw window and one step each way
execute_process(COMMAND "${TOOL}" "${DUMP}" - --passes ${passes}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "; ([0-9]+) draws in pass ${passes}")
  message(FATAL_ERROR "--passes ${passes}: exit ${rc}, no draw count:\n${out}\n${err}")
endif()
set(draws ${CMAKE_MATCH_1})
if(draws GREATER 0)
  execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/draws.png" --target DISPLAY
                          --passes 1-${passes} --draws 1-${draws}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "draws 1-${draws} of pass ${passes}: ${draws} drawn; the last: ")
    message(FATAL_ERROR "--draws 1-${draws}: exit ${rc}:\n${out}\n${err}")
  endif()
  file(SHA256 "${OUT}/draws.png" c)
  if(NOT a STREQUAL c)
    message(FATAL_ERROR "--passes 1-${passes} --draws 1-${draws} drew another picture than the "
                        "plain replay")
  endif()
  execute_process(COMMAND "${TOOL}" "${DUMP}" - --loss-bisect --passes ${passes} --draws 1-2
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "loss bisect step: clean; pass ${passes} of ${passes}, draws 1..2 of ${draws}"
     OR NOT out MATCHES "loss bisect step: the last draw: list ")
    message(FATAL_ERROR "--loss-bisect --draws 1-2: exit ${rc}:\n${out}\n${err}")
  endif()
  execute_process(COMMAND "${TOOL}" "${DUMP}" - --loss-bisect --passes ${passes} --mask-marks
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "loss bisect step: clean; pass ${passes} of ${passes}, draws 1..${draws} of ${draws}, marks masked, ${draws} drawn")
    message(FATAL_ERROR "--loss-bisect --mask-marks: exit ${rc}:\n${out}\n${err}")
  endif()
endif()
message(STATUS "rd_replay_tool_loss_bisect: ${passes} passes, ${replays} replays, clean; "
               "${draws} draws in the last pass")
