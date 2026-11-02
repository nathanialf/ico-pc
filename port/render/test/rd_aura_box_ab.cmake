# rd_aura_box_ab: the aura's feedback read of the picture into FEED128 in two
# passes (port/render/rd_aura_box.c; docs/port/WORK_BUFFERS.md, "The aura's
# feedback read") against the one-pass box, byte for byte.  For every dump
# the list names (tools/aura_box_dumps.txt: tools/vustage_dumps.txt's dumps
# and the PowerVR phone's three F12 frames, read as rd_fades_fold_ab.cmake
# reads its lists), rd_replay_tool's --pass-dump with --stats twice under
# ICO_RD_BLEND=fixed, with ICO_RD_AURA_BOX=0 (one pass) and ICO_RD_AURA_BOX=1
# (two passes), at two settings:
#   phoneauto the phone's Resolution Auto (2364x1080, Full height, Full
#             pixel, aspect 2.189, presented at 2364x1080, the programs' own
#             entries, ICO_RD_VU_PREPASS=0): the box's edges a fraction of a
#             texel off the grid, moving by a 32nd from column to column; a
#             dump drawn narrower than the aspect cannot show it and is
#             skipped there;
#   enh2x     Enhanced 2x at 4:3, presented at 1280x960.
# Every PNG the first run writes (FEED128, SCENE, PRESENT and every other
# named target) must be the second's byte for byte; the one-pass run must
# say "aura box off: 0", the other "aura box on"; the two-pass run's passes
# and draws must be the other's plus its reads in two passes (ROWBOX's pass
# and draw, one each a read); some dump must read in two passes at some
# setting (else the gate saw nothing).  A missing list or dump is named and
# skipped; no dump read, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
set(dumps "")
if(NOT "${LIST}" STREQUAL "" AND EXISTS "${LIST}")
  get_filename_component(listDir "${LIST}" DIRECTORY)
  file(STRINGS "${LIST}" lines)
  foreach(l IN LISTS lines)
    string(STRIP "${l}" l)
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    if(NOT IS_ABSOLUTE "${l}")
      set(l "${listDir}/${l}")
    endif()
    if(l MATCHES "[*]")
      file(GLOB found "${l}")
      list(SORT found)
      if(NOT found)
        message(STATUS "${l}: no dump matches, skipped")
      endif()
      list(APPEND dumps ${found})
    elseif(EXISTS "${l}")
      list(APPEND dumps "${l}")
    else()
      message(STATUS "${l}: no such dump, skipped")
    endif()
  endforeach()
else()
  message(STATUS "${LIST}: no dump list, skipped")
endif()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()

set(settings phoneauto enh2x)
set(args_phoneauto --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
                   --present 2364x1080)
set(env_phoneauto "ICO_RD_VU_PREPASS=0")
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x "")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(boxRe "stats: aura box (on|off): ([0-9]+) feedback reads in two passes")
set(failures 0)
set(reads 0)
set(compared 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(dirOff "${OUT}/${index}-${s}-off")
    set(dirOn "${OUT}/${index}-${s}-on")
    file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
    file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_AURA_BOX=0
                            ${env_${s}} "${TOOL}" "${dump}" - --pass-dump "${dirOff}" --stats
                            ${args_${s}}
                    RESULT_VARIABLE rcOff OUTPUT_VARIABLE outOff ERROR_VARIABLE errOff)
    if(rcOff EQUAL 77)
      message(STATUS "SKIP: no device: ${errOff}")
      return()
    endif()
    if(NOT rcOff EQUAL 0 AND "${errOff}" MATCHES "cannot show")
      message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_AURA_BOX=1
                            ${env_${s}} "${TOOL}" "${dump}" - --pass-dump "${dirOn}" --stats
                            ${args_${s}}
                    RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
    if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOff} (one pass) and ${rcOn} (two passes):\n"
                         "${errOff}\n${errOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    string(REPLACE ";" "," flatOff "${outOff}")
    string(REPLACE ";" "," flatOn "${outOn}")
    if(NOT flatOff MATCHES "${drawsRe}")
      message(SEND_ERROR "${dump} ${s}: no stats line:\n${outOff}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(drawsOff "${CMAKE_MATCH_1}")
    set(passesOff "${CMAKE_MATCH_2}")
    if(NOT flatOn MATCHES "${drawsRe}")
      message(SEND_ERROR "${dump} ${s}: no stats line:\n${outOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(drawsOn "${CMAKE_MATCH_1}")
    set(passesOn "${CMAKE_MATCH_2}")
    if(NOT flatOff MATCHES "${boxRe}")
      message(SEND_ERROR "${dump} ${s}: no aura box line:\n${outOff}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(stateOff "${CMAKE_MATCH_1}")
    set(nOff "${CMAKE_MATCH_2}")
    if(NOT flatOn MATCHES "${boxRe}")
      message(SEND_ERROR "${dump} ${s}: no aura box line:\n${outOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(stateOn "${CMAKE_MATCH_1}")
    set(nOn "${CMAKE_MATCH_2}")
    if(NOT stateOff STREQUAL "off" OR NOT nOff EQUAL 0 OR NOT stateOn STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: ICO_RD_AURA_BOX=0 says ${stateOff} with ${nOff} reads "
                         "in two passes, ICO_RD_AURA_BOX=1 ${stateOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR wantPasses "${passesOff} + ${nOn}")
    math(EXPR wantDraws "${drawsOff} + ${nOn}")
    if(NOT passesOn EQUAL wantPasses OR NOT drawsOn EQUAL wantDraws)
      message(SEND_ERROR "${dump} ${s}: two passes ${passesOn} passes and ${drawsOn} draws, one "
                         "pass ${passesOff} and ${drawsOff}: want one of each more a read "
                         "(${nOn} reads)")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR reads "${reads} + ${nOn}")
    message(STATUS "${dump} ${s}: ${nOn} reads in two passes; passes ${passesOff} -> "
                   "${passesOn}, draws ${drawsOff} -> ${drawsOn}")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngsOn npngOn)
    if(NOT npngOn EQUAL npng)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written in one pass, ${npngOn} in two")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirOn}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written in one pass only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs: it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_aura_box_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} could be compared")
  return()
endif()
if(reads EQUAL 0)
  message(FATAL_ERROR "rd_aura_box_ab: no dump read the picture in two passes at any setting: "
                      "the gate saw nothing")
endif()
message(STATUS "rd_aura_box_ab: ${compared} targets the same bytes over ${ndumps} dumps, ${reads} "
               "reads in two passes in all")
