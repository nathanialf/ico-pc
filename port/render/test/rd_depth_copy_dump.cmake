# rd_fog_depth_copy_dump: frames with the depth fog (list 4's RD_POST_FOG on
# SCENE reading SCENE's depth, then list 8's aura on the same depth: AURA_WORK
# with SCENE's depth, the FEED128 rows, the paste onto SCENE), committed
# fixtures (port/render/test/fixtures, docs/port/TESTING_FIXTURES.md) named by
# LIST, each replayed by rd_replay_tool three times, the fog sampling the depth
# in place (ICO_RD_DEPTH_COPY=0), a copy of it (=1, the path of tile-based GPUs
# and D24S8) and reading its words through a buffer (ICO_RD_FOG_PATH=buffer,
# rd_fog_path.c): SCENE must come out byte for byte the same.  -DNO_BUFFER=1
# leaves the buffer out (D24S8: the shader turns the 24-bit step into a float
# itself, which may round a depth one step of the float apart from the
# device's own conversion, and a pixel whose step sits on an index boundary
# then takes the index next to it).
# The path ran: each run's stats name the path the environment asked for
# (rd_fog_path.c rd__fog_path_init: ICO_RD_DEPTH_COPY 0 inplace, else copy;
# ICO_RD_FOG_PATH=buffer buffer), the run through the copy shows the fixture's
# declared quantities (the list line's words: the aura's paste drawn), and one
# more run through the copy with its reads sabotaged (ICO_RD_FOG_SABOTAGE=copy:
# the shader's depth reads give 0, the fog's index 0 everywhere) must give a
# SCENE that differs from the copy's: the fog drew, and what it read of the
# depth is in the picture.
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<dir>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<list> -DOUT=<dir>
# [-DEXTRA_ENV=VAR=VALUE] [-DNO_BUFFER=1]
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
rd_fixture_list("${LIST}" fixtures)
file(MAKE_DIRECTORY "${OUT}")
set(env "")
if(EXTRA_ENV)
  set(env "${EXTRA_ENV}")
endif()
set(modes 0 1 buffer sabotage)
if(NO_BUFFER)
  set(modes 0 1 sabotage)
endif()
set(failures 0)
foreach(fx IN LISTS fixtures)
  rd_fixture_list_props(${fx})
  rd_fixture_expand(${fx} "${OUT}/fixtures" dump)
  set(o "${OUT}/${fx}")
  file(MAKE_DIRECTORY "${o}")
  foreach(mode ${modes})
    set(png "${o}/scene-copy${mode}.png")
    file(REMOVE "${png}")
    if(mode STREQUAL "buffer")
      set(path "ICO_RD_FOG_PATH=buffer")
      set(want buffer)
    elseif(mode STREQUAL "sabotage")
      set(path "ICO_RD_DEPTH_COPY=1" "ICO_RD_FOG_SABOTAGE=copy")
      set(want copy)
    elseif(mode STREQUAL "1")
      set(path "ICO_RD_DEPTH_COPY=1")
      set(want copy)
    else()
      set(path "ICO_RD_DEPTH_COPY=0")
      set(want inplace)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env} ${path}
                            "${TOOL}" "${dump}" "${png}" --target SCENE --stats
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: rd_replay_tool found no usable device")
      return()
    endif()
    if(NOT rc EQUAL 0 OR NOT EXISTS "${png}")
      message(FATAL_ERROR "${fx}: rd_replay_tool (${path}) exited ${rc}:\n${out}\n${err}")
    endif()
    rd_fixture_quantity(fog_path "${out}" got)
    if(NOT got STREQUAL want)
      message(SEND_ERROR "${fx} (${path}): the fog's path is '${got}', want ${want}")
      math(EXPR failures "${failures} + 1")
    endif()
    if(mode STREQUAL "1")
      rd_fixture_require(${fx} "${out}" draws aura_pastes_whole)
    endif()
  endforeach()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${o}/scene-copy1.png"
                          "${o}/scene-copysabotage.png" RESULT_VARIABLE same)
  if(same EQUAL 0)
    message(SEND_ERROR "${fx}: SCENE is the same with the copy's depth reads sabotaged: the fog "
                       "did not draw, or what it read of the depth is not in the picture")
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "${fx}: the fog's depth read is in SCENE (the sabotaged reads change it)")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${o}/scene-copy0.png"
                          "${o}/scene-copy1.png" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    message(SEND_ERROR "${fx}: SCENE differs between the depth in place and its copy "
                       "(${o}/scene-copy0.png, ${o}/scene-copy1.png)")
    math(EXPR failures "${failures} + 1")
  endif()
  if(NOT NO_BUFFER)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${o}/scene-copy0.png"
                            "${o}/scene-copybuffer.png" RESULT_VARIABLE same)
    if(NOT same EQUAL 0)
      message(SEND_ERROR "${fx}: SCENE differs between the depth in place and its words through "
                         "a buffer (${o}/scene-copy0.png, ${o}/scene-copybuffer.png)")
      math(EXPR failures "${failures} + 1")
    endif()
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_fog_depth_copy_dump: ${failures} failures")
endif()
list(LENGTH fixtures n)
if(NO_BUFFER)
  message(STATUS "rd_fog_depth_copy_dump: SCENE identical with the depth in place and its copy "
                 "over ${n} fixtures")
else()
  message(STATUS "rd_fog_depth_copy_dump: SCENE identical with the depth in place, its copy and "
                 "its words through a buffer over ${n} fixtures")
endif()
