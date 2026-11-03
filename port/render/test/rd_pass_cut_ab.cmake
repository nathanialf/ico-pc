# rd_pass_cut_ab: the passes cut (port/render/rd_pass_cut.c; docs/port/
# WORK_BUFFERS.md, "Passes cut"), each on against all three off, byte for byte.
# For every dump the list names (tools/pass_cut_dumps.txt: tools/vustage_dumps.txt's
# dumps, the PowerVR phone's three F12 frames and its rc3 frame, read as
# rd_pass_area_ab.cmake reads its list), rd_replay_tool's --pass-dump with
# --stats under ICO_RD_BLEND=fixed, the store plan (ICO_RD_STORE_PLAN=1) and
# its poison (ICO_RD_STORE_POISON=1: a depth or stencil load the plan leaves
# undefined clears to garbage, lavapipe itself keeps an attachment's contents
# whatever its store op says, so a store dropped that a later pass reads
# shows), five times: every cut off (ICO_RD_CLEAR_CARRY=0, ICO_RD_CLEAR_SKIP=0,
# ICO_RD_LAST_STORE=0), each one alone on, and all three on, at two settings:
#   phone     the phone's Resolution Auto (2364x1080, Full height, Full
#             pixel, aspect 2.189, presented at 2364x1080), its paths: the
#             programs' own entries (ICO_RD_VU_PREPASS=0), the fog's depth
#             read in the scene pass (ICO_RD_FOG_PATH=local), DATE through
#             the stencil bit (ICO_RD_DATE_PATH=stencil); a dump drawn
#             narrower than the aspect cannot show it and is skipped there;
#   enh2x     Enhanced 2x at 4:3, presented at 1280x960.
# Every PNG the off run writes (SCENE, DISPLAY, PRESENT and every other named
# target; a target whose clear was skipped is read back with its clear drawn
# first, as any read of it is) must be each other run's byte for byte; the off
# run must cut nothing; each run's draws must be the off run's; the passes the
# off run's less the clears carried and the clears skipped; the depth stores
# dropped the off run's plus those dropped at the last touch, less at most one a
# clear carried or skipped (a clear's own pass whose depth store the plan dropped
# goes with the carry: queen rd-00100 at enh2x, 'a replay of 4 passes (2 with a
# depth)' and 2 dropped off, '2 passes (1 with a depth)' and 1 dropped with its two
# clears carried).  Over the phone's frames (frame-20261028-* and
# frame-20261102-*) at the phone's setting, each cut must cut something (the
# reduction's DISPLAY clear, AURA_WORK's clear, the fog's pass's depth store),
# else the gate saw nothing.  The crack fill after lists 4-6 reads SCENE's depth
# at list 7 on every such frame, and the store plan keeps the fog's pass's depth
# store for that read (rd_store_plan.c, PlanDepth.late), so the last-touch drop
# is counted on two more runs of those frames, every cut off and the last store
# alone on, both with ICO_RD_CRACK_FILL=0 (offnf, lastnf: compared with each
# other as the five are with off).  A missing list or
# dump is named and skipped; no dump read, or no device: SKIP (77).
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

set(settings phone enh2x)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
               --present 2364x1080)
set(env_phone ICO_RD_VU_PREPASS=0 ICO_RD_FOG_PATH=local ICO_RD_DATE_PATH=stencil)
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x "")
# ICO_RD_FADES_FOLD=0: list 11's last sprites drawn into SCENE, as the pass-dump gate draws them.
# With the fold (its brightness step above 1x too, rd_fades_fold.c) list 11's pass goes, and the
# reduction's DISPLAY clear is then taken by the reduction's own pass with no barrier in between:
# nothing is left for the carry to cut on the phone's frames (rc3's v15677 at the phone's setting:
# 39 passes and 1 clear carried with the sprites drawn, 38 and 0 with the fold)
set(common ICO_RD_BLEND=fixed ICO_RD_STORE_PLAN=1 ICO_RD_STORE_POISON=1 ICO_RD_FADES_FOLD=0)
# the runs: name and the three switches (carry, skip, last)
set(runs off carry skip last all)
set(runsNoFill offnf lastnf)
set(sw_off 0 0 0)
set(sw_carry 1 0 0)
set(sw_skip 0 1 0)
set(sw_last 0 0 1)
set(sw_all 1 1 1)
set(sw_offnf 0 0 0)
set(sw_lastnf 0 0 1)
# each run's crack fill switch and the run it is compared with
foreach(r IN LISTS runs)
  set(fill_${r} 1)
  set(ref_${r} off)
endforeach()
foreach(r IN LISTS runsNoFill)
  set(fill_${r} 0)
  set(ref_${r} offnf)
endforeach()
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(storesRe "stats: stores: ([0-9]+) depth and ([0-9]+) colour stores dropped")
set(cutRe "stats: pass cut: clear carry (on|off): ([0-9]+) clears taken by the next pass, clear skip (on|off): ([0-9]+) clears skipped, last store (on|off): ([0-9]+) depth stores dropped at the last touch")
set(failures 0)
set(compared 0)
set(phoneCompared 0)
set(phoneCarried 0)
set(phoneSkipped 0)
set(phoneLast 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  get_filename_component(dumpName "${dump}" NAME)
  set(phoneFrame FALSE)
  if(dumpName MATCHES "^frame-2026(1028|1102)-")
    set(phoneFrame TRUE)
  endif()
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    set(theRuns ${runs})
    if(phoneFrame AND s STREQUAL "phone")
      list(APPEND theRuns ${runsNoFill})
    endif()
    foreach(run IN LISTS theRuns)
      if(skipped)
        break()
      endif()
      list(GET sw_${run} 0 carry)
      list(GET sw_${run} 1 skip)
      list(GET sw_${run} 2 last)
      set(dir "${OUT}/${index}-${s}-${run}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      set(fillEnv "")
      if(NOT fill_${run})
        set(fillEnv ICO_RD_CRACK_FILL=0)
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ICO_RD_CLEAR_CARRY=${carry}
                              ICO_RD_CLEAR_SKIP=${skip} ICO_RD_LAST_STORE=${last} ${fillEnv}
                              ${env_${s}}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND run STREQUAL "off" AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} ${run}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${drawsRe}")
        message(SEND_ERROR "${dump} ${s} ${run}: no stats line:\n${out}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(draws_${run} "${CMAKE_MATCH_1}")
      set(passes_${run} "${CMAKE_MATCH_2}")
      if(NOT flat MATCHES "${storesRe}")
        message(SEND_ERROR "${dump} ${s} ${run}: no stores line:\n${out}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(depth_${run} "${CMAKE_MATCH_1}")
      if(NOT flat MATCHES "${cutRe}")
        message(SEND_ERROR "${dump} ${s} ${run}: no pass cut line:\n${out}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(carried_${run} "${CMAKE_MATCH_2}")
      set(skipped_${run} "${CMAKE_MATCH_4}")
      set(last_${run} "${CMAKE_MATCH_6}")
      set(words "${CMAKE_MATCH_1} ${CMAKE_MATCH_3} ${CMAKE_MATCH_5}")
      set(want "")
      foreach(v IN ITEMS ${carry} ${skip} ${last})
        if(v)
          string(APPEND want " on")
        else()
          string(APPEND want " off")
        endif()
      endforeach()
      string(STRIP "${want}" want)
      if(NOT words STREQUAL want)
        message(SEND_ERROR "${dump} ${s} ${run}: the cuts say ${words}, ${want} expected")
        math(EXPR failures "${failures} + 1")
      endif()
      message(STATUS "${dump} ${s} ${run}: ${passes_${run}} passes, ${draws_${run}} draws, "
                     "${depth_${run}} depth stores dropped; ${carried_${run}} clears carried, "
                     "${skipped_${run}} skipped, ${last_${run}} stores dropped at the last touch")
      set(ref "${ref_${run}}")
      if(run STREQUAL ref)
        if(NOT carried_${ref} EQUAL 0 OR NOT skipped_${ref} EQUAL 0 OR NOT last_${ref} EQUAL 0)
          message(SEND_ERROR "${dump} ${s} ${run}: every cut off cut ${carried_${ref}}, "
                             "${skipped_${ref}} and ${last_${ref}}")
          math(EXPR failures "${failures} + 1")
        endif()
        file(GLOB pngs RELATIVE "${dir}" "${dir}/*.png")
        list(LENGTH pngs npng)
        if(npng EQUAL 0)
          message(SEND_ERROR "${dump} ${s}: no target written")
          math(EXPR failures "${failures} + 1")
        endif()
        continue()
      endif()
      math(EXPR wantPasses "${passes_${ref}} - ${carried_${run}} - ${skipped_${run}}")
      math(EXPR wantDepth "${depth_${ref}} + ${last_${run}}")
      math(EXPR leastDepth "${wantDepth} - ${carried_${run}} - ${skipped_${run}}")
      if(NOT draws_${run} EQUAL draws_${ref} OR NOT passes_${run} EQUAL wantPasses OR
         depth_${run} GREATER wantDepth OR depth_${run} LESS leastDepth)
        message(SEND_ERROR "${dump} ${s} ${run}: ${passes_${run}} passes, ${draws_${run}} draws, "
                           "${depth_${run}} depth stores dropped; want ${wantPasses}, "
                           "${draws_${ref}} and ${leastDepth} to ${wantDepth}")
        math(EXPR failures "${failures} + 1")
      endif()
      if(phoneFrame AND s STREQUAL "phone" AND run STREQUAL "all")
        math(EXPR phoneCompared "${phoneCompared} + 1")
        math(EXPR phoneCarried "${phoneCarried} + ${carried_all}")
        math(EXPR phoneSkipped "${phoneSkipped} + ${skipped_all}")
      endif()
      if(phoneFrame AND s STREQUAL "phone" AND run STREQUAL "lastnf")
        math(EXPR phoneLast "${phoneLast} + ${last_lastnf}")
      endif()
      set(dirOff "${OUT}/${index}-${s}-${ref}")
      file(GLOB pngsOn RELATIVE "${dir}" "${dir}/*.png")
      list(LENGTH pngsOn npngOn)
      if(NOT npngOn EQUAL npng)
        message(SEND_ERROR "${dump} ${s}: ${npng} targets written with every cut off (${ref}), "
                           "${npngOn} with ${run}")
        math(EXPR failures "${failures} + 1")
      endif()
      foreach(png IN LISTS pngs)
        if(NOT EXISTS "${dir}/${png}")
          message(SEND_ERROR "${dump} ${s}: ${png} written with every cut off only (${run}, "
                             "against ${ref})")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        math(EXPR compared "${compared} + 1")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                                "${dir}/${png}" RESULT_VARIABLE same)
        if(NOT same EQUAL 0)
          message(SEND_ERROR "${dump} ${s} ${run}: ${png} differs from ${ref}: it must be the "
                             "same bytes")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_pass_cut_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} could be compared")
  return()
endif()
if(phoneCompared GREATER 0 AND
   (phoneCarried EQUAL 0 OR phoneSkipped EQUAL 0 OR phoneLast EQUAL 0))
  message(FATAL_ERROR "rd_pass_cut_ab: the phone's ${phoneCompared} frames at its setting "
                      "carried ${phoneCarried} clears, skipped ${phoneSkipped} and dropped "
                      "${phoneLast} stores at the last touch (without the crack fill): each must "
                      "cut something")
endif()
message(STATUS "rd_pass_cut_ab: ${compared} targets the same bytes over ${ndumps} dumps; the "
               "phone's frames: ${phoneCarried} clears carried, ${phoneSkipped} skipped, "
               "${phoneLast} stores dropped at the last touch without the crack fill")
