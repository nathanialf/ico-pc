# rd_vufuse_ab: the fused scissor pair against code 36's cut and kick draws
# under their own pipelines, byte for byte (docs/port/VU1_PROGRAMS.md, "The
# fused scissor pair").  For every dump the lists name (tools/vustage_dumps.txt
# and tools/vufuse_dumps.txt, read as rd_vumerge_ab.cmake reads them), under
# ICO_RD_BLEND=fixed and the draw merge on, rd_replay_tool's --pass-dump with
# --stats twice: with ICO_RD_VU_FUSE=0 (every kick under its own pipeline, as
# before the fuse) and with the fuse (a pair the blend form and the plans allow
# drawn with the kick under its cut's pipeline, its pixels unblended), at four
# settings:
#   phone    the PowerVR phone's profile: the programs' own entries
#            (ICO_RD_VU_PREPASS=0, a tile-based GPU's path), Enhanced 2x at the
#            dump's 2.189 aspect, presented at 2364x1080 (a dump narrower than
#            2.189 cannot show it: skipped for it);
#   own43    the programs' own entries, Enhanced 4:3 2x;
#   rec43    the record (the pre-passes on, the desktop's path), Enhanced 4:3 2x;
#   rec169   the record, Enhanced 16:9 2x.
# Every named target's PNG (PRESENT.png with --present) must be equal byte for
# byte; the fused run must say "fuse on" and the other "fuse off" with no pair
# fused, both runs must count the same pairs (fused, kept apart and under one
# pipeline already), the fused run's GPU draws must be the other's less what
# its merge joined beyond the other's (draws merged less the GPU draws they
# took, from each run's "VU merge:" line), and at every setting where pairs
# were counted some must be fused, with fewer GPU draws.  Each pair's draws and
# pairs are printed.  A missing list or dump is named and skipped; no dump
# read, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DEXTRA=<a second dump list>
# -DOUT=<a folder the test may write>
set(dumps "")
foreach(list IN ITEMS "${LIST}" "${EXTRA}")
  if(list STREQUAL "" OR NOT EXISTS "${list}")
    message(STATUS "${list}: no dump list, skipped")
    continue()
  endif()
  get_filename_component(listDir "${list}" DIRECTORY)
  file(STRINGS "${list}" lines)
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
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} or ${EXTRA} is here")
  return()
endif()

set(settings "phone" "own43" "rec43" "rec169")
set(args_phone --enhanced --resolution 2x --aspect 2.189 --present 2364x1080)
set(env_phone "ICO_RD_VU_PREPASS=0")
set(args_own43 --enhanced --resolution 2x --aspect 4:3)
set(env_own43 "ICO_RD_VU_PREPASS=0")
set(args_rec43 --enhanced --resolution 2x --aspect 4:3)
set(env_rec43 "")
set(args_rec169 --enhanced --resolution 2x --aspect 16:9)
set(env_rec169 "")
set(drawsRe "stats: ([0-9]+) draws, [0-9]+ passes")
set(mergeRe "stats: VU merge: [0-9]+ draws in [0-9]+ GPU draws \\(([0-9]+) merged into ([0-9]+), [0-9]+ alone, merge (on|off)\\)")
set(fuseRe "stats: VU fuse: ([0-9]+) pairs fused, ([0-9]+) kept apart \\(blend form ([0-9]+), plans differ ([0-9]+), passes [0-9]+, dst/DATE [0-9]+, no planes [0-9]+, no merge build [0-9]+, fuse off ([0-9]+)\\), ([0-9]+) under one pipeline already, fuse (on|off)")
set(compared 0)
set(failures 0)
foreach(s IN LISTS settings)
  set(pairs_${s} 0)
  set(fused_${s} 0)
  set(drawsOff_${s} 0)
  set(drawsOn_${s} 0)
endforeach()
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(dirOff "${OUT}/${index}-${s}-off")
    set(dirOn "${OUT}/${index}-${s}-on")
    file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
    file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_VU_FUSE=0
                            ${env_${s}} "${TOOL}" "${dump}" - --pass-dump "${dirOff}" --stats
                            ${args_${s}}
                    RESULT_VARIABLE rcOff OUTPUT_VARIABLE outOff ERROR_VARIABLE errOff)
    if(rcOff EQUAL 77)
      message(STATUS "SKIP: no device: ${errOff}")
      return()
    endif()
    if(NOT rcOff EQUAL 0 AND "${errOff}" MATCHES "cannot show")
      # a dump drawn narrower than the setting's aspect: that setting is skipped for it
      message(STATUS "${dump} ${s}: skipped (${errOff})")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ${env_${s}} "${TOOL}"
                            "${dump}" - --pass-dump "${dirOn}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
    if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOff} (kicks apart) and ${rcOn} (fused):\n"
                         "${errOff}\n${errOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    # the tool's lines with their ";" taken out (a ";" would split a list item)
    string(REPLACE ";" "," flatOff "${outOff}")
    string(REPLACE ";" "," flatOn "${outOn}")
    set(ok TRUE)
    foreach(run Off On)
      if(NOT flat${run} MATCHES "${drawsRe}")
        message(SEND_ERROR "${dump} ${s}: no draw count:\n${out${run}}")
        set(ok FALSE)
        break()
      endif()
      set(n${run} "${CMAKE_MATCH_1}")
      if(NOT flat${run} MATCHES "${mergeRe}" OR NOT CMAKE_MATCH_3 STREQUAL "on")
        message(SEND_ERROR "${dump} ${s}: the draw merge is not on:\n${out${run}}")
        set(ok FALSE)
        break()
      endif()
      math(EXPR saved${run} "${CMAKE_MATCH_1} - ${CMAKE_MATCH_2}")
      if(NOT flat${run} MATCHES "${fuseRe}")
        message(SEND_ERROR "${dump} ${s}: no VU fuse line:\n${out${run}}")
        set(ok FALSE)
        break()
      endif()
      set(fused${run} "${CMAKE_MATCH_1}")
      set(kept${run} "${CMAKE_MATCH_2}")
      set(blend${run} "${CMAKE_MATCH_3}")
      set(plans${run} "${CMAKE_MATCH_4}")
      set(off${run} "${CMAKE_MATCH_5}")
      set(same${run} "${CMAKE_MATCH_6}")
      set(state${run} "${CMAKE_MATCH_7}")
    endforeach()
    if(NOT ok)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    if(NOT stateOff STREQUAL "off" OR NOT fusedOff EQUAL 0 OR NOT stateOn STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: ICO_RD_VU_FUSE=0 says fuse ${stateOff} with ${fusedOff} "
                         "pairs fused, the default fuse ${stateOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR pairsOff "${fusedOff} + ${keptOff} + ${sameOff}")
    math(EXPR pairsOn "${fusedOn} + ${keptOn} + ${sameOn}")
    if(NOT pairsOff EQUAL pairsOn OR NOT sameOff EQUAL sameOn)
      message(SEND_ERROR "${dump} ${s}: ${pairsOff} pairs (${sameOff} under one pipeline) with "
                         "the kicks apart, ${pairsOn} (${sameOn}) fused")
      math(EXPR failures "${failures} + 1")
    endif()
    # the same VU draws either way: the GPU draws differ by what the merge joined
    math(EXPR want "${nOff} - (${savedOn} - ${savedOff})")
    message(STATUS "${dump} ${s}: ${pairsOn} pairs, ${fusedOn} fused, ${keptOn} kept apart "
                   "(blend form ${blendOn}, plans differ ${plansOn}), ${sameOn} under one "
                   "pipeline already; GPU draws ${nOff} kicks apart, ${nOn} fused")
    if(NOT nOn EQUAL want)
      message(SEND_ERROR "${dump} ${s}: ${nOn} GPU draws fused, ${want} expected (${nOff} less "
                         "the merge's ${savedOn} joined draws against ${savedOff})")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR pairs_${s} "${pairs_${s}} + ${pairsOn} - ${sameOn}")
    math(EXPR fused_${s} "${fused_${s}} + ${fusedOn}")
    math(EXPR drawsOff_${s} "${drawsOff_${s}} + ${nOff}")
    math(EXPR drawsOn_${s} "${drawsOn_${s}} + ${nOn}")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirOn}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written with the kicks apart only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      math(EXPR compared "${compared} + 1")
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs between the kicks apart and fused")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
foreach(s IN LISTS settings)
  message(STATUS "rd_vufuse_ab ${s}: ${fused_${s}} of ${pairs_${s}} pairs with ABE off fused, "
                 "GPU draws ${drawsOff_${s}} kicks apart, ${drawsOn_${s}} fused")
  if(pairs_${s} GREATER 0 AND NOT fused_${s} GREATER 0)
    message(SEND_ERROR "rd_vufuse_ab ${s}: ${pairs_${s}} pairs with ABE off and none fused")
    math(EXPR failures "${failures} + 1")
  elseif(fused_${s} GREATER 0 AND NOT drawsOn_${s} LESS drawsOff_${s})
    message(SEND_ERROR "rd_vufuse_ab ${s}: ${fused_${s}} pairs fused and the GPU draws not fewer "
                       "(${drawsOn_${s}} against ${drawsOff_${s}})")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vufuse_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} or ${EXTRA} could be compared")
  return()
endif()
message(STATUS "rd_vufuse_ab: ${compared} targets equal over ${ndumps} dumps at 4 settings")
