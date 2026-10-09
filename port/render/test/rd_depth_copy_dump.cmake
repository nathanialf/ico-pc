# rd_fog_depth_copy_dump: a player dump with the depth fog (the dark hall,
# frame-20261009-083248-v13836: RD_POST_FOG on SCENE's depth, then the aura
# on the same depth) replayed by rd_replay_tool twice, the fog sampling the
# depth in place (ICO_RD_DEPTH_COPY=0) and a copy of it (=1, the path of
# tile-based GPUs and D24S8): SCENE must come out byte for byte the same.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<dir> [-DEXTRA_ENV=VAR=VALUE]
if(NOT EXISTS "${DUMP}")
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: ${DUMP}: no dump (dist/dumps is not in the tree)")
  return()
endif()
file(MAKE_DIRECTORY "${OUT}")
set(env "")
if(EXTRA_ENV)
  set(env "${EXTRA_ENV}")
endif()
foreach(mode 0 1)
  set(png "${OUT}/scene-copy${mode}.png")
  file(REMOVE "${png}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env} "ICO_RD_DEPTH_COPY=${mode}"
                          "${TOOL}" "${DUMP}" "${png}" --target SCENE
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: rd_replay_tool found no usable device")
    return()
  endif()
  if(NOT rc EQUAL 0 OR NOT EXISTS "${png}")
    message(FATAL_ERROR "rd_replay_tool (ICO_RD_DEPTH_COPY=${mode}) exited ${rc}:\n${out}\n${err}")
  endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/scene-copy0.png"
                        "${OUT}/scene-copy1.png" RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR "SCENE differs between the depth in place and its copy "
                      "(${OUT}/scene-copy0.png, ${OUT}/scene-copy1.png)")
endif()
message(STATUS "rd_fog_depth_copy_dump: SCENE identical with the depth in place and its copy")
