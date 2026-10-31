# rd_replay_tool_save_blended: the blended present's frame F12 saves (rd.h
# rd_dump_blended_arm: the frame rd_interp.c built between two ticks,
# written by rd__dump_frame after its replay) replays on its own as drawn.
# rd_replay_tool CUR --interp T PREV --save-blended B writes that frame with
# the same writer; B replayed alone (no --interp) gives SCENE and the
# presented picture byte for byte as the --interp run drew them.
# -DTOOL=<rd_replay_tool> -DPREV=<dump> -DCUR=<dump> -DT=<alpha>
# -DOUT=<dir> -DARGS=<a|b|...>
foreach(_d PREV CUR)
  if(NOT EXISTS "${${_d}}")
    # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
    message(STATUS "SKIP: ${${_d}}: no dump (build-host/tmp is not in the tree)")
    return()
  endif()
endforeach()
string(REPLACE "|" ";" ARGS "${ARGS}")
file(MAKE_DIRECTORY "${OUT}")

foreach(_what SCENE PRESENT)
  if(_what STREQUAL "PRESENT")
    set(_t --present 1280x960)
  else()
    set(_t --target ${_what})
  endif()
  set(_b "${OUT}/blended-${_what}.rddump")
  file(REMOVE "${_b}")
  # the frame between the ticks, and its dump
  execute_process(COMMAND "${TOOL}" "${CUR}" "${OUT}/interp-${_what}.png" --interp ${T} "${PREV}"
                          --save-blended "${_b}" ${ARGS} ${_t}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77 OR out MATCHES "SKIP:")
    message(STATUS "SKIP: no usable Vulkan device or mode")
    return()
  endif()
  if(NOT rc EQUAL 0 OR NOT out MATCHES "the blended frame \\(frame [0-9]+ at alpha [0-9.e-]+\\) saved to")
    message(FATAL_ERROR "${TOOL} --interp ${T} --save-blended (${_what}) exited ${rc}:\n${out}\n${err}")
  endif()
  # the dump alone
  execute_process(COMMAND "${TOOL}" "${_b}" "${OUT}/blended-${_what}.png" ${ARGS} ${_t}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out2 ERROR_VARIABLE err2)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${TOOL} ${_b} (${_what}) exited ${rc}:\n${out2}\n${err2}")
  endif()
  if(out2 MATCHES "interp [0-9]+ -> ")
    message(FATAL_ERROR "the blended dump's replay interpolated:\n${out2}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/interp-${_what}.png"
                          "${OUT}/blended-${_what}.png" RESULT_VARIABLE d)
  if(NOT d EQUAL 0)
    message(FATAL_ERROR "${_what}: the blended dump's replay differs from --interp ${T}: "
                        "${OUT}/interp-${_what}.png and ${OUT}/blended-${_what}.png")
  endif()
endforeach()
message(STATUS "--interp ${T}: SCENE and the presented picture byte-identical replayed from the "
               "saved blended frame")
