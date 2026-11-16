# rd_cull_ab: the scene draws' visibility test (rd_vis.c; docs/port/RENDER.md,
# "Culling") against every draw replayed, byte for byte.  For every committed
# fixture of LIST (test/fixtures/lists/cull.txt; rd_fixture.cmake,
# docs/port/TESTING_FIXTURES.md: expanded into OUT, a listed fixture that is
# absent fails), under ICO_RD_BLEND=fixed, rd_replay_tool's --pass-dump with
# --stats three times: with ICO_RD_CULL=off (every scene draw replayed),
# ICO_RD_CULL=frustum (the draws wholly off their target or behind the eye
# replayed as NOPs) and ICO_RD_CULL=full (those, and the draws of list 0 wholly
# behind the depth the opaque draws before them wrote: rd_vis_hiz.c), at the
# settings
#   orig43   the Original preset at 4:3, presented at 640x480 (PRESENT.png too);
#   enh2x43  Enhanced 4:3 2x;
#   wide     Enhanced 2x at the frame's own aspect, for a frame drawn wider than
#            4:3 (its aspect read from --no-device --list);
# and msaa (Enhanced 4:3 2x with --aa msaa4) on the first fixture and on every
# fixture whose list line has the word msaa (a frame whose MSAA epoch closes by
# a read of SCENE: tools' old cull_msaa list's kept frame).
# Every named target's PNG of the frustum and the full runs must equal the off
# run's byte for byte; the off run's stats line must say "cull off: 0 of 0",
# the frustum run's "cull frustum: N of M" with N <= M, the full run's "cull
# full: N of M (F frustum, O occluded)" with F the frustum run's N and
# N = F + O; over the list the frustum runs must skip draws and the occlusion
# test must hide draws.  The path ran: on the full run at every setting, the
# fixture's declared frustum_culled and occluded (its own, or its list line's
# words: rd_fixture_list_props.cmake), and on the msaa run its declared
# msaa_epoch_kept (the draws kept because they may close the epoch) where it
# declares one; a fixture no setting checked fails.  Each run's counts are printed.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<folder>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<fixture list>
# -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
rd_fixture_list("${LIST}" names)
set(dumps "")
set(msaaList "")
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  rd_fixture_list_props(${name})
  set(name_${d} "${name}")
  list(APPEND dumps "${d}")
  rd_fixture_list_word(${name} msaa isMsaa)
  if(isMsaa)
    list(APPEND msaaList "${d}")
  endif()
endforeach()
list(LENGTH dumps ndumps)

set(args_orig43 --aspect 4:3 --present 640x480)
set(args_enh2x43 --enhanced --aspect 4:3 --resolution 2x)
set(args_wide --enhanced --resolution 2x)
set(args_msaa --enhanced --aspect 4:3 --resolution 2x --aa msaa4)
set(runs "off" "frustum" "full")
set(cullRe "stats: cull (off|frustum|full): ([0-9]+) of ([0-9]+) scene draws skipped \\(([0-9]+) frustum, ([0-9]+) occluded\\), ([0-9.]+) ms")
set(aspectRe "dump: version [0-9]+, GS [0-9]+x[0-9]+, aspect ([0-9.]+)")
set(compared 0)
set(failures 0)
set(skippedTotal 0)
set(occludedTotal 0)
set(drawsTotal 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(name "${name_${dump}}")
  set(checked_${name} 0)
  list(FIND msaaList "${dump}" msaaAt)
  # the frame's own aspect (the wide setting runs where it is wider than 4:3)
  execute_process(COMMAND "${TOOL}" "${dump}" - --no-device --list
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(settings "orig43" "enh2x43")
  if(rc EQUAL 0 AND out MATCHES "${aspectRe}")
    # the tool prints it with four decimals (%.4f): 4:3 is 1.3333, and a
    # dump before version 10 says so too; the digits as one number
    string(REPLACE "." "" aspectDigits "${CMAKE_MATCH_1}")
    if(aspectDigits GREATER 13400)
      list(APPEND settings "wide")
    endif()
  else()
    message(SEND_ERROR "${name}: its aspect not read (exit ${rc})")
    math(EXPR failures "${failures} + 1")
  endif()
  if(index EQUAL 1 OR NOT msaaAt EQUAL -1)
    list(APPEND settings "msaa")
  endif()
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    set(ok TRUE)
    foreach(run IN LISTS runs)
      set(dir_${run} "${OUT}/${index}-${s}-${run}")
      file(REMOVE_RECURSE "${dir_${run}}")
      file(MAKE_DIRECTORY "${dir_${run}}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_CULL=${run}
                              "${TOOL}" "${dump}" - --pass-dump "${dir_${run}}" --stats
                              ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${name} ${s}: skipped (${err})")
        set(skipped TRUE)
        break()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${name} ${s} ${run}: exit ${rc}:\n${err}")
        set(ok FALSE)
        break()
      endif()
      # the tool's lines with their ";" taken out (a ";" would split a list item)
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${cullRe}")
        message(SEND_ERROR "${name} ${s} ${run}: no cull line:\n${out}")
        set(ok FALSE)
        break()
      endif()
      set(mode_${run} "${CMAKE_MATCH_1}")
      set(skip_${run} "${CMAKE_MATCH_2}")
      set(draws_${run} "${CMAKE_MATCH_3}")
      set(frustum_${run} "${CMAKE_MATCH_4}")
      set(occluded_${run} "${CMAKE_MATCH_5}")
      set(ms_${run} "${CMAKE_MATCH_6}")
      set(out_${run} "${out}")
    endforeach()
    if(skipped)
      continue()
    endif()
    if(NOT ok)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    # the path ran: the declared counts on the full run
    rd_fixture_require(${name} "${out_full}" frustum_culled occluded)
    math(EXPR checked_${name} "${checked_${name}} + 1")
    if(s STREQUAL "msaa" AND RDFX_PROPS_${name} MATCHES "msaa_epoch_kept")
      rd_fixture_require(${name} "${out_full}" msaa_epoch_kept)
    endif()
    message(STATUS "${name} ${s}: ${skip_frustum} of ${draws_frustum} scene draws skipped "
                   "(${ms_frustum} ms); full: ${skip_full} (${occluded_full} occluded, "
                   "${ms_full} ms)")
    if(NOT mode_off STREQUAL "off" OR NOT skip_off EQUAL 0 OR NOT draws_off EQUAL 0)
      message(SEND_ERROR "${name} ${s}: ICO_RD_CULL=off says cull ${mode_off}, ${skip_off} of "
                         "${draws_off}")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT mode_frustum STREQUAL "frustum" OR skip_frustum GREATER draws_frustum)
      message(SEND_ERROR "${name} ${s}: ICO_RD_CULL=frustum says cull ${mode_frustum}, "
                         "${skip_frustum} of ${draws_frustum}")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR fullSum "${frustum_full} + ${occluded_full}")
    if(NOT mode_full STREQUAL "full" OR NOT draws_full EQUAL draws_frustum OR
       NOT frustum_full EQUAL skip_frustum OR NOT skip_full EQUAL fullSum)
      message(SEND_ERROR "${name} ${s}: ICO_RD_CULL=full says cull ${mode_full}, ${skip_full} of "
                         "${draws_full} (${frustum_full} frustum, ${occluded_full} occluded) "
                         "against the frustum run's ${skip_frustum} of ${draws_frustum}")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR skippedTotal "${skippedTotal} + ${skip_frustum}")
    math(EXPR occludedTotal "${occludedTotal} + ${occluded_full}")
    math(EXPR drawsTotal "${drawsTotal} + ${draws_frustum}")
    file(GLOB pngs RELATIVE "${dir_off}" "${dir_off}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${name} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(on IN ITEMS "frustum" "full")
      file(GLOB pngsOn RELATIVE "${dir_${on}}" "${dir_${on}}/*.png")
      foreach(png IN LISTS pngsOn)
        if(NOT EXISTS "${dir_off}/${png}")
          message(SEND_ERROR "${name} ${s}: ${png} written with the test (${on}) only")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
      foreach(png IN LISTS pngs)
        if(NOT EXISTS "${dir_${on}}/${png}")
          message(SEND_ERROR "${name} ${s}: ${png} written with every draw replayed only "
                             "(against ${on})")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir_off}/${png}"
                                "${dir_${on}}/${png}" RESULT_VARIABLE same)
        math(EXPR compared "${compared} + 1")
        if(NOT same EQUAL 0)
          message(SEND_ERROR "${name} ${s}: ${png} differs between every draw replayed and the "
                             "visibility test (${on})")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
    endforeach()
  endforeach()
  if(checked_${name} EQUAL 0)
    message(SEND_ERROR "${name}: no setting checked its declared counts")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
message(STATUS "rd_cull_ab: ${skippedTotal} of ${drawsTotal} scene draws skipped over the runs "
               "by the frustum test, ${occludedTotal} more by the occlusion test")
if(NOT skippedTotal GREATER 0)
  message(SEND_ERROR "rd_cull_ab: the visibility test skipped no scene draw over ${ndumps} fixtures")
  math(EXPR failures "${failures} + 1")
endif()
if(NOT occludedTotal GREATER 0)
  message(SEND_ERROR "rd_cull_ab: the occlusion test hid no scene draw over ${ndumps} fixtures")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_cull_ab: ${failures} failures over ${ndumps} fixtures")
endif()
message(STATUS "rd_cull_ab: ${compared} targets equal over ${ndumps} fixtures")
