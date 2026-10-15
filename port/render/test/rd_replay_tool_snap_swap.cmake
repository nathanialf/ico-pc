# rd_replay_tool_snap_swap: the staticBlur sprites that cover their whole
# target draw into the snapshot texture and swap it with the target's
# instead of copying the target first (issue 103, rd_replay.c
# blurCoversTarget, blurSwapTarget; ICO_RD_SNAP_SWAP=0 keeps the copy).  On
# the dump rd_pixel leaves behind, at 1x and at 2x, each blend path the
# device has: the DISPLAY picture with the swap is the copy's, byte for
# byte, and the swap takes no more snapshots than the copy.
#   Exit 77 (no device) skips.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

set(report "")
foreach(blend attachment storage fixed)
  foreach(scale 1x 2x)
    set(extra "")
    if(scale STREQUAL "2x")
      set(extra --enhanced --resolution 2x)
    endif()
    set(skipped 0)
    foreach(swap 0 1)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=${blend} ICO_RD_SNAP_SWAP=${swap}
                              "${TOOL}" "${DUMP}" "${OUT}/${blend}-${scale}-swap${swap}.png"
                              --target DISPLAY --stats ${extra}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if("${out}${err}" MATCHES "is not on this device")
        set(skipped 1)
        break()
      endif()
      if(NOT rc EQUAL 0 OR NOT out MATCHES "snapshots ([0-9]+) copied, ([0-9]+) swapped")
        message(FATAL_ERROR "${blend} ${scale} ICO_RD_SNAP_SWAP=${swap} exited ${rc}:\n${out}\n${err}")
      endif()
      set(copied${swap} ${CMAKE_MATCH_1})
      set(swapped${swap} ${CMAKE_MATCH_2})
    endforeach()
    if(skipped)
      continue()
    endif()
    file(SHA256 "${OUT}/${blend}-${scale}-swap0.png" a)
    file(SHA256 "${OUT}/${blend}-${scale}-swap1.png" b)
    if(NOT a STREQUAL b)
      message(FATAL_ERROR "${blend} ${scale}: the swap drew another picture than the copy")
    endif()
    if(NOT swapped0 EQUAL 0)
      message(FATAL_ERROR "${blend} ${scale}: ICO_RD_SNAP_SWAP=0 swapped ${swapped0} times")
    endif()
    math(EXPR total1 "${copied1} + ${swapped1}")
    if(NOT total1 EQUAL copied0)
      message(FATAL_ERROR "${blend} ${scale}: ${copied1} copies and ${swapped1} swaps for the "
                          "copy's ${copied0} snapshots")
    endif()
    string(APPEND report " ${blend} ${scale}: ${copied0} copies -> ${copied1} copies and ${swapped1} swaps;")
  endforeach()
endforeach()
message(STATUS "rd_replay_tool_snap_swap:${report} the same pictures")
