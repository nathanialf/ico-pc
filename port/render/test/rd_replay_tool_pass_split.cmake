# rd_replay_tool_pass_split: the pass split (docs/port/RENDER.md, "The
# loss bisect"; ICO_RD_PASS_SPLIT, off by default) on the dump rd_pixel
# leaves behind: the frame replayed with a render pass ended and begun again
# after every 8 VU draws draws the same DISPLAY picture, byte for byte, as
# the plain replay, and begins more passes when a pass has more than 8.
#   Both runs without the destination read (ICO_VK_FAKE_NO_DST=1), as on a
#   phone: the split leaves a readable pass (the exact blend's) whole.
#   Exit 77 (no device) skips.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=ICO_RD_PASS_SPLIT ICO_VK_FAKE_NO_DST=1
                        "${TOOL}" "${DUMP}" "${OUT}/plain.png" --target DISPLAY --stats
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: no device: ${err}")
  return()
endif()
if(NOT rc EQUAL 0 OR NOT out MATCHES "stats: [0-9]+ draws, ([0-9]+) passes")
  message(FATAL_ERROR "plain replay exited ${rc}:\n${out}\n${err}")
endif()
set(plain ${CMAKE_MATCH_1})
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_PASS_SPLIT=8 ICO_VK_FAKE_NO_DST=1
                        "${TOOL}" "${DUMP}" "${OUT}/split.png" --target DISPLAY --stats
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "stats: [0-9]+ draws, ([0-9]+) passes")
  message(FATAL_ERROR "ICO_RD_PASS_SPLIT=8 exited ${rc}:\n${out}\n${err}")
endif()
set(split ${CMAKE_MATCH_1})
if(NOT "${out}${err}" MATCHES "pass split: every 8 VU draws")
  message(FATAL_ERROR "ICO_RD_PASS_SPLIT=8 was not taken:\n${out}\n${err}")
endif()
file(SHA256 "${OUT}/plain.png" a)
file(SHA256 "${OUT}/split.png" b)
if(NOT a STREQUAL b)
  message(FATAL_ERROR "ICO_RD_PASS_SPLIT=8 drew another picture than the plain replay")
endif()
message(STATUS "rd_replay_tool_pass_split: ${plain} passes plain, ${split} split every 8 VU draws; "
               "the same picture")
