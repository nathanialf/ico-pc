# rd_shadow_tile_ab: the shadow composites drawn whole (ICO_RD_SHADOW_TILE=0)
# against over the GPU tile mask (=1; port/render/rd_shadow_tile.c,
# docs/port/RENDER_POST.md "The shadow tiles"), byte for byte.  For every
# fixture the list names (port/render/test/fixtures/lists/shadow_tile.txt:
# fixture names of the repository, docs/port/TESTING_FIXTURES.md, then the
# composites drawn over the mask at the phone setting and optionally the
# bounds on SHADOW0's, SHADOW1's and SHADOW2's live mask texels there; a
# listed fixture that is absent or does not expand to its pinned SHA-256
# FAILS), expanded into OUT, then rd_replay_tool's --pass-dump with --stats
# under the store plan (ICO_RD_STORE_PLAN=1) and the programs' own entries
# (ICO_RD_VU_PREPASS=0), at:
#   phone    the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=
#            moltenvk: DATE through the stencil bit, the count in one draw):
#            Enhanced, 2364x1080, Full height, Full pixel, presented 2364x1080;
#   mali     the Mali-G52's (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_NO_DST=1: DATE
#            by the R8 snapshot), 1x, presented 1612x720: nothing tiled;
#   bs2, bs3, bs4  the B-spline composites (ICO_RD_SHADOW_BSPLINE=1) at
#            Enhanced 2x, 3x and 4x, DATE through the stencil bit
#            (ICO_RD_DATE_PATH=stencil), presented 1280x720;
# a fixture a setting cannot show ("cannot show") is skipped there.  Each pair:
#   - every PNG the =0 run writes (SCENE, DISPLAY, PRESENT, SHADOW0-2 and
#     every other target) the =1 run's byte for byte, the same number;
#   - =0 says "shadow tiles off: 0 composites over the tile mask (0 masks)";
#   - the =1 run's passes are the =0 run's plus at most its masks (a mask is a
#     pass of its own, drawn before the composites' pass begins);
#   - at the phone setting a fixture draws its list's count of composites
#     over the mask, and one with bounds has at most that many live texels in
#     each level's mask (the stats' "SHADOWk L of N");
#   - the mali setting draws none over the mask;
#   - the path ran (rd_fixture_require on the =1 run at the phone setting):
#     the fixture's declared shadow_volume_draws and shadow_tile_composites;
#     a fixture the phone setting could not show FAILS;
# and over the fixtures the phone setting draws at least one composite over
# the mask.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
set(dumps "")
set(failures 0)
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
  set(wantC_${d} "")
  set(bounds_${d} "")
  set(f "${RDFX_FIELDS_${name}}")
  if(f MATCHES "^([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)$")
    set(wantC_${d} "${CMAKE_MATCH_1}")
    set(bounds_${d} "${CMAKE_MATCH_2};${CMAKE_MATCH_3};${CMAKE_MATCH_4}")
  elseif(f MATCHES "^([0-9]+)$")
    set(wantC_${d} "${CMAKE_MATCH_1}")
  else()
    message(SEND_ERROR "${name}: the list gives '${f}' after the name: the composites over the mask at the phone setting, then optionally three bounds")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(LENGTH dumps ndumps)

set(common ICO_RD_STORE_PLAN=1 ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(args_mali --present 1612x720)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1)
foreach(k 2 3 4)
  set(args_bs${k} --enhanced --resolution ${k}x --present 1280x720)
  set(env_bs${k} ICO_RD_SHADOW_BSPLINE=1 ICO_RD_DATE_PATH=stencil)
endforeach()
set(settings phone mali bs2 bs3 bs4)

set(passesRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(tileRe "stats: shadow tiles (on|off): ([0-9]+) composites over the tile mask \\(([0-9]+) masks\\), ([0-9]+) whole[^\n]*tiles live([^\n]*)")
set(levels SHADOW0 SHADOW1 SHADOW2)

set(compared 0)
set(phoneTiled 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(tile 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${tile}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}}
                              ICO_RD_SHADOW_TILE=${tile}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND tile EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (the setting cannot show it)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} tile ${tile}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      set(out_${tile} "${out}")
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${passesRe}" OR NOT flat MATCHES "${tileRe}")
        message(SEND_ERROR "${dump} ${s} tile ${tile}: no passes or shadow tiles line:\n${out}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${passesRe}" m "${flat}")
      set(passes_${tile} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${tileRe}" m "${flat}")
      set(word_${tile} "${CMAKE_MATCH_1}")
      set(tiled_${tile} "${CMAKE_MATCH_2}")
      set(masks_${tile} "${CMAKE_MATCH_3}")
      set(live_${tile} "${CMAKE_MATCH_5}")
      message(STATUS "${dump} ${s} tile ${tile}: ${passes_${tile}} passes; shadow tiles "
                     "${word_${tile}}: ${tiled_${tile}} composites, ${masks_${tile}} masks; "
                     "live:${live_${tile}}")
    endforeach()
    if(skipped)
      continue()
    endif()
    if(NOT word_0 STREQUAL "off" OR NOT tiled_0 EQUAL 0 OR NOT masks_0 EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: =0 says ${word_0} with ${tiled_0} composites and "
                         "${masks_0} masks: off with none expected")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR most "${passes_0} + ${masks_1}")
    if(passes_1 LESS passes_0 OR passes_1 GREATER most)
      message(SEND_ERROR "${dump} ${s}: ${passes_1} passes over the mask, ${passes_0} whole and "
                         "${masks_1} masks")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "mali" AND NOT tiled_1 EQUAL 0)
      message(SEND_ERROR "${dump} mali: ${tiled_1} composites over the mask without the stencil "
                         "bit")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "phone")
      # the path ran: volumes in the window, composites over the tile mask
      rd_fixture_require(${name_${dump}} "${out_1}" shadow_volume_draws shadow_tile_composites)
      set(required_${dump} TRUE)
      math(EXPR phoneTiled "${phoneTiled} + ${tiled_1}")
      set(w "${wantC_${dump}}")
      if(NOT w STREQUAL "" AND NOT tiled_1 EQUAL w)
        message(SEND_ERROR "${dump} phone: ${tiled_1} composites over the mask, ${w} expected")
        math(EXPR failures "${failures} + 1")
      endif()
      set(b "${bounds_${dump}}")
      if(NOT b STREQUAL "")
        set(li 0)
        foreach(lvl IN LISTS levels)
          list(GET b ${li} bound)
          math(EXPR li "${li} + 1")
          if(NOT "${live_1}" MATCHES " ${lvl} ([0-9]+) of ([0-9]+)")
            message(SEND_ERROR "${dump} phone: no ${lvl} mask in the stats:${live_1}")
            math(EXPR failures "${failures} + 1")
          elseif(CMAKE_MATCH_1 GREATER bound)
            message(SEND_ERROR "${dump} phone: ${lvl}'s mask has ${CMAKE_MATCH_1} of "
                               "${CMAKE_MATCH_2} texels live, at most ${bound} by its alpha box")
            math(EXPR failures "${failures} + 1")
          else()
            message(STATUS "${dump} phone: ${lvl} ${CMAKE_MATCH_1} of ${CMAKE_MATCH_2} mask "
                           "texels live (bound ${bound})")
          endif()
        endforeach()
      endif()
    endif()
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written whole, ${npngOn} over the mask")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs over the tile mask: it must be the same "
                           "bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(phoneTiled EQUAL 0)
  message(SEND_ERROR "rd_shadow_tile_ab: the phone setting drew no composite over the mask")
  math(EXPR failures "${failures} + 1")
endif()
foreach(dump IN LISTS dumps)
  if(NOT required_${dump})
    message(SEND_ERROR "${name_${dump}}: its properties were never checked (the phone setting "
                       "could not show it): a fixture this test cannot measure is no test")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_shadow_tile_ab: ${failures} failures over ${ndumps} fixtures")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_shadow_tile_ab: no target compared over ${ndumps} fixtures")
endif()
message(STATUS "rd_shadow_tile_ab: ${compared} targets the same bytes over ${ndumps} fixtures; "
               "the phone setting drew ${phoneTiled} composites over the mask")
