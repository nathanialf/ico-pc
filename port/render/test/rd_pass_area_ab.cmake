# rd_pass_area_ab: the passes' render areas (port/render/rd_pass_area.c;
# docs/port/RENDER.md, "The pass area") against whole passes, byte for byte.
# For every fixture the list names (port/render/test/fixtures/lists/
# pass_area.txt: fixture names of the repository, docs/port/
# TESTING_FIXTURES.md; the word phone after a name marks a PowerVR phone's
# frame; a listed fixture that is absent or does not expand to its pinned
# SHA-256 FAILS), expanded into OUT, rd_replay_tool's --pass-dump with --stats
# twice under ICO_RD_BLEND=fixed,
# with ICO_RD_PASS_AREA=0 (every pass its whole target) and
# ICO_RD_PASS_AREA=1 (on whatever the device: lavapipe is not a tile-based
# GPU, but it clips every draw to the area, so an area that misses a texel
# a draw writes shows as a texel that differs), at two settings:
#   phoneauto the phone's Resolution Auto (2364x1080, Full height, Full
#             pixel, aspect 2.189, presented at 2364x1080, the programs' own
#             entries, ICO_RD_VU_PREPASS=0): the scaled, cropped frame the
#             areas meet on the phone; a fixture drawn narrower than the
#             aspect cannot show it and is skipped there;
#   enh2x     Enhanced 2x at 4:3, presented at 1280x960.
# Every PNG the first run writes (SCENE, DISPLAY, PRESENT and every other
# named target) must be the second's byte for byte; the off run must say
# "pass area off: 0 passes", the other "pass area on"; both the same draws,
# and the on run's passes the off run's plus at most its passes ended early
# (a pass ended at its area's last command and begun again); the phone's
# frames at the phone's setting must give some pass an area or say why their
# passes were kept whole with "its draws cover the picture" among the reasons
# (the aura's paste and list 11's fades are full-frame sprites: staticBlur.c
# pasteFeedBackAreaToFB and addWork1ToFB draw the frame's rect, so a stage 6
# frame of 2026-11-02 has no pass an area can spare a tile of; any other
# answer means the scan no longer sees those sprites).  The path ran
# (rd_fixture_require on the on run of every setting the fixture shows): its
# declared pass_area_passes and pass_area_covered; a fixture whose properties
# no setting checked FAILS.  No device: SKIP (77).  (No committed fixture,
# and no source dump measured for it, takes an area at these settings: the
# stage 6 frames' passes are kept whole for their full-frame sprites, the
# others for a 3D draw, the fog, a clear or a post sprite; the area itself is
# covered on the CPU by rd_state and on lavapipe by rhi_vk and rd_pass_cut.)
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(dumps "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_list_props(${name})
  rd_fixture_list_word(${name} phone isPhone)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
  set(phone_${d} ${isPhone})
endforeach()
list(LENGTH dumps ndumps)

set(settings phoneauto enh2x)
set(args_phoneauto --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
                   --present 2364x1080)
set(env_phoneauto "ICO_RD_VU_PREPASS=0")
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x "")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(areaRe "stats: pass area (on|off): ([0-9]+) passes with an area \\(([0-9]+) of their ([0-9]+) tiles loaded\\), ([0-9]+) ended early")
set(failures 0)
set(areas 0)
set(phoneAreas 0)
set(phoneCovered 0)
set(phoneCompared 0)
set(compared 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(phone ${phone_${dump}})
  foreach(s IN LISTS settings)
    set(dirOff "${OUT}/${index}-${s}-off")
    set(dirOn "${OUT}/${index}-${s}-on")
    file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
    file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_PASS_AREA=0
                            ${env_${s}} "${TOOL}" "${dump}" "${OUT}/out.png" --pass-dump
                            "${dirOff}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOff OUTPUT_VARIABLE outOff ERROR_VARIABLE errOff)
    if(rcOff EQUAL 77)
      message(STATUS "SKIP: no device: ${errOff}")
      return()
    endif()
    if(NOT rcOff EQUAL 0 AND "${errOff}" MATCHES "cannot show")
      message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_PASS_AREA=1
                            ${env_${s}} "${TOOL}" "${dump}" "${OUT}/out.png" --pass-dump
                            "${dirOn}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
    if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOff} (areas off) and ${rcOn} (areas on):\n"
                         "${errOff}\n${errOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    string(REPLACE ";" "," flatOff "${outOff}")
    string(REPLACE ";" "," flatOn "${outOn}")
    if(NOT flatOff MATCHES "${drawsRe}")
      message(SEND_ERROR "${dump} ${s}: no stats line:\n${outOff}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(drawsOff "${CMAKE_MATCH_1}")
    set(passesOff "${CMAKE_MATCH_2}")
    if(NOT flatOn MATCHES "${drawsRe}")
      message(SEND_ERROR "${dump} ${s}: no stats line:\n${outOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(drawsOn "${CMAKE_MATCH_1}")
    set(passesOn "${CMAKE_MATCH_2}")
    if(NOT flatOff MATCHES "${areaRe}")
      message(SEND_ERROR "${dump} ${s}: no pass area line:\n${outOff}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(stateOff "${CMAKE_MATCH_1}")
    set(nOff "${CMAKE_MATCH_2}")
    if(NOT flatOn MATCHES "${areaRe}")
      message(SEND_ERROR "${dump} ${s}: no pass area line:\n${outOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(stateOn "${CMAKE_MATCH_1}")
    set(nOn "${CMAKE_MATCH_2}")
    set(tilesOn "${CMAKE_MATCH_3}")
    set(allOn "${CMAKE_MATCH_4}")
    set(endsOn "${CMAKE_MATCH_5}")
    if(NOT stateOff STREQUAL "off" OR NOT nOff EQUAL 0 OR NOT stateOn STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: ICO_RD_PASS_AREA=0 says ${stateOff} with ${nOff} passes "
                         "with an area, ICO_RD_PASS_AREA=1 ${stateOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR mostPasses "${passesOff} + ${endsOn}")
    if(NOT drawsOn EQUAL drawsOff OR passesOn LESS passesOff OR passesOn GREATER mostPasses)
      message(SEND_ERROR "${dump} ${s}: areas on ${passesOn} passes and ${drawsOn} draws, off "
                         "${passesOff} and ${drawsOff}: want the same draws and at most "
                         "${endsOn} passes more (those ended early)")
      math(EXPR failures "${failures} + 1")
    endif()
    # the path ran: the areas and the reasons the fixture declares
    rd_fixture_require(${name_${dump}} "${outOn}" pass_area_passes pass_area_covered)
    set(required_${dump} TRUE)
    math(EXPR areas "${areas} + ${nOn}")
    # why the others were whole (rd.h RD_AREA_WHY_*, rd_perf_why_text)
    set(whyOn "")
    if(flatOn MATCHES "ended early, ([0-9]+ passes kept whole \\([^)]*\\))")
      set(whyOn "${CMAKE_MATCH_1}")
    endif()
    if(phone AND s STREQUAL "phoneauto")
      math(EXPR phoneAreas "${phoneAreas} + ${nOn}")
      math(EXPR phoneCompared "${phoneCompared} + 1")
      if(whyOn MATCHES "its draws cover the picture")
        math(EXPR phoneCovered "${phoneCovered} + 1")
      endif()
    endif()
    message(STATUS "${dump} ${s}: ${nOn} passes with an area (${tilesOn} of their ${allOn} "
                   "tiles), ${endsOn} ended early; passes ${passesOff} -> ${passesOn}; ${whyOn}")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngsOn npngOn)
    if(NOT npngOn EQUAL npng)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written with areas off, ${npngOn} on")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirOn}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written with areas off only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs: it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
foreach(dump IN LISTS dumps)
  if(NOT required_${dump})
    message(SEND_ERROR "${name_${dump}}: its properties were never checked (no setting could "
                       "show it): a fixture this test cannot measure is no test")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_pass_area_ab: ${failures} failures over ${ndumps} fixtures")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_pass_area_ab: no target compared over ${ndumps} fixtures")
endif()
if(phoneCompared EQUAL 0)
  message(FATAL_ERROR "rd_pass_area_ab: no phone frame (the word phone) compared at the phone's "
                      "setting: the gate saw nothing")
endif()
if(phoneAreas EQUAL 0 AND phoneCovered EQUAL 0)
  message(FATAL_ERROR "rd_pass_area_ab: the phone's ${phoneCompared} frames gave no pass an area "
                      "at its setting and none was kept whole because its draws cover the "
                      "picture: the scan no longer sees the full-frame sprites")
endif()
message(STATUS "rd_pass_area_ab: ${compared} targets the same bytes over ${ndumps} fixtures, "
               "${areas} passes with an area in all (${phoneAreas} on the phone's frames at its "
               "setting)")
