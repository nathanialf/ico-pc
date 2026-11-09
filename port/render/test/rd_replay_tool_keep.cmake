# rd_replay_tool_fsr3_keep / rd_replay_tool_upscale_keep: the keep loop of a
# stage switch's fade under a temporal upscaler (the display layer,
# port/render/rd_layer.h).  rd_pixel's keep frame (rd_pixel_keep.rddump:
# the head's clear, the brightness, KEEP, a fade, the reduction) replayed
# 20 times (--repeat 20: 20 ticks of the keep loop) after rd_pixel's frame
# (--prev: the picture the fade starts from), at Enhanced in the 1280x960
# box under FSR 3.1 Quality (MODE fsr3) or the test's upscaler at 1.5
# (MODE test, every backend):
#   - the picture before dispatched once and nothing more: the keep frames
#     hold the composite ("dispatched 1", "fsr3 ran 1 presents")
#   - 20 keep frames held, each with its keep sprite drawn as the 1:1 copy
#     ("keep frames held 20 (keep copies 20, no keep sprite 0)"), no layer
#     refused, no dispatch refused; under FSR 3.1 no validation error
# -DTOOL=<rd_replay_tool> -DDUMP=<rd_pixel_keep.rddump>
# -DPREV=<rd_pixel_frame.rddump> -DOUT=<png> -DMODE=fsr3|test
if(NOT EXISTS "${DUMP}" OR NOT EXISTS "${PREV}")
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
if(MODE STREQUAL "fsr3")
  set(up --fsr3 quality)
else()
  set(up --upscale-test 1.5)
endif()
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}" --enhanced ${up} --present 1280x960
                        --prev "${PREV}" --repeat 20 --stats
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: ${MODE}: ${err}")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the keep loop (${MODE}) exited ${rc}:\n${out}\n${err}")
endif()
if(NOT out MATCHES "upscale point: [^\n]*, dispatched 1, refused 0, ")
  message(FATAL_ERROR "${MODE}: not one dispatch (the picture before alone) with none refused:\n${out}")
endif()
if(NOT out MATCHES "keep frames held 20 \\(keep copies 20, no keep sprite 0\\)[^\n]*, refused 0\n")
  message(FATAL_ERROR "${MODE}: not 20 keep frames held as 1:1 copies, none refused:\n${out}")
endif()
if(MODE STREQUAL "fsr3" AND NOT out MATCHES
   "upscale: fsr3 ran 1 presents \\([^)]*refused 0, failed 0, [^)]*\\), validation errors 0")
  message(FATAL_ERROR "fsr3: not one FSR 3.1 dispatch without errors:\n${out}")
endif()
message(STATUS "rd_replay_tool keep loop (${MODE}): 20 keep frames held after one dispatch")
