# rd_replay_tool_velocity: rd_replay_tool's --velocity and --velocity-dump
# (port/render/rd_velocity.h) on the dump rd_velocity leaves behind (a moving
# prelit mesh whose draw carries its previous transform, dump version 16) and
# on rd_pixel's (no VU draw).
#   - --velocity-dump PNG writes a PNG with moving texels (the mesh moved 8
#     pixels) and the line says the moved draw (1 of the 2) carries its
#     previous transform and was drawn into VELOCITY
#   - Off identity: every target and the present are the same bytes with and
#     without --velocity, on both dumps (the velocity pass draws only into
#     VELOCITY; with it off nothing of it runs)
# -DTOOL=<rd_replay_tool> -DDUMP=<rd_velocity_moving.rddump>
# -DPIXEL=<rd_pixel_frame.rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_velocity found no device)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/off" "${OUT}/on" "${OUT}/poff" "${OUT}/pon")

set(names SCENE DISPLAY SHADOW0 SHADOW1 SHADOW2 WORK0 WORK1 WORK2 WORK3 AA0 AA1 FEED128
    AURA_WORK AURA_TAP WORK2_PAD FEED_HELD DISPLAY_HELD WORK3_HELD PRESENT)

execute_process(COMMAND "${TOOL}" "${DUMP}" - --pass-dump "${OUT}/off" --present 640x480
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: no device: ${err}")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the moving mesh without --velocity exited ${rc}:\n${out}\n${err}")
endif()
execute_process(COMMAND "${TOOL}" "${DUMP}" - --pass-dump "${OUT}/on" --present 640x480
                        --velocity-dump "${OUT}/velocity.png"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--velocity-dump exited ${rc}:\n${out}\n${err}")
endif()
if(NOT out MATCHES "velocity: 1 of 2 mesh and grid draws carry their previous transform; the replay drew [1-9][0-9]* into VELOCITY, 0 of list 0 left at 0")
  message(FATAL_ERROR "--velocity: the draw's previous transform not drawn:\n${out}")
endif()
if(NOT out MATCHES "velocity-dump: [0-9]+x[0-9]+ -> [^\n]*velocity.png: moving [1-9][0-9]* texels, the largest ([0-9.]+) pixels")
  message(FATAL_ERROR "--velocity-dump: no moving texel:\n${out}")
endif()
set(largest "${CMAKE_MATCH_1}")
if(largest LESS 7.5 OR largest GREATER 8.5)
  message(FATAL_ERROR "--velocity-dump: the largest motion ${largest} pixels, want 8:\n${out}")
endif()
file(READ "${OUT}/velocity.png" head LIMIT 8 HEX)
if(NOT head STREQUAL "89504e470d0a1a0a")
  message(FATAL_ERROR "velocity.png is not a PNG (${head})")
endif()
foreach(n ${names})
  file(SHA256 "${OUT}/off/${n}.png" a)
  file(SHA256 "${OUT}/on/${n}.png" b)
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "${n}.png of the moving mesh differs with --velocity")
  endif()
endforeach()

if(EXISTS "${PIXEL}")
  execute_process(COMMAND "${TOOL}" "${PIXEL}" - --pass-dump "${OUT}/poff" --present 640x480
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  execute_process(COMMAND "${TOOL}" "${PIXEL}" - --pass-dump "${OUT}/pon" --present 640x480
                          --velocity
                  RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2 ERROR_VARIABLE err2)
  if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0)
    message(FATAL_ERROR "rd_pixel's dump: exits ${rc} and ${rc2}:\n${err}\n${err2}")
  endif()
  if(NOT out2 MATCHES "velocity: 0 of 0 mesh and grid draws")
    message(FATAL_ERROR "rd_pixel's dump has VU draws now? the velocity line:\n${out2}")
  endif()
  foreach(n ${names})
    file(SHA256 "${OUT}/poff/${n}.png" a)
    file(SHA256 "${OUT}/pon/${n}.png" b)
    if(NOT a STREQUAL b)
      message(FATAL_ERROR "${n}.png of rd_pixel's dump differs with --velocity")
    endif()
  endforeach()
endif()
message(STATUS "rd_replay_tool_velocity: a moving texel set, 19 pictures unchanged by --velocity")
