# rd_replay_tool_loss_bisect: the loss bisect and the pass window
# (docs/port/RENDER.md, "The loss bisect") on the dump rd_pixel leaves behind.
#   - --loss-bisect runs the whole frame one pass longer each time without a
#     loss on this device: "clean", the frame's P passes counted and P + 1
#     replays (the count, then passes 1..k for k = 1..P)
#   - --passes 1-P (the whole frame) draws the same DISPLAY picture as a
#     replay without a window, and does not stop the walk
#   - --passes 1 stops the walk where pass 2 begins (when there is one)
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
  if(NOT rc EQUAL 0 OR NOT out MATCHES "passes 1-1 of the 2 begun \\(the walk stopped after the window\\)")
    message(FATAL_ERROR "--passes 1: exit ${rc}, want the walk stopped at pass 2:\n${out}\n${err}")
  endif()
endif()
message(STATUS "rd_replay_tool_loss_bisect: ${passes} passes, ${replays} replays, clean")
