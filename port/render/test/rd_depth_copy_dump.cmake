# rd_fog_depth_copy_dump: a player dump with the depth fog (the dark hall,
# frame-20261009-083248-v13836: RD_POST_FOG on SCENE's depth, then the aura
# on the same depth) replayed by rd_replay_tool three times, the fog
# sampling the depth in place (ICO_RD_DEPTH_COPY=0), a copy of it (=1, the
# path of tile-based GPUs and D24S8) and reading its words through a buffer
# (ICO_RD_FOG_PATH=buffer, rd_fog_path.c): SCENE must come out byte for
# byte the same.  -DNO_BUFFER=1 leaves the buffer out (D24S8: the shader
# turns the 24-bit step into a float itself, which may round a depth one
# step of the float apart from the device's own conversion, and a pixel
# whose step sits on an index boundary then takes the index next to it).
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<dir> [-DEXTRA_ENV=VAR=VALUE]
# [-DNO_BUFFER=1]
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
set(modes 0 1 buffer)
if(NO_BUFFER)
  set(modes 0 1)
endif()
foreach(mode ${modes})
  set(png "${OUT}/scene-copy${mode}.png")
  file(REMOVE "${png}")
  if(mode STREQUAL "buffer")
    set(path "ICO_RD_FOG_PATH=buffer")
  else()
    set(path "ICO_RD_DEPTH_COPY=${mode}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env} "${path}"
                          "${TOOL}" "${DUMP}" "${png}" --target SCENE
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: rd_replay_tool found no usable device")
    return()
  endif()
  if(NOT rc EQUAL 0 OR NOT EXISTS "${png}")
    message(FATAL_ERROR "rd_replay_tool (${path}) exited ${rc}:\n${out}\n${err}")
  endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/scene-copy0.png"
                        "${OUT}/scene-copy1.png" RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR "SCENE differs between the depth in place and its copy "
                      "(${OUT}/scene-copy0.png, ${OUT}/scene-copy1.png)")
endif()
if(NO_BUFFER)
  message(STATUS "rd_fog_depth_copy_dump: SCENE identical with the depth in place and its copy")
  return()
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/scene-copy0.png"
                        "${OUT}/scene-copybuffer.png" RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR "SCENE differs between the depth in place and its words through a buffer "
                      "(${OUT}/scene-copy0.png, ${OUT}/scene-copybuffer.png)")
endif()
message(STATUS "rd_fog_depth_copy_dump: SCENE identical with the depth in place, its copy and "
               "its words through a buffer")
