# rd_late_pass_ab: fewer late full-size passes on SCENE, the same bytes
# (port/render/rd_late_pass.c; docs/port/RENDER_POST.md, "The late passes on
# SCENE"): the late pass off (ICO_RD_LATE_PASS=0) against on (=1).
# For every dump the list names (tools/late_pass_dumps.txt: a path, '*' globs
# allowed, optionally the crack fills one replay holds at the phone's setting,
# and optionally joinN, the passes list 11's draws join at the phone's setting
# instead of beginning one with SCENE's depth; a listed dump that is absent
# FAILS), rd_replay_tool's --pass-dump
# with --stats and ICO_RD_VU_PREPASS=0 (the programs' own entries), at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced, 2364x1080, Full height,
#           Full pixel, presented at 2364x1080) under the tile class's fakes
#           (ICO_VK_FAKE_LIMITS=moltenvk);
#   enh43   Enhanced 2x at 4:3, presented at 1280x960, under the same fakes (a
#           frame recorded 512 GS pixels wide keeps the PS2's border there:
#           SCENE's columns not repeated; a wider one repeats them);
#   gl      -DBACKEND=opengl (rd_late_pass_gl): the phone's setting on OpenGL
#           (llvmpipe; no fakes: the aura's paste reads its destination through
#           a snapshot there, so no fill is held for it: in a DARK room the
#           fill is drawn where it was due, as with the late pass off; one is
#           held into the glow's paste pass, drawn past SCENE's border repeat
#           with a draw for each repeated block; list 11's draws are not
#           joined there, rd_late_pass.c);
# a dump drawn narrower than a setting's aspect cannot show it and is skipped
# there.  Each pair: every PNG the off run writes (SCENE, DISPLAY, PRESENT and
# every other named target) the on run's byte for byte (but the join's, below); the same draws but
# those the stats name for the border's blocks (a held fill drawn past SCENE's
# border repeat: one draw for the middle and one a repeated block); the
# off run says "off" and holds, defers and joins nothing; the on run says "on"; its
# render passes fewer than the off run's by exactly the fills it held into a
# paste pass (the aura's or the glow's), the glow pastes it drew in the aura's
# paste pass and the passes list 11's draws joined, the crack fill's own passes
# by the fills held; its colour MB loaded and stored each fewer by at least the
# MB its stats name for the held fills and the deferred pastes and at most that
# and the MB it names for the joined passes (within 0.02, the two
# decimals' rounding: a pass the join saves may have had a render area), its
# depth and stencil MB loaded not higher, stored not higher but by up to 1.25
# times the held fills' MB where a fill was held into the glow's paste pass or
# the glow's paste deferred (the lamps' pass keeps its depth store for the
# fill's read), the same without a join, a fill in the glow's pass or a
# deferred paste, its copies the same; at the
# phone's setting a dump the list marks holds that many fills where the off
# run draws a fill in its own pass (on OpenGL none held or drawn early: no
# paste there takes a fill in these DARK rooms), and joins as many passes
# as it marks.  Where the on run joined list 11's draws to a pass, SCENE,
# DISPLAY and PRESENT may differ within the accepted bound (DIVERGENCES.md
# VD-31: at most joinPixels pixels apart by at most joinLevels levels,
# png_level_diff.py; -DPY, -DDIFF), every other target the same bytes.  On
# Vulkan at least one fill held and one pass joined over the list.  No
# device: SKIP (77).
# -DPY=<python3> -DDIFF=<png_level_diff.py>
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# -DBACKEND=vulkan|opengl
set(dumps "")
set(failures 0)
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_late_pass_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  set(want "")
  set(wantJoin "")
  if(l MATCHES "^(.*[^ \t])[ \t]+join([0-9]+)$")
    set(l "${CMAKE_MATCH_1}")
    set(wantJoin "${CMAKE_MATCH_2}")
  endif()
  if(l MATCHES "^([^ \t]+)[ \t]+([0-9]+)$")
    set(l "${CMAKE_MATCH_1}")
    set(want "${CMAKE_MATCH_2}")
  endif()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${listDir}/${l}")
  endif()
  if(l MATCHES "[*]")
    file(GLOB found "${l}")
    list(SORT found)
    if(NOT found)
      message(SEND_ERROR "${l}: no dump matches (a listed dump must exist)")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(d IN LISTS found)
      list(APPEND dumps "${d}")
      set(want_${d} "${want}")
      set(wantJoin_${d} "${wantJoin}")
    endforeach()
  elseif(EXISTS "${l}")
    list(APPEND dumps "${l}")
    set(want_${l} "${want}")
    set(wantJoin_${l} "${wantJoin}")
  else()
    message(SEND_ERROR "${l}: no such dump (a listed dump must exist)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)

# the crack fill's swap off in both runs (rd_crack_fill.c FILLSWAP: refused for a
# held fill, so with it on the pair's copies would differ by the fills held;
# rd_crack_work_ab checks the swap)
set(common ICO_RD_VU_PREPASS=0 ICO_RD_CRACK_SWAP=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(args_enh43 --enhanced --resolution 2x --aspect 1.3333 --present 1280x960)
set(env_enh43 ICO_VK_FAKE_LIMITS=moltenvk)
set(args_gl ${args_phone})
set(env_gl "")
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
else()
  set(settings phone enh43)
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(lateRe "stats: late pass (on|off|all|fill|defer|join|some): ([0-9]+) crack fills held into a paste pass \\(([0-9]+) the glow's, ([0-9.]+) MB of colour not loaded and as many not stored\\), ([0-9]+) drawn early")
set(deferRe ", ([0-9]+) glow pastes in the aura's paste pass \\(([0-9.]+) MB of colour not loaded and as many not stored\\), ([0-9]+) drawn early, ")
set(joinRe ", ([0-9]+) list 11 draws joined the open pass without SCENE's depth \\(([0-9]+) passes not begun, ([0-9.]+) MB of colour at most each way\\), ([0-9]+) draws more for the border's blocks")
set(crackRe "stats: crack fill (on|off): ([0-9]+) passes")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored, depth and stencil ([0-9.]+)/([0-9.]+) MB loaded/stored, [0-9.]+ MB cleared, ([0-9.]+) MB copied")

# list 11's join's bound (DIVERGENCES.md VD-31) on SCENE, DISPLAY and PRESENT of
# a run that joined a pass: measured on lavapipe at the phone's setting, issue
# 102's v4251 537, 515 and 478 pixels apart by up to 2, 3 and 2 levels, issue
# 164's v5836 235, 206 and 186 by 1; at Enhanced 2x v5836 92, 44 and 102 by 1:
# a margin of about half over the largest count, the largest level
set(joinPixels 800)
set(joinLevels 3)
set(diffRe "png_level_diff: ([0-9]+) x ([0-9]+), [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, [0-9]+ more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")

# v in hundredths of a MB (the stats print two decimals)
function(mb_hundredths out v)
  string(REGEX REPLACE "^([0-9]+)\\.([0-9][0-9])$" "\\1\\2" h "${v}")
  math(EXPR h "${h}")
  set(${out} "${h}" PARENT_SCOPE)
endfunction()

set(compared 0)
set(heldTotal 0)
set(joinTotal 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(lp 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${lp}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}} ICO_RD_LATE_PASS=${lp}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND lp EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} late pass ${lp}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      set(ok TRUE)
      foreach(re drawsRe lateRe deferRe joinRe crackRe trafficRe)
        if(NOT flat MATCHES "${${re}}")
          message(SEND_ERROR "${dump} ${s} late pass ${lp}: no ${re} line:\n${out}")
          math(EXPR failures "${failures} + 1")
          set(ok FALSE)
        endif()
      endforeach()
      if(NOT ok)
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${lp} "${CMAKE_MATCH_1}")
      set(passes_${lp} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${lateRe}" m "${flat}")
      set(word_${lp} "${CMAKE_MATCH_1}")
      set(held_${lp} "${CMAKE_MATCH_2}")
      set(glowHeld_${lp} "${CMAKE_MATCH_3}")
      set(heldMb_${lp} "${CMAKE_MATCH_4}")
      set(early_${lp} "${CMAKE_MATCH_5}")
      string(REGEX MATCH "${deferRe}" m "${flat}")
      set(deferred_${lp} "${CMAKE_MATCH_1}")
      set(deferMb_${lp} "${CMAKE_MATCH_2}")
      set(deferEarly_${lp} "${CMAKE_MATCH_3}")
      string(REGEX MATCH "${joinRe}" m "${flat}")
      set(joined_${lp} "${CMAKE_MATCH_1}")
      set(joinPasses_${lp} "${CMAKE_MATCH_2}")
      set(joinMb_${lp} "${CMAKE_MATCH_3}")
      set(blocks_${lp} "${CMAKE_MATCH_4}")
      string(REGEX MATCH "${crackRe}" m "${flat}")
      set(crack_${lp} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(cl_${lp} "${CMAKE_MATCH_1}")
      set(cs_${lp} "${CMAKE_MATCH_2}")
      set(dl_${lp} "${CMAKE_MATCH_3}")
      set(ds_${lp} "${CMAKE_MATCH_4}")
      set(cp_${lp} "${CMAKE_MATCH_5}")
      message(STATUS "${dump} ${s} late pass ${lp}: ${passes_${lp}} passes, ${draws_${lp}} draws, "
                     "${crack_${lp}} crack fill passes; ${held_${lp}} fills held "
                     "(${glowHeld_${lp}} in the glow's paste pass) "
                     "(${heldMb_${lp}} MB each way), ${early_${lp}} drawn early; ${joined_${lp}} "
                     "list 11 draws joined ${joinPasses_${lp}} passes (${joinMb_${lp}} MB); "
                     "${deferred_${lp}} glow pastes deferred (${deferMb_${lp}} MB), "
                     "${deferEarly_${lp}} early; colour "
                     "${cl_${lp}}/${cs_${lp}} MB, depth and stencil ${dl_${lp}}/${ds_${lp}} MB "
                     "loaded/stored, ${cp_${lp}} MB copied")
    endforeach()
    if(skipped)
      continue()
    endif()
    # the pair
    if(NOT word_0 STREQUAL "off" OR NOT held_0 EQUAL 0 OR NOT early_0 EQUAL 0 OR
       NOT joined_0 EQUAL 0 OR NOT deferred_0 EQUAL 0 OR NOT deferEarly_0 EQUAL 0 OR
       NOT blocks_0 EQUAL 0 OR
       NOT word_1 STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: the late pass says ${word_0} (${held_0} held, ${early_0} "
                         "early) off and ${word_1} on: off holding nothing and on expected")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR passesWant "${passes_0} - ${held_1} - ${joinPasses_1} - ${deferred_1}")
    math(EXPR crackWant "${crack_0} - ${held_1}")
    math(EXPR drawsWant "${draws_0} + ${blocks_1}")
    if(NOT draws_1 EQUAL drawsWant OR NOT passes_1 EQUAL passesWant OR NOT crack_1 EQUAL crackWant)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws, ${passes_1} passes, ${crack_1} crack "
                         "fill passes with the late pass; ${draws_0}, ${passes_0} and ${crack_0} "
                         "without, ${held_1} fills held, ${joinPasses_1} passes joined, "
                         "${blocks_1} draws more for the border's blocks")
      math(EXPR failures "${failures} + 1")
    endif()
    mb_hundredths(cl0 "${cl_0}")
    mb_hundredths(cl1 "${cl_1}")
    mb_hundredths(cs0 "${cs_0}")
    mb_hundredths(cs1 "${cs_1}")
    mb_hundredths(fall "${heldMb_1}")
    mb_hundredths(joinFall "${joinMb_1}")
    mb_hundredths(deferFall "${deferMb_1}")
    math(EXPR fallL "${cl0} - ${cl1} - ${fall} - ${deferFall}")
    math(EXPR fallS "${cs0} - ${cs1} - ${fall} - ${deferFall}")
    math(EXPR fallMax "${joinFall} + 2")
    if(fallL LESS -2 OR fallL GREATER fallMax OR fallS LESS -2 OR fallS GREATER fallMax)
      message(SEND_ERROR "${dump} ${s}: colour ${cl_0}/${cs_0} -> ${cl_1}/${cs_1} MB loaded/stored: "
                         "the held fills name ${heldMb_1} MB each way, the deferred glow pastes "
                         "${deferMb_1}, the joined passes up to ${joinMb_1}")
      math(EXPR failures "${failures} + 1")
    endif()
    mb_hundredths(dl0 "${dl_0}")
    mb_hundredths(dl1 "${dl_1}")
    mb_hundredths(ds0 "${ds_0}")
    mb_hundredths(ds1 "${ds_1}")
    # a fill held into the glow's paste pass (or, with the paste deferred, the
    # aura's) reads SCENE's depth after the lamps' pass, which then keeps its
    # depth store (the last store's cut took it before): up to 5 bytes a texel
    # more stored a fill (D32F_S8), 1.25 times the colour MB the held fills name
    set(dsMore 0)
    if(glowHeld_1 GREATER 0 OR deferred_1 GREATER 0)
      math(EXPR dsMore "${fall} * 5 / 4 + 2")
    endif()
    math(EXPR ds0more "${ds0} + ${dsMore}")
    if(dl1 GREATER dl0 OR ds1 GREATER ds0more OR NOT cp_1 STREQUAL cp_0 OR
       (joinPasses_1 EQUAL 0 AND glowHeld_1 EQUAL 0 AND deferred_1 EQUAL 0 AND
        (NOT dl_1 STREQUAL dl_0 OR NOT ds_1 STREQUAL ds_0)))
      message(SEND_ERROR "${dump} ${s}: depth and stencil ${dl_1}/${ds_1} MB, copies ${cp_1} MB "
                         "with the late pass; ${dl_0}/${ds_0} and ${cp_0} without (the hold moves "
                         "the fill's colour pass only, a join drops a pass's depth, a fill held to "
                         "the glow's paste keeps the lamps' pass's depth store)")
      math(EXPR failures "${failures} + 1")
    endif()
    set(want "${want_${dump}}")
    if(NOT want STREQUAL "" AND (s STREQUAL "phone" OR s STREQUAL "gl") AND crack_0 GREATER 0)
      set(got "${held_1}")
      if(s STREQUAL "gl")
        # no paste on OpenGL takes the fill in a DARK room: none held, none
        # drawn early (rd_late_pass.c landPossible)
        math(EXPR got "${held_1} + ${early_1}")
        set(want 0)
      endif()
      if(NOT got EQUAL want)
        message(SEND_ERROR "${dump} ${s}: ${held_1} fills held, ${early_1} drawn early; ${want} "
                           "expected ${s} (the light plan's DARK room)")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    set(wantJoin "${wantJoin_${dump}}")
    if(NOT wantJoin STREQUAL "" AND s STREQUAL "phone" AND NOT joinPasses_1 EQUAL wantJoin)
      message(SEND_ERROR "${dump} ${s}: list 11's draws joined ${joinPasses_1} passes, ${wantJoin} "
                         "expected (the pause menu after the aura's paste)")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR heldTotal "${heldTotal} + ${held_1}")
    math(EXPR joinTotal "${joinTotal} + ${joinPasses_1}")
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written with the late pass off, ${npngOn} on")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(same EQUAL 0)
        continue()
      endif()
      # list 11's join (DIVERGENCES.md VD-31, accepted): where the run joined
      # a pass, SCENE and what is made from it (DISPLAY, PRESENT) may differ
      # within the bound; every other target the same bytes
      if(joinPasses_1 GREATER 0 AND png MATCHES "^(SCENE|DISPLAY|PRESENT)\\.png$")
        if("${PY}" STREQUAL "" OR "${DIFF}" STREQUAL "")
          message(SEND_ERROR "${dump} ${s}: ${png} differs and no -DPY/-DDIFF to measure it")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        execute_process(COMMAND "${PY}" -I "${DIFF}" "${dirOff}/${png}" "${dirOn}/${png}"
                        OUTPUT_VARIABLE dout RESULT_VARIABLE drc)
        if(NOT drc EQUAL 0 OR NOT dout MATCHES "${diffRe}")
          message(SEND_ERROR "${dump} ${s}: ${png}: png_level_diff failed: ${dout}")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        set(largest "${CMAKE_MATCH_3}")
        set(pixels "${CMAKE_MATCH_4}")
        message(STATUS "${dump} ${s}: ${png} with list 11 joined: ${pixels} pixels differ, by "
                       "${largest} levels at most (the bound ${joinPixels}, ${joinLevels})")
        if(largest GREATER joinLevels OR pixels GREATER joinPixels)
          message(SEND_ERROR "${dump} ${s}: ${png}: ${pixels} pixels differ by up to ${largest} "
                             "levels with list 11 joined: the bound is ${joinPixels} pixels and "
                             "${joinLevels} levels (DIVERGENCES.md VD-31)")
          math(EXPR failures "${failures} + 1")
        endif()
        continue()
      endif()
      message(SEND_ERROR "${dump} ${s}: ${png} differs with the late pass on: it must be the "
                         "same bytes")
      math(EXPR failures "${failures} + 1")
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_late_pass_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_late_pass_ab: no target compared over ${ndumps} dumps")
endif()
if(NOT "${BACKEND}" STREQUAL "opengl" AND (heldTotal EQUAL 0 OR joinTotal EQUAL 0))
  message(FATAL_ERROR "rd_late_pass_ab: ${heldTotal} crack fills held and ${joinTotal} passes "
                      "joined over ${ndumps} dumps: one of each at least expected")
endif()
message(STATUS "rd_late_pass_ab: ${compared} targets the same bytes (or within VD-31's bound) over ${ndumps} dumps; "
               "${heldTotal} crack fills held into the aura's paste pass, ${joinTotal} passes "
               "joined by list 11's draws")
