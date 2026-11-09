# rd_replay_tool_fsr3_dump: rd_replay_tool's --fsr3-dump DIR (issue 203;
# port/render/rd_upscale_capture.h), the same four pictures F12 and the
# bug-report row save under an upscaler, on rd_pixel's dump at Enhanced in
# the 1280x960 box at FSR 3.1 Quality (rd_replay_tool_fsr3's run):
#   - the line names <dump>-scene.png, -velocity.png, -depth.png and
#     -composite.png with their sizes; the four files are PNGs of those
#     sizes; SCENE, VELOCITY and the depth at FSR's render size, the
#     composite at its display size (both from --stats' context line)
#   - the capture changes no picture: the present the same bytes with and
#     without --fsr3-dump
#   - without an upscaler --fsr3-dump fails (exit 1, no dispatch named)
# 77 (SKIP) where FSR 3.1 cannot run, as rd_replay_tool_fsr3.
# -DTOOL=<rd_replay_tool> -DDUMP=<rd_pixel_frame.rddump> -DOUT=<a folder>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/pics")

set(run --enhanced --fsr3 quality --present 1280x960 --stats)
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/plain.png" ${run}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: FSR 3.1 cannot run here: ${err}")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the FSR 3.1 run exited ${rc}:\n${out}\n${err}")
endif()
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/dumped.png" ${run} --fsr3-dump "${OUT}/pics"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--fsr3-dump exited ${rc}:\n${out}\n${err}")
endif()
if(NOT out MATCHES "context render ([0-9]+)x([0-9]+) -> display ([0-9]+)x([0-9]+)")
  message(FATAL_ERROR "--stats: no FSR 3.1 context line:\n${out}")
endif()
set(rw "${CMAKE_MATCH_1}")
set(rh "${CMAKE_MATCH_2}")
set(dw "${CMAKE_MATCH_3}")
set(dh "${CMAKE_MATCH_4}")
if(NOT out MATCHES "fsr3-dump: ([^\n]*)-scene.png ([0-9]+)x([0-9]+), -velocity.png ([0-9]+)x([0-9]+), -depth.png ([0-9]+)x([0-9]+), -composite.png ([0-9]+)x([0-9]+)\n")
  message(FATAL_ERROR "--fsr3-dump: no line naming the four pictures:\n${out}\n${err}")
endif()
set(stem "${CMAKE_MATCH_1}")
set(sizes "${CMAKE_MATCH_2}x${CMAKE_MATCH_3} ${CMAKE_MATCH_4}x${CMAKE_MATCH_5} ${CMAKE_MATCH_6}x${CMAKE_MATCH_7} ${CMAKE_MATCH_8}x${CMAKE_MATCH_9}")
set(want "${rw}x${rh} ${rw}x${rh} ${rw}x${rh} ${dw}x${dh}")
if(NOT sizes STREQUAL want)
  message(FATAL_ERROR "--fsr3-dump: sizes ${sizes}, want ${want} (scene, velocity, depth at the render size, composite at the display size)")
endif()
set(i 0)
foreach(suffix scene velocity depth composite)
  set(f "${stem}-${suffix}.png")
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "--fsr3-dump: ${f} not written")
  endif()
  file(READ "${f}" head LIMIT 24 HEX)
  string(SUBSTRING "${head}" 0 16 sig)
  string(SUBSTRING "${head}" 32 8 hw)
  string(SUBSTRING "${head}" 40 8 hh)
  math(EXPR pw "0x${hw}")
  math(EXPR ph "0x${hh}")
  string(REPLACE " " ";" wl "${want}")
  list(GET wl ${i} wsize)
  if(NOT sig STREQUAL "89504e470d0a1a0a" OR NOT "${pw}x${ph}" STREQUAL "${wsize}")
    message(FATAL_ERROR "${f}: ${sig}, ${pw}x${ph} (want a PNG of ${wsize})")
  endif()
  math(EXPR i "${i} + 1")
endforeach()
file(SHA256 "${OUT}/plain.png" a)
file(SHA256 "${OUT}/dumped.png" b)
if(NOT a STREQUAL b)
  message(FATAL_ERROR "--fsr3-dump changed the present (${OUT}/plain.png and dumped.png differ)")
endif()
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/none.png" --enhanced --present 1280x960
                        --fsr3-dump "${OUT}/pics"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 1 OR NOT err MATCHES "no dispatch of the upscale point")
  message(FATAL_ERROR "--fsr3-dump without an upscaler: exit ${rc}, want 1 and the no-dispatch line:\n${out}\n${err}")
endif()
message(STATUS "fsr3-dump: ${sizes}, the present unchanged")
