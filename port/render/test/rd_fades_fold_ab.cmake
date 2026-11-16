# rd_fades_fold_ab: the fades fold (port/render/rd_fades_fold.c;
# docs/port/RENDER_POST.md, "The fades fold") against list 11's sprites drawn
# into SCENE.  For every fixture the list names
# (port/render/test/fixtures/lists/fades_fold.txt: fixture names of the
# repository, docs/port/TESTING_FIXTURES.md; a listed fixture that is absent or
# does not expand to its pinned SHA-256 FAILS; after the name the line's own
# properties, rd_fixture_list_props.cmake), first the frame's last list 11
# commands (rd_replay_tool --list --no-device: which ones end with the
# fade, the letterbox or the brightness), then rd_replay_tool's --pass-dump
# with --stats twice, with ICO_RD_FADES_FOLD=0 (the sprites drawn, the
# tests' way to draw them as before v0.10.0) and without it (the fold: how
# the game draws, on every device), under two blends and three settings:
#   blends    ICO_RD_BLEND=attachment (the exact GS blend through the
#             destination read: the sprites store the GS's floor, and so
#             does the fold: its log line must say "the GS's floor") and
#             ICO_RD_BLEND=fixed (the default
#             fixed-function blend: the sprites store the nearest level, the
#             fold too but for the blender's own ties and precision; the
#             log line "the nearest level");
#   orig      Original at the dump's aspect, presented at 640x480: the
#             reduction's GS sprite model, the steps on SCENE's texels;
#   enh2x     Enhanced 2x, presented at 1280x960: the hardware sprite, the
#             steps after the filter;
#   phoneauto the PowerVR phone's Resolution Auto (2364x1080, Full height,
#             Full pixel, aspect 2.189, the programs' own entries,
#             ICO_RD_VU_PREPASS=0): a fractional scale, a bar's edge
#             between two of the filter's rows; a dump drawn narrower than
#             the aspect cannot show it and is skipped there.
# Every target's PNG but SCENE's (it holds the picture without the sprites
# by design: the WORK2 caveat of docs/port/WORK_BUFFERS.md) is compared:
# byte for byte where the fold did not happen (the folded run says
# "fades fold on: 0") and under the exact blend where the log says the steps
# were applied before the GS filter; elsewhere within two levels on every
# channel (png_level_diff.py: under the fixed-function blend two lerps
# between levels on one row add their roundings), the channels a level
# apart and the largest difference counted and printed per target, dump,
# blend and setting (docs/port/DIVERGENCES.md VD-27).  The A/B run must say "fades fold off" with none folded; a run
# whose reduction did not take a planned fold ("took another way") fails.  The
# path ran (rd_fixture_require on every folded run): each fixture's declared
# fades_folded, the sprites folded into the reduction (list 11's last
# full-frame lerp sprites: their rectangles, state and colours are the
# frame's commands, which the fixture keeps); a fixture no setting replayed
# FAILS, and some fixture must fold.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DPY=<python3> -DDIFF=<png_level_diff.py> -DOUT=<a folder the
# test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(dumps "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  rd_fixture_list_props(${name})
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
  set(checked_${name} FALSE)
endforeach()
list(LENGTH dumps ndumps)

set(blends attachment fixed)
set(settings orig enh2x phoneauto)
set(args_orig --present 640x480)
set(env_orig "")
set(args_enh2x --enhanced --resolution 2x --present 1280x960)
set(env_enh2x "")
set(args_phoneauto --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
                   --present 2364x1080)
set(env_phoneauto "ICO_RD_VU_PREPASS=0")
set(foldRe "stats: fades fold (on|off): ([0-9]+) sprites folded into the reduction")
set(diffRe "png_level_diff: [0-9]+ x [0-9]+, ([0-9]+) channels: [0-9]+ equal, ([0-9]+) one level apart, ([0-9]+) more \\(largest ([0-9]+)\\)")
set(failures 0)
set(folds 0)
set(compared 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  # the dump's list 11: its last commands
  execute_process(COMMAND "${TOOL}" "${dump}" "${OUT}/${index}-list.png" --list --no-device
                  RESULT_VARIABLE rcList OUTPUT_VARIABLE outList ERROR_VARIABLE errList)
  string(REPLACE ";" "," flatList "${outList}")
  string(REPLACE "\n" ";" listLines "${flatList}")
  set(l11 "")
  foreach(line IN LISTS listLines)
    if(line MATCHES "^11:")
      list(APPEND l11 "${line}")
    endif()
  endforeach()
  list(LENGTH l11 n11)
  set(tail "")
  if(n11 GREATER 12)
    math(EXPR from "${n11} - 12")
    list(SUBLIST l11 ${from} 12 tail)
  else()
    set(tail ${l11})
  endif()
  string(REPLACE ";" "\n    " tailText "${tail}")
  message(STATUS "${dump}: list 11 holds ${n11} commands, the last:\n    ${tailText}")
  foreach(blend IN LISTS blends)
    foreach(s IN LISTS settings)
      set(dirOff "${OUT}/${index}-${s}-${blend}-off")
      set(dirOn "${OUT}/${index}-${s}-${blend}-on")
      file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
      file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=${blend} ICO_RD_FADES_FOLD=0
                              ${env_${s}} "${TOOL}" "${dump}" "${dirOff}-out.png" --pass-dump "${dirOff}" --stats
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
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=${blend}
                              ${env_${s}} "${TOOL}" "${dump}" "${dirOn}-out.png" --pass-dump "${dirOn}" --stats
                              ${args_${s}}
                      RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
      if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
        message(SEND_ERROR "${dump} ${s} ${blend}: exit ${rcOff} (drawn) and ${rcOn} (folded):\n"
                           "${errOff}\n${errOn}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      string(REPLACE ";" "," flatOff "${outOff}")
      string(REPLACE ";" "," flatOn "${outOn}")
      if(NOT flatOff MATCHES "${foldRe}")
        message(SEND_ERROR "${dump} ${s} ${blend}: no fades fold line:\n${outOff}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(stateOff "${CMAKE_MATCH_1}")
      set(nOff "${CMAKE_MATCH_2}")
      if(NOT flatOn MATCHES "${foldRe}")
        message(SEND_ERROR "${dump} ${s} ${blend}: no fades fold line:\n${outOn}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(stateOn "${CMAKE_MATCH_1}")
      set(nOn "${CMAKE_MATCH_2}")
      if(NOT stateOff STREQUAL "off" OR NOT nOff EQUAL 0 OR NOT stateOn STREQUAL "on")
        message(SEND_ERROR "${dump} ${s} ${blend}: ICO_RD_FADES_FOLD=0 says ${stateOff} with "
                           "${nOff} folded, the default ${stateOn}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      if("${errOn}${outOn}" MATCHES "fades fold: the reduction took another way \\(([^)]*)\\)")
        message(SEND_ERROR "${dump} ${s} ${blend}: the reduction did not take the planned fold "
                           "(${CMAKE_MATCH_1}): the picture lacks the sprites")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      if(nOn GREATER 0)
        # the rounding the plan chose: the GS's floor under the exact blend
        set(wantRound "the nearest level")
        if(blend STREQUAL "attachment")
          set(wantRound "the GS's floor")
        endif()
        if(NOT "${errOn}${outOn}" MATCHES "rounding ${wantRound}")
          message(SEND_ERROR "${dump} ${s} ${blend}: the fold does not round to ${wantRound}")
          math(EXPR failures "${failures} + 1")
        endif()
      endif()
      math(EXPR folds "${folds} + ${nOn}")
      # the path ran: the sprites this fixture declares folded
      set(name "${name_${dump}}")
      rd_fixture_require(${name} "${outOn}" fades_folded)
      set(checked_${name} TRUE)
      # exact where the steps met SCENE's texels before the GS filter under
      # the exact blend; within a level elsewhere
      set(exact FALSE)
      if(nOn EQUAL 0)
        set(exact TRUE)
        set(way "not folded")
      elseif("${errOn}${outOn}" MATCHES "the steps on SCENE's texels before the GS filter")
        set(way "folded before the GS filter")
        if(blend STREQUAL "attachment")
          set(exact TRUE)
        endif()
      else()
        set(way "folded after the hardware filter")
      endif()
      set(why "")
      if("${errOn}" MATCHES "fades fold: list 11's last sprites are drawn into SCENE: ([^(\n]*)")
        set(why " (${CMAKE_MATCH_1})")
      endif()
      message(STATUS "${dump} ${s} ${blend}: ${nOn} sprites ${way}${why}")
      file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
      list(LENGTH pngs npng)
      if(npng EQUAL 0)
        message(SEND_ERROR "${dump} ${s} ${blend}: no target written")
        math(EXPR failures "${failures} + 1")
      endif()
      foreach(png IN LISTS pngs)
        if(png MATCHES "^SCENE")
          continue() # the picture without the sprites, by design
        endif()
        if(NOT EXISTS "${dirOn}/${png}")
          message(SEND_ERROR "${dump} ${s} ${blend}: ${png} written with the sprites drawn only")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        math(EXPR compared "${compared} + 1")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                                "${dirOn}/${png}" RESULT_VARIABLE same)
        if(same EQUAL 0)
          continue()
        endif()
        execute_process(COMMAND "${PY}" -I "${DIFF}" "${dirOff}/${png}" "${dirOn}/${png}"
                        RESULT_VARIABLE rcDiff OUTPUT_VARIABLE outDiff)
        string(STRIP "${outDiff}" outDiff)
        if(NOT rcDiff EQUAL 0 OR NOT outDiff MATCHES "${diffRe}")
          message(SEND_ERROR "${dump} ${s} ${blend}: ${png} differs and cannot be measured: "
                             "${outDiff}")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        set(more "${CMAKE_MATCH_3}")
        set(largest "${CMAKE_MATCH_4}")
        message(STATUS "  ${png}: ${outDiff}")
        if(exact)
          message(SEND_ERROR "${dump} ${s} ${blend}: ${png} differs (${way}): it must be the "
                             "same bytes")
          math(EXPR failures "${failures} + 1")
        elseif(largest GREATER 2)
          # under the fixed-function blend two lerps between levels on one
          # row add their roundings: two levels at most (VD-27)
          message(SEND_ERROR "${dump} ${s} ${blend}: ${png} has ${more} channels more than a "
                             "level apart, the largest ${largest} (two is the bound)")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
    endforeach()
  endforeach()
endforeach()
foreach(name IN LISTS names)
  if(NOT checked_${name})
    message(SEND_ERROR "${name}: no setting replayed it: its folds not checked")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_fades_fold_ab: ${failures} failures over ${ndumps} fixtures")
endif()
if(folds EQUAL 0)
  message(FATAL_ERROR "rd_fades_fold_ab: no fixture folded a sprite at any setting: the gate saw "
                      "nothing")
endif()
message(STATUS "rd_fades_fold_ab: ${compared} targets within the rule over ${ndumps} fixtures, "
               "${folds} sprites folded in all")
