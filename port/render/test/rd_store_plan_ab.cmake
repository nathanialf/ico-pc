# rd_store_plan_ab: the store plan against every depth store kept, byte for byte
# (docs/port/WORK_BUFFERS.md, "The store plan").  For every dump the lists name
# (tools/vustage_dumps.txt and tools/vufuse_dumps.txt, read as rd_vufuse_ab.cmake
# reads them), under ICO_RD_BLEND=fixed and ICO_RD_STORE_POISON=1 (a depth or
# stencil load the plan leaves undefined clears to garbage instead: lavapipe
# itself keeps an attachment's contents whatever its store op says),
# rd_replay_tool's --pass-dump with --stats three times: with ICO_RD_STORE_PLAN=0
# (every store kept), with the plan (ICO_RD_STORE_PLAN=1) and with every depth
# store dropped (ICO_RD_STORE_PLAN=all, a wrong plan), at three settings:
#   phone    the PowerVR phone's profile: the programs' own entries
#            (ICO_RD_VU_PREPASS=0), the fog's depth read in the scene pass and
#            DATE through the stencil bit (ICO_RD_FOG_PATH=local,
#            ICO_RD_DATE_PATH=stencil: the depth copied for the fog, the depth
#            and stencil stored together), Enhanced 2x at the dump's 2.189
#            aspect, presented at 2364x1080 (a dump narrower than 2.189 cannot
#            show it: skipped for it);
#   desk43   the desktop's: the record, Enhanced 4:3 2x;
#   desk169  the desktop's at 16:9 2x (the dumps drawn at 16:9 or wider).
# The planned run's every named target's PNG (PRESENT.png with --present) must
# equal the kept run's byte for byte; the kept run must say "store plan off"
# with no store dropped, the planned run "store plan on" with the same passes
# and passes with a depth, the wrong run "store plan all" with every pass with a
# depth dropped; over every dump and setting the plan must drop depth stores,
# and the wrong plan must change at least one target (proof the poison shows a
# dropped store that is read).  Each run's counts are printed.  A missing list
# or dump is named and skipped; no dump read, or no device: SKIP (77).
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

set(settings "phone" "desk43" "desk169")
set(args_phone --enhanced --resolution 2x --aspect 2.189 --present 2364x1080)
set(env_phone ICO_RD_VU_PREPASS=0 ICO_RD_FOG_PATH=local ICO_RD_DATE_PATH=stencil)
set(args_desk43 --enhanced --resolution 2x --aspect 4:3)
set(env_desk43 "")
set(args_desk169 --enhanced --resolution 2x --aspect 16:9)
set(env_desk169 "")
set(runs "off" "on" "all")
set(plan_off 0)
set(plan_on 1)
set(plan_all all)
set(storesRe "stats: stores: ([0-9]+) depth and ([0-9]+) colour stores dropped a replay of ([0-9]+) passes \\(([0-9]+) with a depth, store plan (on|off|all)\\)")
set(compared 0)
set(failures 0)
set(dropped 0)
set(wrongDiffer 0)
set(wrongCompared 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    set(ok TRUE)
    foreach(run IN LISTS runs)
      set(dir_${run} "${OUT}/${index}-${s}-${run}")
      file(REMOVE_RECURSE "${dir_${run}}")
      file(MAKE_DIRECTORY "${dir_${run}}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_STORE_POISON=1
                              ICO_RD_STORE_PLAN=${plan_${run}} ${env_${s}} "${TOOL}" "${dump}" -
                              --pass-dump "${dir_${run}}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND "${err}" MATCHES "cannot show")
        # a dump drawn narrower than the setting's aspect: that setting is skipped for it
        message(STATUS "${dump} ${s}: skipped (${err})")
        set(skipped TRUE)
        break()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} ${run}: exit ${rc}:\n${err}")
        set(ok FALSE)
        break()
      endif()
      # the tool's lines with their ";" taken out (a ";" would split a list item)
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${storesRe}")
        message(SEND_ERROR "${dump} ${s} ${run}: no stores line:\n${out}")
        set(ok FALSE)
        break()
      endif()
      set(depth_${run} "${CMAKE_MATCH_1}")
      set(colour_${run} "${CMAKE_MATCH_2}")
      set(passes_${run} "${CMAKE_MATCH_3}")
      set(withDepth_${run} "${CMAKE_MATCH_4}")
      set(state_${run} "${CMAKE_MATCH_5}")
    endforeach()
    if(skipped)
      continue()
    endif()
    if(NOT ok)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    message(STATUS "${dump} ${s}: ${passes_on} passes, ${withDepth_on} with a depth; depth "
                   "stores dropped: kept ${depth_off}, planned ${depth_on}, all ${depth_all}")
    if(NOT state_off STREQUAL "off" OR NOT depth_off EQUAL 0 OR NOT colour_off EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: ICO_RD_STORE_PLAN=0 says store plan ${state_off} with "
                         "${depth_off} depth and ${colour_off} colour stores dropped")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT state_on STREQUAL "on" OR NOT state_all STREQUAL "all")
      message(SEND_ERROR "${dump} ${s}: the plan says ${state_on}, the wrong plan ${state_all}")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT passes_on EQUAL passes_off OR NOT withDepth_on EQUAL withDepth_off OR
       NOT passes_all EQUAL passes_off OR NOT withDepth_all EQUAL withDepth_off)
      message(SEND_ERROR "${dump} ${s}: passes (with a depth) kept ${passes_off} "
                         "(${withDepth_off}), planned ${passes_on} (${withDepth_on}), all "
                         "${passes_all} (${withDepth_all})")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT depth_all EQUAL withDepth_all OR depth_on GREATER withDepth_on OR
       NOT colour_on EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: ${depth_all} of ${withDepth_all} depth stores dropped by "
                         "the wrong plan, ${depth_on} of ${withDepth_on} (and ${colour_on} colour) "
                         "by the plan")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR dropped "${dropped} + ${depth_on}")
    file(GLOB pngs RELATIVE "${dir_off}" "${dir_off}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      foreach(run on all)
        if(NOT EXISTS "${dir_${run}}/${png}")
          message(SEND_ERROR "${dump} ${s}: ${png} written with every store kept only (${run})")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir_off}/${png}"
                                "${dir_${run}}/${png}" RESULT_VARIABLE same)
        if(run STREQUAL "on")
          math(EXPR compared "${compared} + 1")
          if(NOT same EQUAL 0)
            message(SEND_ERROR "${dump} ${s}: ${png} differs between every store kept and the "
                               "store plan")
            math(EXPR failures "${failures} + 1")
          endif()
        else()
          math(EXPR wrongCompared "${wrongCompared} + 1")
          if(NOT same EQUAL 0)
            math(EXPR wrongDiffer "${wrongDiffer} + 1")
          endif()
        endif()
      endforeach()
    endforeach()
  endforeach()
endforeach()
if(compared EQUAL 0 AND failures EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} or ${EXTRA} could be compared")
  return()
endif()
message(STATUS "rd_store_plan_ab: ${dropped} depth stores dropped by the plan; the wrong plan "
               "changed ${wrongDiffer} of ${wrongCompared} targets")
if(NOT dropped GREATER 0)
  message(SEND_ERROR "rd_store_plan_ab: the plan dropped no depth store over ${ndumps} dumps")
  math(EXPR failures "${failures} + 1")
endif()
if(NOT wrongDiffer GREATER 0)
  message(SEND_ERROR "rd_store_plan_ab: every depth store dropped (ICO_RD_STORE_PLAN=all) "
                     "changed no target: the poison does not show a dropped store")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_store_plan_ab: ${failures} failures over ${ndumps} dumps")
endif()
message(STATUS "rd_store_plan_ab: ${compared} targets equal over ${ndumps} dumps at 3 settings")
