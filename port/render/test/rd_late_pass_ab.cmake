# rd_late_pass_ab: fewer late full-size passes on SCENE, the same bytes
# (port/render/rd_late_pass.c; docs/port/RENDER_POST.md, "The late passes on
# SCENE"): the late pass off (ICO_RD_LATE_PASS=0) against on (=1).
# For every dump the list names (tools/late_pass_dumps.txt: a path, '*' globs
# allowed, and optionally the crack fills one replay holds at the phone's
# setting; a listed dump that is absent FAILS), rd_replay_tool's --pass-dump
# with --stats and ICO_RD_VU_PREPASS=0 (the programs' own entries), at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced, 2364x1080, Full height,
#           Full pixel, presented at 2364x1080) under the tile class's fakes
#           (ICO_VK_FAKE_LIMITS=moltenvk);
#   enh43   Enhanced 2x at 4:3, presented at 1280x960, under the same fakes (a
#           frame recorded 512 GS pixels wide keeps the PS2's border there:
#           SCENE's columns not repeated; a wider one repeats them);
#   gl      -DBACKEND=opengl (rd_late_pass_gl): the phone's setting on OpenGL
#           (llvmpipe; no fakes: the aura's paste reads its destination through
#           a snapshot there, so a held fill is drawn before it in a pass of its
#           own, after SCENE's border repeat: the border's draws at their
#           source texel on GLSL);
# a dump drawn narrower than a setting's aspect cannot show it and is skipped
# there.  Each pair: every PNG the off run writes (SCENE, DISPLAY, PRESENT and
# every other named target) the on run's byte for byte; the same draws; the
# off run says "off" and holds nothing; the on run says "on"; its render passes
# and the crack fill's own passes fewer than the off run's by exactly the fills
# it held into the aura's paste pass; its colour MB loaded and stored each fewer
# by the MB its stats name for those fills (within 0.02, the two decimals'
# rounding), its depth and stencil MB and its copies the same; at the phone's
# setting (and on OpenGL, held or drawn early) a dump the list marks holds that
# many fills where the off run draws a fill in its own pass.  On Vulkan at least
# one fill is held over the list.  No device: SKIP (77).
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
    endforeach()
  elseif(EXISTS "${l}")
    list(APPEND dumps "${l}")
    set(want_${l} "${want}")
  else()
    message(SEND_ERROR "${l}: no such dump (a listed dump must exist)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)

set(common ICO_RD_VU_PREPASS=0)
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
set(lateRe "stats: late pass (on|off|fill): ([0-9]+) crack fills held into the aura's paste pass \\(([0-9.]+) MB of colour not loaded and as many not stored\\), ([0-9]+) drawn early")
set(crackRe "stats: crack fill (on|off): ([0-9]+) passes")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored, depth and stencil ([0-9.]+)/([0-9.]+) MB loaded/stored, [0-9.]+ MB cleared, ([0-9.]+) MB copied")

# v in hundredths of a MB (the stats print two decimals)
function(mb_hundredths out v)
  string(REGEX REPLACE "^([0-9]+)\\.([0-9][0-9])$" "\\1\\2" h "${v}")
  math(EXPR h "${h}")
  set(${out} "${h}" PARENT_SCOPE)
endfunction()

set(compared 0)
set(heldTotal 0)
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
      foreach(re drawsRe lateRe crackRe trafficRe)
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
      set(heldMb_${lp} "${CMAKE_MATCH_3}")
      set(early_${lp} "${CMAKE_MATCH_4}")
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
                     "(${heldMb_${lp}} MB each way), ${early_${lp}} drawn early; colour "
                     "${cl_${lp}}/${cs_${lp}} MB, depth and stencil ${dl_${lp}}/${ds_${lp}} MB "
                     "loaded/stored, ${cp_${lp}} MB copied")
    endforeach()
    if(skipped)
      continue()
    endif()
    # the pair
    if(NOT word_0 STREQUAL "off" OR NOT held_0 EQUAL 0 OR NOT early_0 EQUAL 0 OR
       NOT word_1 STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: the late pass says ${word_0} (${held_0} held, ${early_0} "
                         "early) off and ${word_1} on: off holding nothing and on expected")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR passesWant "${passes_0} - ${held_1}")
    math(EXPR crackWant "${crack_0} - ${held_1}")
    if(NOT draws_1 EQUAL draws_0 OR NOT passes_1 EQUAL passesWant OR NOT crack_1 EQUAL crackWant)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws, ${passes_1} passes, ${crack_1} crack "
                         "fill passes with the late pass; ${draws_0}, ${passes_0} and ${crack_0} "
                         "without, ${held_1} fills held")
      math(EXPR failures "${failures} + 1")
    endif()
    mb_hundredths(cl0 "${cl_0}")
    mb_hundredths(cl1 "${cl_1}")
    mb_hundredths(cs0 "${cs_0}")
    mb_hundredths(cs1 "${cs_1}")
    mb_hundredths(fall "${heldMb_1}")
    math(EXPR fallL "${cl0} - ${cl1} - ${fall}")
    math(EXPR fallS "${cs0} - ${cs1} - ${fall}")
    if(fallL LESS -2 OR fallL GREATER 2 OR fallS LESS -2 OR fallS GREATER 2)
      message(SEND_ERROR "${dump} ${s}: colour ${cl_0}/${cs_0} -> ${cl_1}/${cs_1} MB loaded/stored: "
                         "the held fills name ${heldMb_1} MB each way")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT dl_1 STREQUAL dl_0 OR NOT ds_1 STREQUAL ds_0 OR NOT cp_1 STREQUAL cp_0)
      message(SEND_ERROR "${dump} ${s}: depth and stencil ${dl_1}/${ds_1} MB, copies ${cp_1} MB "
                         "with the late pass; ${dl_0}/${ds_0} and ${cp_0} without (the hold moves "
                         "the fill's colour pass only)")
      math(EXPR failures "${failures} + 1")
    endif()
    set(want "${want_${dump}}")
    if(NOT want STREQUAL "" AND (s STREQUAL "phone" OR s STREQUAL "gl") AND crack_0 GREATER 0)
      if(s STREQUAL "gl")
        math(EXPR got "${held_1} + ${early_1}")
      else()
        set(got "${held_1}")
      endif()
      if(NOT got EQUAL want)
        message(SEND_ERROR "${dump} ${s}: ${held_1} fills held, ${early_1} drawn early; ${want} "
                           "expected ${s} (the light plan's DARK room)")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    math(EXPR heldTotal "${heldTotal} + ${held_1}")
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
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the late pass on: it must be the "
                           "same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_late_pass_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_late_pass_ab: no target compared over ${ndumps} dumps")
endif()
if(NOT "${BACKEND}" STREQUAL "opengl" AND heldTotal EQUAL 0)
  message(FATAL_ERROR "rd_late_pass_ab: no crack fill was held over ${ndumps} dumps")
endif()
message(STATUS "rd_late_pass_ab: ${compared} targets the same bytes over ${ndumps} dumps; "
               "${heldTotal} crack fills held into the aura's paste pass")
