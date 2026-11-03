# rd_shadow_merge_ab: the shadow volumes drawn in the scene's open pass
# (port/render/rd_shadow_merge.c; docs/port/WORK_BUFFERS.md, "The shadow
# volumes in the scene's pass") against their own pass, byte for byte.  For
# every dump the list names (tools/shadow_merge_dumps.txt: vustage_dumps.txt's
# dumps, the PowerVR phone's F12 frames and the heavy stage 6 room of 2026-11-02, read as
# rd_aura_box_ab.cmake reads its list), rd_replay_tool's --pass-dump with
# --stats twice under ICO_RD_BLEND=fixed and ICO_RD_STORE_POISON=1, with
# ICO_RD_SHADOW_MERGE=0 (the volumes' own pass) and ICO_RD_SHADOW_MERGE=1, at
# two settings:
#   phoneauto the phone's Resolution Auto (2364x1080, Full height, Full
#             pixel, aspect 2.189, presented at 2364x1080) under the MoltenVK
#             fakes (a tile-based class: the local fog, DATE through the
#             stencil, the pass area and the passes cut as the phone has
#             them) and ICO_RD_VU_PREPASS=0; a dump drawn narrower than the
#             aspect cannot show it and is skipped there;
#   enh2x     Enhanced 2x at 4:3, presented at 1280x960, the desktop class.
# Every PNG the first run writes (SCENE, the shadow levels, PRESENT and every
# other named target) must be the second's byte for byte; the first run must
# say "shadow volumes in the scene's pass off: 0 of R counts, 0 passes
# armed; 0 count targets' clears not drawn", the second "on: M of R counts";
# the second's draws must be the first's and its passes the first's less M
# (the volumes' pass, one a reset taken; the count targets' clears it leaves
# undrawn are drawn here with the poison's 0x5A, so a reader of them would
# change a picture); some dump must take a reset at some setting (else the
# gate saw nothing).  A missing list or dump is named and skipped; no dump read, or
# no device: SKIP (77).
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
set(env_phoneauto ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0)
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x "")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
# the outputs are matched with their ";" made "," (a CMake list would split there)
set(mergeRe "stats: shadow volumes in the scene's pass (on|off): ([0-9]+) of ([0-9]+) counts, ([0-9]+) passes armed[;,] ([0-9]+) count targets' clears not drawn")
set(failures 0)
set(taken 0)
set(compared 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(dirOff "${OUT}/${index}-${s}-off")
    set(dirOn "${OUT}/${index}-${s}-on")
    file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
    file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_STORE_POISON=1
                            ICO_RD_SHADOW_MERGE=0 ${env_${s}} "${TOOL}" "${dump}" - --pass-dump
                            "${dirOff}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOff OUTPUT_VARIABLE outOff ERROR_VARIABLE errOff)
    if(rcOff EQUAL 77)
      message(STATUS "SKIP: no device: ${errOff}")
      return()
    endif()
    if(NOT rcOff EQUAL 0 AND "${errOff}" MATCHES "cannot show")
      message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_STORE_POISON=1
                            ICO_RD_SHADOW_MERGE=1 ${env_${s}} "${TOOL}" "${dump}" - --pass-dump
                            "${dirOn}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
    if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOff} (own pass) and ${rcOn} (scene's pass):\n"
                         "${errOff}\n${errOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    string(REPLACE ";" "," flatOff "${outOff}")
    string(REPLACE ";" "," flatOn "${outOn}")
    set(ok TRUE)
    foreach(run Off On)
      if(NOT flat${run} MATCHES "${drawsRe}")
        message(SEND_ERROR "${dump} ${s}: no stats line:\n${out${run}}")
        set(ok FALSE)
        break()
      endif()
      set(draws${run} "${CMAKE_MATCH_1}")
      set(passes${run} "${CMAKE_MATCH_2}")
      if(NOT flat${run} MATCHES "${mergeRe}")
        message(SEND_ERROR "${dump} ${s}: no shadow volumes line:\n${out${run}}")
        set(ok FALSE)
        break()
      endif()
      set(state${run} "${CMAKE_MATCH_1}")
      set(merged${run} "${CMAKE_MATCH_2}")
      set(counts${run} "${CMAKE_MATCH_3}")
      set(armed${run} "${CMAKE_MATCH_4}")
      set(clears${run} "${CMAKE_MATCH_5}")
    endforeach()
    if(NOT ok)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    if(NOT stateOff STREQUAL "off" OR NOT mergedOff EQUAL 0 OR NOT armedOff EQUAL 0 OR
       NOT clearsOff EQUAL 0 OR NOT stateOn STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: ICO_RD_SHADOW_MERGE=0 says ${stateOff} with ${mergedOff} "
                         "taken and ${armedOff} armed, ICO_RD_SHADOW_MERGE=1 ${stateOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR wantPasses "${passesOff} - ${mergedOn}")
    if(NOT passesOn EQUAL wantPasses OR NOT drawsOn EQUAL drawsOff OR mergedOn GREATER countsOn)
      message(SEND_ERROR "${dump} ${s}: in the scene's pass ${passesOn} passes and ${drawsOn} "
                         "draws, own pass ${passesOff} and ${drawsOff}: want one pass fewer a "
                         "reset taken (${mergedOn} of ${countsOn}) and the same draws")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR taken "${taken} + ${mergedOn}")
    message(STATUS "${dump} ${s}: ${mergedOn} of ${countsOn} resets in the scene's pass "
                   "(${armedOn} armed), ${clearsOn} count clears left (poisoned here); passes "
                   "${passesOff} -> ${passesOn}")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngsOn npngOn)
    if(NOT npngOn EQUAL npng)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written off, ${npngOn} on")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirOn}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written off only")
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
  message(FATAL_ERROR "rd_shadow_merge_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} could be compared")
  return()
endif()
if(taken EQUAL 0)
  message(FATAL_ERROR "rd_shadow_merge_ab: no dump drew its volumes in the scene's pass at any "
                      "setting: the gate saw nothing")
endif()
message(STATUS "rd_shadow_merge_ab: ${compared} targets the same bytes over ${ndumps} dumps, "
               "${taken} resets in the scene's pass in all")
