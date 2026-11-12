# rd_lamps_lowres_ab: the lamps' glow at the GS frame's size above 1x
# (port/render/rd_lamps.c RD_LAMPS_LOWRES, ICO_RD_LAMPS=...+lowres; the
# owner's choice B for v0.11.5: identical to the eye, not byte-identical;
# docs/port/DIVERGENCES.md VD-32) against today's chain, both arms with the
# byte-identical items pad+owed+wholepad (the tile class's default but the
# area, which the MoltenVK fake's Apple class leaves off; the lowres arm with
# fillhold, its shipping form), on every glow
# dump of tools/glow_light_dumps.txt (machine-local; a listed dump that is
# absent FAILS), rd_replay_tool --pass-dump --stats, ICO_RD_VU_PREPASS=0, at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced 2364x1080, Full
#           height, Full pixel, presented at 2364x1080) under
#           ICO_VK_FAKE_LIMITS=moltenvk;
#   enh43   Enhanced 2x at 4:3, presented at 1280x960, the same fakes;
#   enh3x   Enhanced 3x at 4:3, presented at 1920x1440;
#   enh4x   Enhanced 4x at 4:3, presented at 2560x1920;
#   phonebox the phone's setting with lowresbox (the reduction the box over
#           the GS footprint instead of the GS bilinear);
#   x1      Enhanced 1x, presented at 2364x1080: nothing changes by
#           construction (WORK2 and SCENE of one size, the draws keep SCENE's
#           depth): every PNG the same bytes, no draw on WORK2's own depth;
#   hold    the phone's setting, lowres against lowres+fillhold (the crack
#           fill held past the lamps, which write WORK2's own depth there):
#           every PNG the same bytes, as many crack fills held at least
#           (rd_replay_tool's late pass line) and one more over the list;
#   poison  the phone's setting, lowres against lowres under
#           ICO_RD_LAMPS_POISON=1 (WORK2's own depth cleared to 0.375 before
#           depth_down_ps writes it): every PNG the same bytes (every texel
#           written).
# Above 1x: the lowres arm says "lamps lowres on" and draws no WORK2 draw
# without a depth; the other "off"; at least one draw on WORK2's own depth
# over the list at each setting; SHADOW0..2 and WORK2_PAD the same bytes;
# DISPLAY and PRESENT measured (png_level_diff --channels RGB: their alpha
# is VD-30's) and printed per dump ("lowres measure: ..."), with each
# setting's maxima at the end; FAILS above the bound (lowresLevels levels,
# lowresPct per cent of the picture's pixels).  The bound is a first one
# from the hand analysis (docs/port/DIVERGENCES.md VD-32); the measured
# maxima this prints tighten it.  No device: SKIP (77).
# -DPY=<python3> -DDIFF=<png_level_diff.py>
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
set(failures 0)
set(dumps "")
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_lamps_lowres_ab: no dump list ${LIST}")
endif()
if("${PY}" STREQUAL "" OR "${DIFF}" STREQUAL "")
  message(FATAL_ERROR "rd_lamps_lowres_ab: -DPY and -DDIFF are needed")
endif()
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
  if(EXISTS "${l}")
    list(APPEND dumps "${l}")
  else()
    message(SEND_ERROR "${l}: listed but absent (the list names only dumps present)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)

# The bound (DIVERGENCES.md VD-32): the owner's rule, identical to the eye,
# as a number: a few levels on a small part of the picture, inside the
# glow's soft light.  The first measurement (2026-11-12, before the fixes of
# depth_down_ps's farthest depth and the reduction's box) was 190 levels on
# 15.4 % (issue 163's gate: its lamp failed every depth test) and 24 on 3.4 %
# (the title's lamps: the GS bilinear's reach); with both fixed the change
# is WORK2's resolution inside the same box footprints, and lamp edges a GS
# pixel apart are what remains.  8 levels on 3 %: the acceptance, not a
# measurement; above it the change stays off.
set(lowresLevels 8)
set(lowresPct 3)

set(common ICO_RD_VU_PREPASS=0)
set(lampsOff "ICO_RD_LAMPS=pad+owed+wholepad")
set(lampsOn "ICO_RD_LAMPS=pad+owed+wholepad+lowres+fillhold")
set(lampsBox "ICO_RD_LAMPS=pad+owed+wholepad+lowresbox+fillhold")
set(lampsNoHold "ICO_RD_LAMPS=pad+owed+wholepad+lowres")
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(args_enh43 --enhanced --resolution 2x --aspect 1.3333 --present 1280x960)
set(args_enh3x --enhanced --resolution 3x --aspect 1.3333 --present 1920x1440)
set(args_enh4x --enhanced --resolution 4x --aspect 1.3333 --present 2560x1920)
set(args_x1 --enhanced --resolution 1x --present 2364x1080)
set(args_poison ${args_phone})
set(args_hold ${args_phone})
set(args_phonebox ${args_phone})
# Original 1x of the dump (no --enhanced): WORK3, the glow the paste adds, is
# the same size at every scale; each form's WORK3 is printed against it
set(args_orig --present 2364x1080)
set(fake ICO_VK_FAKE_LIMITS=moltenvk)
set(settings phone phonebox enh43 enh3x enh4x x1 hold poison)
set(measured phone phonebox enh43 enh3x enh4x)

set(lowresRe "stats: lamps lowres (on|off): WORK2 ([0-9]+)x([0-9]+) under SCENE ([0-9]+)x([0-9]+), ([0-9]+) draws into WORK2 on its own depth, ([0-9]+) depth fills, ([0-9]+) draws without a depth")
set(lateRe "stats: late pass [a-z]+: ([0-9]+) crack fills held into a paste pass \\(([0-9]+) the glow's")
set(diffRe "png_level_diff: ([0-9]+) x ([0-9]+), [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, [0-9]+ more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")

foreach(s IN LISTS settings)
  set(onDraws_${s} 0)
  set(maxLevel_${s} 0)
  set(maxPct10_${s} 0)
  set(maxWhere_${s} "")
endforeach()
set(holdMore 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(origDir "${OUT}/${index}-orig")
  file(REMOVE_RECURSE "${origDir}")
  file(MAKE_DIRECTORY "${origDir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${fake} "${lampsOff}"
                          "${TOOL}" "${dump}" - --pass-dump "${origDir}" ${args_orig}
                  RESULT_VARIABLE orc OUTPUT_QUIET ERROR_QUIET)
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(arm 0 1)
      if(skipped)
        break()
      endif()
      set(env ${common} ${fake})
      if(s STREQUAL "poison")
        list(APPEND env "${lampsOn}")
        if(arm EQUAL 1)
          list(APPEND env ICO_RD_LAMPS_POISON=1)
        endif()
      elseif(s STREQUAL "hold")
        if(arm EQUAL 1)
          list(APPEND env "${lampsOn}")
        else()
          list(APPEND env "${lampsNoHold}")
        endif()
      elseif(s STREQUAL "phonebox" AND arm EQUAL 1)
        list(APPEND env "${lampsBox}")
      elseif(arm EQUAL 1)
        list(APPEND env "${lampsOn}")
      else()
        list(APPEND env "${lampsOff}")
      endif()
      set(dir "${OUT}/${index}-${s}-${arm}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND arm EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT rc EQUAL 0 OR NOT flat MATCHES "${lowresRe}")
        message(SEND_ERROR "${dump} ${s} arm ${arm}: exit ${rc}, no lowres line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${lowresRe}" m "${flat}")
      set(word_${arm} "${CMAKE_MATCH_1}")
      set(w2w_${arm} "${CMAKE_MATCH_2}")
      set(scw_${arm} "${CMAKE_MATCH_4}")
      set(draws_${arm} "${CMAKE_MATCH_6}")
      set(fills_${arm} "${CMAKE_MATCH_7}")
      set(dropped_${arm} "${CMAKE_MATCH_8}")
      set(held_${arm} 0)
      if(flat MATCHES "${lateRe}")
        set(held_${arm} "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    if(skipped)
      continue()
    endif()
    # the words and the counts
    set(wantWord0 off)
    if(s STREQUAL "poison" OR s STREQUAL "hold")
      set(wantWord0 on)
    endif()
    if(NOT word_0 STREQUAL wantWord0 OR NOT word_1 STREQUAL "on" OR
       (NOT draws_0 EQUAL 0 AND wantWord0 STREQUAL "off"))
      message(SEND_ERROR "${dump} ${s}: lowres ${word_0} (${draws_0} draws) and ${word_1}: "
                         "${wantWord0} and on expected, no draw on WORK2's own depth off")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT dropped_1 EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: ${dropped_1} draws into WORK2 without a depth")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "x1" AND (NOT draws_1 EQUAL 0 OR NOT fills_1 EQUAL 0 OR
                            NOT w2w_1 EQUAL scw_1))
      message(SEND_ERROR "${dump} x1: ${draws_1} draws on WORK2's own depth, ${fills_1} fills, "
                         "WORK2 ${w2w_1} wide under SCENE ${scw_1}: at 1x nothing changes")
      math(EXPR failures "${failures} + 1")
    endif()
    if(draws_1 GREATER 0 AND fills_1 EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws on WORK2's own depth, no fill of it")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR onDraws_${s} "${onDraws_${s}} + ${draws_1}")
    if(s STREQUAL "hold")
      if(held_1 LESS held_0)
        message(SEND_ERROR "${dump} hold: ${held_1} crack fills held with fillhold, ${held_0} "
                           "without")
        math(EXPR failures "${failures} + 1")
      endif()
      math(EXPR holdMore "${holdMore} + ${held_1} - ${held_0}")
      message(STATUS "${dump} hold: ${held_0} crack fills held without fillhold, ${held_1} with")
    endif()
    # the pictures
    set(d0 "${OUT}/${index}-${s}-0")
    set(d1 "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${d0}" "${d0}/*.png")
    file(GLOB pngs1 RELATIVE "${d1}" "${d1}/*.png")
    list(LENGTH pngs n0)
    list(LENGTH pngs1 n1)
    if(n0 EQUAL 0 OR NOT n0 EQUAL n1)
      message(SEND_ERROR "${dump} ${s}: ${n0} PNGs in one arm, ${n1} in the other")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(exact FALSE)
    if(s STREQUAL "x1" OR s STREQUAL "poison" OR s STREQUAL "hold")
      set(exact TRUE)
    endif()
    set(apart "")
    foreach(png IN LISTS pngs)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${d0}/${png}" "${d1}/${png}"
                      RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        list(APPEND apart "${png}")
      endif()
      if(exact)
        if(NOT same EQUAL 0)
          message(SEND_ERROR "${dump} ${s}: ${png} differs: it must be the same bytes")
          math(EXPR failures "${failures} + 1")
        endif()
        continue()
      endif()
      if(png MATCHES "^(SHADOW[0-2]|WORK2_PAD)\\.png$" AND NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs: the lamps' WORK2 does not reach it")
        math(EXPR failures "${failures} + 1")
      endif()
      if(NOT png MATCHES "^(DISPLAY|PRESENT)\\.png$")
        continue()
      endif()
      set(largest 0)
      set(pixels 0)
      set(total 1)
      if(NOT same EQUAL 0)
        execute_process(COMMAND "${PY}" -I "${DIFF}" --channels RGB "${d0}/${png}" "${d1}/${png}"
                        OUTPUT_VARIABLE dout RESULT_VARIABLE drc)
        if(NOT drc EQUAL 0 OR NOT dout MATCHES "${diffRe}")
          message(SEND_ERROR "${dump} ${s}: ${png}: png_level_diff failed: ${dout}")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        math(EXPR total "${CMAKE_MATCH_1} * ${CMAKE_MATCH_2}")
        set(largest "${CMAKE_MATCH_3}")
        set(pixels "${CMAKE_MATCH_4}")
      endif()
      math(EXPR pct10 "${pixels} * 1000 / ${total}")
      math(EXPR pctWhole "${pct10} / 10")
      math(EXPR pctTenth "${pct10} % 10")
      message(STATUS "lowres measure: ${s} ${dump} ${png}: ${pixels} pixels apart "
                     "(${pctWhole}.${pctTenth} %), largest ${largest} levels; ${draws_1} draws on "
                     "WORK2's own depth")
      if(largest GREATER maxLevel_${s})
        set(maxLevel_${s} "${largest}")
        set(maxWhere_${s} "${dump} ${png}")
      endif()
      if(pct10 GREATER maxPct10_${s})
        set(maxPct10_${s} "${pct10}")
      endif()
      math(EXPR pctBound10 "${lowresPct} * 10")
      if(largest GREATER lowresLevels OR pct10 GREATER pctBound10)
        message(SEND_ERROR "${dump} ${s}: ${png}: ${pixels} pixels apart (${pctWhole}.${pctTenth} "
                           "%) by up to ${largest} levels: the bound is ${lowresLevels} levels "
                           "on ${lowresPct} % (DIVERGENCES.md VD-32)")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    if(apart)
      string(REPLACE ";" " " apartText "${apart}")
      message(STATUS "${dump} ${s}: apart: ${apartText}")
    endif()
    # the sharper reference: WORK3 against Original 1x's (printed, no bound)
    if(orc EQUAL 0 AND NOT exact AND EXISTS "${origDir}/WORK3.png")
      set(origText "")
      foreach(arm 0 1)
        execute_process(COMMAND "${PY}" -I "${DIFF}" --channels RGB "${origDir}/WORK3.png"
                                "${OUT}/${index}-${s}-${arm}/WORK3.png"
                        OUTPUT_VARIABLE oout RESULT_VARIABLE odrc)
        if(odrc EQUAL 0 AND oout MATCHES "${diffRe}")
          string(APPEND origText " arm ${arm}: ${CMAKE_MATCH_4} texels apart, largest "
                                 "${CMAKE_MATCH_3};")
        else()
          string(APPEND origText " arm ${arm}: not comparable;")
        endif()
      endforeach()
      message(STATUS "lowres original: ${s} ${dump} WORK3 against Original 1x (arm 0 today, "
                     "arm 1 lowres):${origText}")
    endif()
  endforeach()
endforeach()
foreach(s IN LISTS measured)
  if(onDraws_${s} EQUAL 0)
    message(SEND_ERROR "rd_lamps_lowres_ab: ${s}: no draw on WORK2's own depth over ${ndumps} "
                       "dumps")
    math(EXPR failures "${failures} + 1")
  endif()
  math(EXPR w "${maxPct10_${s}} / 10")
  math(EXPR t "${maxPct10_${s}} % 10")
  message(STATUS "lowres maxima: ${s}: largest ${maxLevel_${s}} levels (${maxWhere_${s}}), at most "
                 "${w}.${t} % of a picture's pixels apart; the bound ${lowresLevels} levels on "
                 "${lowresPct} %")
endforeach()
message(STATUS "lowres hold: ${holdMore} crack fills more held with fillhold over the list")
if(holdMore LESS 1)
  message(SEND_ERROR "rd_lamps_lowres_ab: fillhold held no crack fill more over ${ndumps} dumps")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_lamps_lowres_ab: ${failures} failures over ${ndumps} dumps")
endif()
message(STATUS "rd_lamps_lowres_ab: ${ndumps} dumps within VD-32's bound; 1x and the poison the "
               "same bytes")
