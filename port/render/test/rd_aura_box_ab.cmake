# rd_aura_box_ab: the aura's feedback read of the picture into FEED128 in two
# passes (port/render/rd_aura_box.c; docs/port/WORK_BUFFERS.md, "The aura's
# feedback read") against the one-pass box, byte for byte.  For every fixture
# the list names (port/render/test/fixtures/lists/aura_box.txt: fixture names
# of the repository, docs/port/TESTING_FIXTURES.md, each with the reads it
# shows here on its line; a listed fixture that is absent or does not expand
# to its pinned SHA-256 FAILS), expanded into OUT, rd_replay_tool's
# --pass-dump with --stats twice under
# ICO_RD_BLEND=fixed, with ICO_RD_AURA_BOX=0 (one pass) and ICO_RD_AURA_BOX=1
# (two passes), at two settings:
#   phoneauto the phone's Resolution Auto (2364x1080, Full height, Full
#             pixel, aspect 2.189, presented at 2364x1080, the programs' own
#             entries, ICO_RD_VU_PREPASS=0): the box's edges a fraction of a
#             texel off the grid, moving by a 32nd from column to column; a
#             fixture drawn narrower than the aspect cannot show it and is
#             skipped there;
#   enh2x     Enhanced 2x at 4:3, presented at 1280x960.
# Every PNG the first run writes (FEED128, SCENE, PRESENT and every other
# named target) must be the second's byte for byte; the one-pass run must
# say "aura box off: 0", the other "aura box on"; the two-pass run's passes
# and draws must be the other's plus its reads in two passes (ROWBOX's pass
# and draw, one each a read); some fixture must read in two passes at some
# setting (else the gate saw nothing).  The path ran (rd_fixture_require on
# the two-pass run of every setting the fixture shows): its declared
# aura_box_reads; a fixture whose properties no setting checked FAILS.  No
# device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(dumps "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_list_props(${name})
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
endforeach()
list(LENGTH dumps ndumps)

set(settings phoneauto enh2x)
set(args_phoneauto --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
                   --present 2364x1080)
set(env_phoneauto "ICO_RD_VU_PREPASS=0")
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x "")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(boxRe "stats: aura box (on|off): ([0-9]+) feedback reads in two passes")
set(failures 0)
set(reads 0)
set(compared 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(dirOff "${OUT}/${index}-${s}-off")
    set(dirOn "${OUT}/${index}-${s}-on")
    file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
    file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_AURA_BOX=0
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
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_AURA_BOX=1
                            ${env_${s}} "${TOOL}" "${dump}" "${OUT}/out.png" --pass-dump
                            "${dirOn}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
    if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOff} (one pass) and ${rcOn} (two passes):\n"
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
    if(NOT flatOff MATCHES "${boxRe}")
      message(SEND_ERROR "${dump} ${s}: no aura box line:\n${outOff}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(stateOff "${CMAKE_MATCH_1}")
    set(nOff "${CMAKE_MATCH_2}")
    if(NOT flatOn MATCHES "${boxRe}")
      message(SEND_ERROR "${dump} ${s}: no aura box line:\n${outOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(stateOn "${CMAKE_MATCH_1}")
    set(nOn "${CMAKE_MATCH_2}")
    if(NOT stateOff STREQUAL "off" OR NOT nOff EQUAL 0 OR NOT stateOn STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: ICO_RD_AURA_BOX=0 says ${stateOff} with ${nOff} reads "
                         "in two passes, ICO_RD_AURA_BOX=1 ${stateOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR wantPasses "${passesOff} + ${nOn}")
    math(EXPR wantDraws "${drawsOff} + ${nOn}")
    if(NOT passesOn EQUAL wantPasses OR NOT drawsOn EQUAL wantDraws)
      message(SEND_ERROR "${dump} ${s}: two passes ${passesOn} passes and ${drawsOn} draws, one "
                         "pass ${passesOff} and ${drawsOff}: want one of each more a read "
                         "(${nOn} reads)")
      math(EXPR failures "${failures} + 1")
    endif()
    # the path ran: the reads the fixture declares, in two passes
    rd_fixture_require(${name_${dump}} "${outOn}" aura_box_reads)
    set(required_${dump} TRUE)
    math(EXPR reads "${reads} + ${nOn}")
    message(STATUS "${dump} ${s}: ${nOn} reads in two passes; passes ${passesOff} -> "
                   "${passesOn}, draws ${drawsOff} -> ${drawsOn}")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngsOn npngOn)
    if(NOT npngOn EQUAL npng)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written in one pass, ${npngOn} in two")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirOn}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written in one pass only")
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
  message(FATAL_ERROR "rd_aura_box_ab: ${failures} failures over ${ndumps} fixtures")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_aura_box_ab: no target compared over ${ndumps} fixtures")
endif()
if(reads EQUAL 0)
  message(FATAL_ERROR "rd_aura_box_ab: no fixture read the picture in two passes at any setting: "
                      "the gate saw nothing")
endif()
message(STATUS "rd_aura_box_ab: ${compared} targets the same bytes over ${ndumps} fixtures, "
               "${reads} reads in two passes in all")
