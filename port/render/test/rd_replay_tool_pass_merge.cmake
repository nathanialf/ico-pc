# rd_replay_tool_pass_merge: the storage pass merge (issue 103,
# docs/port/RENDER.md "Destination reads and blend paths";
# ICO_RD_PASS_MERGE=1, off by default) on the dump rd_pixel leaves behind,
# under the storage destination read (ICO_RD_BLEND=storage, a device with
# the fragment shader interlock or ROV): the frame replayed with the merge draws the
# same DISPLAY picture, byte for byte, as with ICO_RD_PASS_MERGE=0, and
# switches between storage passes and passes with the colour attached no
# more often.  Then the same at 2x (--enhanced --resolution 2x).
#   Exit 77 (no device) skips; so does a device without the storage read
#   (lavapipe: no interlock).
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

set(report "")
foreach(scale 1x 2x)
  set(extra "")
  if(scale STREQUAL "2x")
    set(extra --enhanced --resolution 2x)
  endif()
  foreach(merge 0 1)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=storage ICO_RD_PASS_MERGE=${merge}
                            "${TOOL}" "${DUMP}" "${OUT}/${scale}-merge${merge}.png" --target DISPLAY
                            --stats ${extra}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    if("${out}${err}" MATCHES "ICO_RD_BLEND=storage is not on this device")
      message(STATUS "SKIP: no storage destination read on this device")
      return()
    endif()
    if(NOT rc EQUAL 0 OR NOT out MATCHES "stats: [0-9]+ draws, ([0-9]+) passes")
      message(FATAL_ERROR "${scale} ICO_RD_PASS_MERGE=${merge} exited ${rc}:\n${out}\n${err}")
    endif()
    set(passes${merge} ${CMAKE_MATCH_1})
    if(NOT out MATCHES "stats: ([0-9]+) destination-read draws, ([0-9]+) storage passes, ([0-9]+) pass switches")
      message(FATAL_ERROR "${scale} ICO_RD_PASS_MERGE=${merge}: no exact blend counts:\n${out}")
    endif()
    set(dst${merge} ${CMAKE_MATCH_1})
    set(storage${merge} ${CMAKE_MATCH_2})
    set(switches${merge} ${CMAKE_MATCH_3})
  endforeach()
  if(NOT "${out}${err}" MATCHES "blend: exact \\((interlock|rov)\\)")
    message(FATAL_ERROR "${scale}: the storage read was not taken:\n${out}\n${err}")
  endif()
  file(SHA256 "${OUT}/${scale}-merge0.png" a)
  file(SHA256 "${OUT}/${scale}-merge1.png" b)
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "${scale}: the storage pass merge drew another picture than ICO_RD_PASS_MERGE=0")
  endif()
  if(switches1 GREATER switches0 OR passes1 GREATER passes0)
    message(FATAL_ERROR "${scale}: the merge switched more (${switches1} against ${switches0}) "
                        "or began more passes (${passes1} against ${passes0})")
  endif()
  string(APPEND report " ${scale}: ${passes0} passes, ${switches0} switches, ${storage0} storage "
                       "passes, ${dst0} destination-read draws without the merge; ${passes1}, "
                       "${switches1}, ${storage1}, ${dst1} with it;")
endforeach()
message(STATUS "rd_replay_tool_pass_merge:${report} the same picture")
