# rd_vumerge_ab: the VU draw merge against one GPU draw a VU draw, byte for
# byte (docs/port/VU1_PROGRAMS.md, "The draw merge").  For every dump the
# list names (tools/vustage_dumps.txt, as rd_vustage_ab.cmake reads it), at
# the Original preset at 4:3 and 1x, the Enhanced preset at 4:3 and 2x and
# at 16:9 and 2x, each through the record (the pre-passes on) and through
# the programs' own entries (ICO_RD_VU_PREPASS=0: what a tile-based GPU and
# Direct3D 11 draw), rd_replay_tool's --pass-dump with --stats twice under
# ICO_RD_BLEND=fixed: with ICO_RD_VU_MERGE=0 (every VU draw a GPU draw of its
# own, the plain builds reading VuCB and VuBoneCB as uniform blocks) and with
# the merge (consecutive draws with the same state one indexed draw, the
# merge builds reading every draw's blocks from the ring through the run's
# table).  Every named target's PNG must be equal byte for byte; the merged
# run must say "merge on" and the other "merge off" with nothing merged; the
# merged run's GPU draws must be the other's less what its runs joined
# (draws merged less the GPU draws they took), and the GPU draws over every
# dump and setting fewer.  Each pair's draws are printed.  A missing list or
# dump is named and skipped; no dump read, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
if(NOT EXISTS "${LIST}")
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(dumps "")
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${listDir}/${l}")
  endif()
  if(l MATCHES "[*]")
    file(GLOB matched "${l}")
    list(SORT matched)
    if(matched)
      list(APPEND dumps ${matched})
    else()
      message(STATUS "${l}: no dump matches, skipped")
    endif()
  elseif(EXISTS "${l}")
    list(APPEND dumps "${l}")
  else()
    message(STATUS "${l}: no such dump, skipped")
  endif()
endforeach()
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()

set(settings "orig" "enh2x" "enh2x169")
set(args_orig --aspect 4:3)
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x)
set(args_enh2x169 --enhanced --aspect 16:9 --resolution 2x)
# the record (the default) and the programs' own entries
set(paths "rec" "own")
set(env_rec "")
set(env_own "ICO_RD_VU_PREPASS=0")
set(drawsRe "stats: ([0-9]+) draws, [0-9]+ passes")
set(mergeRe "stats: VU merge: ([0-9]+) draws in ([0-9]+) GPU draws \\(([0-9]+) merged into ([0-9]+), ([0-9]+) alone, merge (on|off)\\)")
set(compared 0)
set(failures 0)
set(drawsOff 0)
set(drawsOn 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(path IN LISTS paths)
    foreach(s IN LISTS settings)
      set(dirOff "${OUT}/${index}-${path}-${s}-off")
      set(dirOn "${OUT}/${index}-${path}-${s}-on")
      file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
      file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_VU_MERGE=0
                              ${env_${path}} "${TOOL}" "${dump}" /dev/null --pass-dump "${dirOff}"
                              --stats ${args_${s}}
                      RESULT_VARIABLE rcOff OUTPUT_VARIABLE outOff ERROR_VARIABLE errOff)
      if(rcOff EQUAL 77)
        message(STATUS "SKIP: no device: ${errOff}")
        return()
      endif()
      if(NOT rcOff EQUAL 0 AND "${errOff}" MATCHES "cannot show")
        # a dump drawn at 4:3 cannot be replayed at 16:9: that setting is skipped for it
        message(STATUS "${dump} ${path} ${s}: skipped (${errOff})")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ${env_${path}}
                              "${TOOL}" "${dump}" /dev/null --pass-dump "${dirOn}" --stats
                              ${args_${s}}
                      RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
      if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
        message(SEND_ERROR "${dump} ${path} ${s}: exit ${rcOff} (one GPU draw a draw) and "
                           "${rcOn} (merged):\n${errOff}\n${errOn}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      # the tool's lines with their ";" taken out (a ";" would split a list item)
      string(REPLACE ";" "," flatOff "${outOff}")
      string(REPLACE ";" "," flatOn "${outOn}")
      # a Direct3D 11 debug-layer error in either run (the runtime drops the call it names;
      # tools/ci_d3d11_warp.sh runs this script on WARP with ICO_D3D11_DEBUG=1)
      if("${errOff}${errOn}" MATCHES "rhi_d3d11 debug (error|corruption)[^\n]*")
        message(SEND_ERROR "${dump} ${path} ${s}: ${CMAKE_MATCH_0}")
        math(EXPR failures "${failures} + 1")
      endif()
      if(NOT flatOff MATCHES "${drawsRe}")
        message(SEND_ERROR "${dump} ${path} ${s}: no draw count:\n${outOff}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(nOff "${CMAKE_MATCH_1}")
      if(NOT flatOn MATCHES "${drawsRe}")
        message(SEND_ERROR "${dump} ${path} ${s}: no draw count:\n${outOn}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(nOn "${CMAKE_MATCH_1}")
      if(NOT flatOff MATCHES "${mergeRe}" OR NOT CMAKE_MATCH_6 STREQUAL "off" OR
         NOT CMAKE_MATCH_1 EQUAL 0)
        message(SEND_ERROR "${dump} ${path} ${s}: ICO_RD_VU_MERGE=0 did not draw one GPU draw "
                           "a VU draw:\n${outOff}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      if(NOT flatOn MATCHES "${mergeRe}" OR NOT CMAKE_MATCH_6 STREQUAL "on")
        message(SEND_ERROR "${dump} ${path} ${s}: the merge is not on:\n${outOn}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(joined "${CMAKE_MATCH_3}")
      set(took "${CMAKE_MATCH_4}")
      math(EXPR want "${nOff} - (${joined} - ${took})")
      message(STATUS "${dump} ${path} ${s}: ${nOff} GPU draws one a draw, ${nOn} merged "
                     "(${joined} draws merged into ${took})")
      math(EXPR drawsOff "${drawsOff} + ${nOff}")
      math(EXPR drawsOn "${drawsOn} + ${nOn}")
      if(NOT nOn EQUAL want)
        message(SEND_ERROR "${dump} ${path} ${s}: ${nOn} GPU draws merged, ${want} expected "
                           "(${nOff} less ${joined} merged into ${took})")
        math(EXPR failures "${failures} + 1")
      endif()
      file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
      list(LENGTH pngs npng)
      if(npng EQUAL 0)
        message(SEND_ERROR "${dump} ${path} ${s}: no target written")
        math(EXPR failures "${failures} + 1")
      endif()
      foreach(png IN LISTS pngs)
        if(NOT EXISTS "${dirOn}/${png}")
          message(SEND_ERROR "${dump} ${path} ${s}: ${png} written one GPU draw a draw only")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                                "${dirOn}/${png}" RESULT_VARIABLE same)
        math(EXPR compared "${compared} + 1")
        if(NOT same EQUAL 0)
          message(SEND_ERROR "${dump} ${path} ${s}: ${png} differs between one GPU draw a draw "
                             "and the merge")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vumerge_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} could be compared")
  return()
endif()
if(NOT drawsOn LESS drawsOff)
  message(FATAL_ERROR "rd_vumerge_ab: ${drawsOn} GPU draws merged against ${drawsOff} one a "
                      "draw: the merge joined nothing")
endif()
message(STATUS "rd_vumerge_ab: ${compared} targets equal over ${ndumps} dumps at 3 settings "
               "and 2 paths, GPU draws ${drawsOff} one a VU draw, ${drawsOn} merged")
