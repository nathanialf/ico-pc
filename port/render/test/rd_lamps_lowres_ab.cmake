# rd_lamps_lowres_ab: the lamps' glow at the GS frame's size above 1x
# (port/render/rd_lamps.c RD_LAMPS_LOWRES, ICO_RD_LAMPS=...+lowres; the
# owner's choice B for v0.11.5: identical to the eye, not byte-identical;
# docs/port/DIVERGENCES.md VD-32) against today's chain, both arms with the
# byte-identical items pad+owed+wholepad (the tile class's default but the
# area, which the MoltenVK fake's Apple class leaves off; the lowres arm with
# no fillhold: off by default), on every glow
# dump of tools/glow_light_dumps.txt (machine-local; a listed dump that is
# absent FAILS), rd_replay_tool --pass-dump --stats, ICO_RD_VU_PREPASS=0, at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced 2364x1080, Full
#           height, Full pixel, presented at 2364x1080) under
#           ICO_VK_FAKE_LIMITS=moltenvk;
#   enh43   Enhanced 2x at 4:3, presented at 1280x960, the same fakes;
#   enh3x   Enhanced 3x at 4:3, presented at 1920x1440;
#   enh4x   Enhanced 4x at 4:3, presented at 2560x1920;
#   phonebox, phonebias, phonehy, phone4, phonep  the phone's setting with
#           lowresbox (the reduction the box), lowresbias (the slope bias at a
#           fractional sample point), lowreshy (bilinear on a plane, the
#           nearest texel off it), lowres4 (the farthest of four texels),
#           lowresp (a point compare everywhere): printed only;
#   enh25   Enhanced 2.5x at 4:3 (1440x1280); w169 the phone's Full height
#           and Full pixel on a 16:9 window (1920x1080): fractional scales;
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
# setting's maxima at the end, for the record; each form's WORK3 against
# the dump's Original 1x WORK3 ("lowres original: ..."), and it FAILS where
# lowres's default form is farther from it than today's (the criterion
# below, docs/port/DIVERGENCES.md VD-32).  No device: SKIP (77).
# -DPY=<python3> -DDIFF=<png_level_diff.py>
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# The lists name dumps of the build machine alone (the owner's rule there: a
# listed dump that is absent FAILS). A machine without that folder has none of
# them: the test is skipped as the older dump tests are.
if(NOT IS_DIRECTORY "/primary/dev/ico-dumps")
  message(STATUS "SKIP: no dump folder on this machine (/primary/dev/ico-dumps): the list's dumps are the build machine's")
  return()
endif()
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

# The criterion (DIVERGENCES.md VD-32: the lamps' glow is the original's
# (1x) glow at every scale, the goal-first rule): WORK3, the glow the paste
# adds (the same size at every scale), against Original 1x's WORK3 of the
# dump, no farther with lowres than today's Enhanced is, in its largest
# level difference and in its texels apart, within a margin of origLevels
# levels and origTexels texels plus origPct per cent (the replays' rounding
# of a few lamp edges).  At the measured settings with lowres's default
# form; phonebox, phoneb2 and phone4 (the other forms) are printed only.
# DISPLAY and PRESENT against today are printed for the record, no bound.
set(origLevels 2)
set(origTexels 16)
set(origPct 5)

set(common ICO_RD_VU_PREPASS=0)
set(lampsOff "ICO_RD_LAMPS=pad+owed+wholepad")
set(lampsOn "ICO_RD_LAMPS=pad+owed+wholepad+lowres")
set(lampsBox "ICO_RD_LAMPS=pad+owed+wholepad+lowresbox")
set(lampsBias "ICO_RD_LAMPS=pad+owed+wholepad+lowresbias")
set(lampsHy "ICO_RD_LAMPS=pad+owed+wholepad+lowreshy")
set(lamps4 "ICO_RD_LAMPS=pad+owed+wholepad+lowres4")
set(lampsP "ICO_RD_LAMPS=pad+owed+wholepad+lowresp")
set(lampsSS "ICO_RD_LAMPS=pad+owed+wholepad+lowres2x")
set(lampsMS "ICO_RD_LAMPS=pad+owed+wholepad+lowresms")
set(lampsMS8 "ICO_RD_LAMPS=pad+owed+wholepad+lowresms8")
set(lampsHold "ICO_RD_LAMPS=pad+owed+wholepad+lowres+fillhold")
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(args_enh43 --enhanced --resolution 2x --aspect 1.3333 --present 1280x960)
set(args_enh3x --enhanced --resolution 3x --aspect 1.3333 --present 1920x1440)
set(args_enh4x --enhanced --resolution 4x --aspect 1.3333 --present 2560x1920)
set(args_x1 --enhanced --resolution 1x --present 2364x1080)
set(args_poison ${args_phone})
set(args_hold ${args_phone})
set(args_phonebox ${args_phone})
set(args_phonebias ${args_phone})
set(args_phonehy ${args_phone})
# two more fractional scales: Enhanced 2.5x at 4:3, the phone's Full height
# and Full pixel on a 16:9 window
set(args_enh25 --enhanced --resolution 1440x1280 --aspect 1.3333 --present 1280x960)
set(args_w169 --enhanced --resolution 1920x1080 --full-height --full-pixel --present 1920x1080)
set(args_phone4 ${args_phone})
set(args_phonep ${args_phone})
# Original 1x of the dump (no --enhanced): WORK3, the glow the paste adds, is
# the same size at every scale; each form's WORK3 is printed against it
set(args_orig --present 2364x1080)
set(fake ICO_VK_FAKE_LIMITS=moltenvk)
# lowres2x (WORK2 at twice the GS size): its reference is today's picture
# (the original's 1x raster shimmers on thin lamps, which the owner saw on
# the phone); ss_<setting> runs it at each measured setting
set(ssSettings ss_phone ss_enh43 ss_enh25 ss_enh3x ss_enh4x ss_w169)
# lowresms (4 samples resolved in the pass): ms_<setting>, both arms on
# lavapipe's own desktop class (no fake: the MoltenVK fake's Apple class
# keeps today's path), its reference today's picture as lowres2x's
set(msSettings ms_phone ms_enh43 ms_enh25 ms_enh3x ms_enh4x ms_w169 ms8_phone ms8_enh43)
set(args_ms8_phone ${args_phone})
set(args_ms8_enh43 ${args_enh43})
foreach(b phone enh43 enh25 enh3x enh4x w169)
  set(args_ss_${b} ${args_${b}})
  set(args_ms_${b} ${args_${b}})
endforeach()
# The bound against today's WORK3 (largest level difference): a lamp edge's
# GS pixel is covered by 4 samples where today's raster gives it
# scale^2 (4 to 16): its coverage at most a quarter of a lamp texel's light
# apart, 64 levels in WORK2; the box of 2 x 4 GS pixels a WORK0 texel
# averages 8 of them, an edge crossing at most 4 of one sign: 32 in WORK0;
# the blur pairs spread it before the paste.  32 levels: a first bound, the
# measured maxima tighten it.  And the shimmer: WORK3's change under a
# shift of half a SCENE texel (--jitter 0.5,0.5, everything moved by the same
# GS distance) with lowres2x at most ssShimmer times today's plus 4 levels,
# at the phone's setting and 2x (printed with the Original's for information)
# Round 13's measurement (lavapipe, desktop class, every lamp in the 4-sample
# pass): lowresms's WORK3 at most 44 levels from today's (the title at 2x;
# 23 to 43 elsewhere), DISPLAY at most 24; its shimmer (WORK3's change
# under the half-texel shift) the title 17 against today's 15 at the phone's
# setting and 44 against 26 at 2x (1.7 times), the gate 11 against 11 and 13
# against 12.  The bounds: 48 levels against today (44 and a margin of 4);
# the shimmer at most twice today's plus 4 (the largest measured ratio 1.7:
# 1.5 times would fail the title at 2x by one level; ssShimmer halves)
set(ssLevels 48)
set(ssShimmer 4)
set(ssShimmerSettings ss_phone ss_enh43 ms_phone ms_enh43 ms8_phone ms8_enh43)
set(settings phone phonebox phonebias phonehy phone4 phonep enh43 enh25 enh3x enh4x w169
             ${ssSettings} ${msSettings} x1 hold poison msx1 mspoison)
# msx1: lowresms at Enhanced 1x (nothing changes: every PNG the same bytes);
# mspoison: lowresms at the phone's setting against it under the poison
# (the 4-sample depth cleared to 0.375 before the per-sample fill)
set(args_msx1 ${args_x1})
set(args_mspoison ${args_phone})
set(measured phone phonebox phonebias phonehy phone4 phonep enh43 enh25 enh3x enh4x w169)
# the WORK3-against-Original criterion is information now (printed): the
# lowres2x criteria below decide
set(asserted "")

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
      if(s MATCHES "^ms")
        set(env ${common})
      endif()
      if(s STREQUAL "mspoison")
        list(APPEND env "${lampsMS}")
        if(arm EQUAL 1)
          list(APPEND env ICO_RD_LAMPS_POISON=1)
        endif()
      elseif(s STREQUAL "msx1" AND arm EQUAL 1)
        list(APPEND env "${lampsMS}")
      elseif(s STREQUAL "poison")
        list(APPEND env "${lampsOn}")
        if(arm EQUAL 1)
          list(APPEND env ICO_RD_LAMPS_POISON=1)
        endif()
      elseif(s STREQUAL "hold")
        if(arm EQUAL 1)
          list(APPEND env "${lampsHold}")
        else()
          list(APPEND env "${lampsOn}")
        endif()
      elseif(s STREQUAL "phonebox" AND arm EQUAL 1)
        list(APPEND env "${lampsBox}")
      elseif(s STREQUAL "phonebias" AND arm EQUAL 1)
        list(APPEND env "${lampsBias}")
      elseif(s MATCHES "^ss_" AND arm EQUAL 1)
        list(APPEND env "${lampsSS}")
      elseif(s MATCHES "^ms8_" AND arm EQUAL 1)
        list(APPEND env "${lampsMS8}")
      elseif(s MATCHES "^ms_" AND arm EQUAL 1)
        list(APPEND env "${lampsMS}")
      elseif(s STREQUAL "phonehy" AND arm EQUAL 1)
        list(APPEND env "${lampsHy}")
      elseif(s STREQUAL "phone4" AND arm EQUAL 1)
        list(APPEND env "${lamps4}")
      elseif(s STREQUAL "phonep" AND arm EQUAL 1)
        list(APPEND env "${lampsP}")
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
    if(s STREQUAL "poison" OR s STREQUAL "hold" OR s STREQUAL "mspoison")
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
    if(s STREQUAL "x1" OR s STREQUAL "poison" OR s STREQUAL "hold" OR s STREQUAL "msx1" OR
       s STREQUAL "mspoison")
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
    endforeach()
    if(apart)
      string(REPLACE ";" " " apartText "${apart}")
      message(STATUS "${dump} ${s}: apart: ${apartText}")
    endif()
    # the reference: WORK3 against Original 1x's (asserted at the asserted settings)
    if(orc EQUAL 0 AND NOT exact AND EXISTS "${origDir}/WORK3.png")
      set(origText "")
      set(oOk TRUE)
      foreach(arm 0 1)
        set(oMax_${arm} -1)
        set(oN_${arm} -1)
        execute_process(COMMAND "${PY}" -I "${DIFF}" --channels RGB "${origDir}/WORK3.png"
                                "${OUT}/${index}-${s}-${arm}/WORK3.png"
                        OUTPUT_VARIABLE oout RESULT_VARIABLE odrc)
        if(odrc EQUAL 0 AND oout MATCHES "${diffRe}")
          set(oMax_${arm} "${CMAKE_MATCH_3}")
          set(oN_${arm} "${CMAKE_MATCH_4}")
          string(APPEND origText " arm ${arm}: ${CMAKE_MATCH_4} texels apart, largest "
                                 "${CMAKE_MATCH_3};")
        elseif(odrc EQUAL 0)
          set(oMax_${arm} 0)
          set(oN_${arm} 0)
          string(APPEND origText " arm ${arm}: the same;")
        else()
          set(oOk FALSE)
          string(APPEND origText " arm ${arm}: not comparable;")
        endif()
      endforeach()
      message(STATUS "lowres original: ${s} ${dump} WORK3 against Original 1x (arm 0 today, "
                     "arm 1 lowres):${origText}")
      # lowres2x against today's WORK3, and its shimmer
      list(FIND ssSettings "${s}" isSS)
      list(FIND msSettings "${s}" isMS)
      set(armWord "${lampsSS}")
      set(formName lowres2x)
      if(isMS GREATER -1)
        set(isSS 0)
        set(armWord "${lampsMS}")
        set(formName lowresms)
        if(s MATCHES "^ms8_")
          set(armWord "${lampsMS8}")
          set(formName lowresms8)
        endif()
        if(NOT flat MATCHES "lowresms on: [1-9]")
          message(SEND_ERROR "${dump} ${s}: lowresms not in force or no pass with 4 samples:\n${flat}")
          math(EXPR failures "${failures} + 1")
        elseif(flat MATCHES "resolved in the pass, ([1-9][0-9]*) single-sampled")
          # a pass that loads WORK2 (the mask after the fans, a whole frame's
          # second pass): printed; a lamps dump must have none
          message(STATUS "${dump} ${s}: ${CMAKE_MATCH_1} WORK2 passes single-sampled")
          if(dump MATCHES "/163/|rd-00400")
            message(SEND_ERROR "${dump} ${s}: a lamps dump with a single-sampled WORK2 pass")
            math(EXPR failures "${failures} + 1")
          endif()
        endif()
      endif()
      if(isSS GREATER -1)
        execute_process(COMMAND "${PY}" -I "${DIFF}" --channels RGB
                                "${OUT}/${index}-${s}-0/WORK3.png" "${OUT}/${index}-${s}-1/WORK3.png"
                        OUTPUT_VARIABLE tout RESULT_VARIABLE tdrc)
        set(tMax 0)
        set(tN 0)
        if(tdrc EQUAL 0 AND tout MATCHES "${diffRe}")
          set(tMax "${CMAKE_MATCH_3}")
          set(tN "${CMAKE_MATCH_4}")
        endif()
        message(STATUS "${formName} today: ${s} ${dump} WORK3 against today's: ${tN} texels apart, "
                       "largest ${tMax} (the bound ${ssLevels})")
        if(tMax GREATER ssLevels)
          message(SEND_ERROR "${dump} ${s}: ${formName}'s WORK3 ${tMax} levels from today's: the "
                             "bound is ${ssLevels} (DIVERGENCES.md VD-32)")
          math(EXPR failures "${failures} + 1")
        endif()
        list(FIND ssShimmerSettings "${s}" isShim)
        if(isShim GREATER -1)
          set(shimText "")
          foreach(arm 0 1)
            set(jdir "${OUT}/${index}-${s}-${arm}-j")
            file(REMOVE_RECURSE "${jdir}")
            file(MAKE_DIRECTORY "${jdir}")
            set(jfake ${fake})
            if(isMS GREATER -1)
              set(jfake "")
            endif()
            set(jenv ${common} ${jfake} "${lampsOff}")
            if(arm EQUAL 1)
              set(jenv ${common} ${jfake} "${armWord}")
            endif()
            execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${jenv} "${TOOL}" "${dump}" -
                                    --pass-dump "${jdir}" ${args_${s}} --jitter 0.5,0.5
                            RESULT_VARIABLE jrc OUTPUT_QUIET ERROR_QUIET)
            set(jMax_${arm} -1)
            if(jrc EQUAL 0)
              execute_process(COMMAND "${PY}" -I "${DIFF}" --channels RGB
                                      "${OUT}/${index}-${s}-${arm}/WORK3.png" "${jdir}/WORK3.png"
                              OUTPUT_VARIABLE jout RESULT_VARIABLE jdrc)
              if(jdrc EQUAL 0 AND jout MATCHES "${diffRe}")
                set(jMax_${arm} "${CMAKE_MATCH_3}")
                string(APPEND shimText " arm ${arm}: ${CMAKE_MATCH_4} texels change, largest "
                                       "${CMAKE_MATCH_3};")
              endif()
            endif()
          endforeach()
          message(STATUS "${formName} shimmer: ${s} ${dump} WORK3 under half a SCENE texel's shift "
                         "(arm 0 today, arm 1 ${formName}):${shimText}")
          if(jMax_0 GREATER -1 AND jMax_1 GREATER -1)
            math(EXPR shimAllowed "${jMax_0} * ${ssShimmer} / 2 + 4")
            if(jMax_1 GREATER shimAllowed)
              message(SEND_ERROR "${dump} ${s}: ${formName}'s WORK3 moves ${jMax_1} levels under "
                                 "the shift, today's ${jMax_0}: at most ${ssShimmer}/2 times "
                                 "today's + 4 (DIVERGENCES.md VD-32)")
              math(EXPR failures "${failures} + 1")
            endif()
          endif()
        endif()
      endif()
      list(FIND asserted "${s}" isAsserted)
      if(isAsserted GREATER -1 AND oOk)
        math(EXPR maxAllowed "${oMax_0} + ${origLevels}")
        math(EXPR nAllowed "${oN_0} + ${oN_0} * ${origPct} / 100 + ${origTexels}")
        if(oMax_1 GREATER maxAllowed OR oN_1 GREATER nAllowed)
          message(SEND_ERROR "${dump} ${s}: WORK3 ${oN_1} texels from Original 1x's, largest "
                             "${oMax_1}, with lowres; today's ${oN_0}, largest ${oMax_0}: lowres "
                             "must be no farther (margin ${origLevels} levels, ${origTexels} "
                             "texels + ${origPct} %; DIVERGENCES.md VD-32)")
          math(EXPR failures "${failures} + 1")
        endif()
      endif()
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
                 "${w}.${t} % of a picture's pixels apart (DISPLAY and PRESENT against today)")
endforeach()
message(STATUS "lowres hold: ${holdMore} crack fills more held with fillhold over the list")
if(holdMore LESS 1)
  message(SEND_ERROR "rd_lamps_lowres_ab: fillhold held no crack fill more over ${ndumps} dumps")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_lamps_lowres_ab: ${failures} failures over ${ndumps} dumps")
endif()
message(STATUS "rd_lamps_lowres_ab: ${ndumps} dumps: lowres's WORK3 no farther from Original 1x's "
               "than today's (VD-32); 1x, the hold and the poison the same bytes")
