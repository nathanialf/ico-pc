# rd_shadow_box_ab: the shadow count and composites drawn whole
# (ICO_RD_SHADOW_BOX=0) against over the volumes' box (=1;
# port/render/rd_shadow_box.c, docs/port/WORK_BUFFERS.md "The shadows over the
# volumes' box"), byte for byte.  For every dump the list names
# (tools/shadow_box_dumps.txt: a path, '*' globs allowed, optionally the counts
# and composites the box takes at the phone setting; a listed dump that is
# absent FAILS), rd_replay_tool's --pass-dump with --stats under the store plan
# (ICO_RD_STORE_PLAN=1) and the programs' own entries (ICO_RD_VU_PREPASS=0), at:
#   phone    the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=
#            moltenvk: DATE through the stencil bit, the count in one draw):
#            Enhanced, 2364x1080, Full height, Full pixel, presented 2364x1080;
#   mali     the Mali-G52's (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_NO_DST=1:
#            DATE by the R8 snapshot), 1x, presented 1612x720;
#   bspline  the composites' B-spline above 1x (ICO_RD_SHADOW_BSPLINE=1),
#            Enhanced 2x, presented 1280x720;
#   msaa     Enhanced 2x under MSAA 4x (the count in the epoch is drawn whole
#            and counted; after the epoch's resolve it is the one draw's),
#            presented 1280x720;
#   gl       -DBACKEND=opengl (rd_shadow_box_ab_gl): 1x on OpenGL, presented
#            1280x720;
# a dump a setting cannot show ("cannot show") is skipped there.  Each pair:
#   - every PNG the =0 run writes (SCENE, SHADOW0-2, the count target,
#     DISPLAY, PRESENT, every other target) the =1 run's byte for byte, the
#     same number;
#   - the same draws and passes;
#   - =0 says "shadow box off: 0 counts and 0 composites", =1 "shadow box on";
#   - at the phone setting a dump with expected figures takes exactly them;
# and over the dumps the phone setting boxes at least one count and one
# composite.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# -DBACKEND=vulkan|opengl
set(dumps "")
set(failures 0)
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_shadow_box_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  set(want "")
  if(l MATCHES "^([^ \t]+)[ \t]+([0-9]+)[ \t]+([0-9]+)$")
    set(l "${CMAKE_MATCH_1}")
    set(want "${CMAKE_MATCH_2} ${CMAKE_MATCH_3}")
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

set(common ICO_RD_STORE_PLAN=1 ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(args_mali --present 1612x720)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1)
set(args_bspline --enhanced --resolution 2x --present 1280x720)
set(env_bspline ICO_RD_SHADOW_BSPLINE=1)
set(args_msaa --enhanced --resolution 2x --aa msaa4 --present 1280x720)
set(env_msaa "")
set(args_gl --present 1280x720)
set(env_gl "")
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
else()
  set(settings phone mali bspline msaa)
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(boxRe "stats: shadow box (on|off): ([0-9]+) counts and ([0-9]+) composites over the volumes' box \\(([0-9]+) with nothing to draw[;,] ([0-9]+) of ([0-9]+) texels\\), ([0-9]+) whole")

set(compared 0)
set(phoneCounts 0)
set(phoneComposites 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(box 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${box}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}}
                              ICO_RD_SHADOW_BOX=${box}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND box EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (the setting cannot show it)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} box ${box}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${drawsRe}" OR NOT flat MATCHES "${boxRe}")
        message(SEND_ERROR "${dump} ${s} box ${box}: no draws or shadow box line:\n${out}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${box} "${CMAKE_MATCH_1}")
      set(passes_${box} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${boxRe}" m "${flat}")
      set(word_${box} "${CMAKE_MATCH_1}")
      set(counts_${box} "${CMAKE_MATCH_2}")
      set(comps_${box} "${CMAKE_MATCH_3}")
      set(texels_${box} "${CMAKE_MATCH_5}")
      set(all_${box} "${CMAKE_MATCH_6}")
      set(whole_${box} "${CMAKE_MATCH_7}")
      message(STATUS "${dump} ${s} box ${box}: ${passes_${box}} passes, ${draws_${box}} draws; "
                     "shadow box ${word_${box}}: ${counts_${box}} counts, ${comps_${box}} "
                     "composites (${texels_${box}} of ${all_${box}} texels), "
                     "${whole_${box}} whole")
    endforeach()
    if(skipped)
      continue()
    endif()
    if(NOT word_0 STREQUAL "off" OR NOT counts_0 EQUAL 0 OR NOT comps_0 EQUAL 0 OR
       NOT word_1 STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: =0 says ${word_0} (${counts_0} counts, ${comps_0} "
                         "composites), =1 says ${word_1}: off with nothing, then on, expected")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT draws_0 EQUAL draws_1 OR NOT passes_0 EQUAL passes_1)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws, ${passes_1} passes with the box; "
                         "${draws_0} and ${passes_0} without")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "phone")
      math(EXPR phoneCounts "${phoneCounts} + ${counts_1}")
      math(EXPR phoneComposites "${phoneComposites} + ${comps_1}")
      set(want "${want_${dump}}")
      if(NOT want STREQUAL "" AND NOT "${counts_1} ${comps_1}" STREQUAL "${want}")
        message(SEND_ERROR "${dump} phone: ${counts_1} counts and ${comps_1} composites over "
                           "the box, ${want} expected")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written drawn whole, ${npngOn} boxed")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs over the box: it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(NOT "${BACKEND}" STREQUAL "opengl" AND (phoneCounts EQUAL 0 OR phoneComposites EQUAL 0))
  message(SEND_ERROR "rd_shadow_box_ab: the phone setting boxed ${phoneCounts} counts and "
                     "${phoneComposites} composites over ${ndumps} dumps")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_shadow_box_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_shadow_box_ab: no target compared over ${ndumps} dumps")
endif()
message(STATUS "rd_shadow_box_ab: ${compared} targets the same bytes over ${ndumps} dumps; the "
               "phone setting boxed ${phoneCounts} counts and ${phoneComposites} composites")
