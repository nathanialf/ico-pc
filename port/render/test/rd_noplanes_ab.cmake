# rd_noplanes_ab: the screen-prim triangles without the sprite planes builds
# (issue 161: the fallback a device whose driver stopped creating them draws
# with, [dev] pipeline_skip; docs/port/WORK_BUFFERS.md "Without the sprite
# planes") against the run with them.  For every fixture the list names
# (port/render/test/fixtures/lists/noplanes.txt: fixture names of the
# repository, docs/port/TESTING_FIXTURES.md; a listed fixture that is absent or
# does not expand to its pinned SHA-256 FAILS; after the name expect=same,
# expect=fallback:N or expect=report, the line's own properties
# (rd_fixture_list_props.cmake) and rd_replay_tool's setting arguments),
# rd_replay_tool's --pass-dump with --stats twice: as usual and under
# ICO_RD_NO_SPRITE_PLANES=1.  The second run must say the builds are off; the
# first must count no command through the fallback ("sprite no-planes 0").
# expect=same: every named target's PNG equal byte for byte, and no command
# through the fallback (a frame that draws no screen-prim triangle with
# planes: the switch changes nothing there).  expect=fallback:N: N commands
# through the fallback (N > 0, the count of the frame's commands of a
# screen-prim triangle key with planes: structure the fixture keeps) and
# every target byte for byte all the same.  expect=report: at least one
# command through the fallback, and the targets that differ are listed (the
# GPU's interpolation of the corners against the planes: a colour a level
# apart on some pixels), not failed.  The path ran (rd_fixture_require on the
# run without the builds): each fixture's declared noplanes_fallback, the
# commands drawn through the plain builds (>=1 where the fallback is tested,
# =0 on the frame with no such triangle); a fixture not compared FAILS, and
# some fixture must draw through the fallback.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
rd_fixture_list("${LIST}" names)
set(dietRe "sprite no-planes ([0-9]+)")
set(compared 0)
set(failures 0)
set(ndumps 0)
set(index 0)
set(fellBack 0)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)
  rd_fixture_list_props(${name})
  separate_arguments(words UNIX_COMMAND "${RDFX_FIELDS_${name}}")
  set(expect "same")
  set(wantN "")
  set(args "")
  foreach(w IN LISTS words)
    if(w MATCHES "^expect=(same|report)$")
      set(expect "${CMAKE_MATCH_1}")
    elseif(w MATCHES "^expect=fallback:([0-9]+)$")
      set(expect "fallback")
      set(wantN "${CMAKE_MATCH_1}")
    elseif(w MATCHES "^[a-z0-9_]+(>=|<=|!=|>|<|=)")
      # the line's own property: rd_fixture_list_props took it
    else()
      list(APPEND args "${w}")
    endif()
  endforeach()
  math(EXPR index "${index} + 1")
  math(EXPR ndumps "${ndumps} + 1")
  set(dirOn "${OUT}/${index}-planes")
  set(dirOff "${OUT}/${index}-noplanes")
  file(REMOVE_RECURSE "${dirOn}" "${dirOff}")
  file(MAKE_DIRECTORY "${dirOn}" "${dirOff}")
  execute_process(COMMAND "${TOOL}" "${dump}" /dev/null --pass-dump "${dirOn}" --stats ${args}
                  RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
  if(rcOn EQUAL 77)
    message(STATUS "SKIP: no device: ${errOn}")
    return()
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_NO_SPRITE_PLANES=1 "${TOOL}" "${dump}"
                          /dev/null --pass-dump "${dirOff}" --stats ${args}
                  RESULT_VARIABLE rcOff OUTPUT_VARIABLE outOff ERROR_VARIABLE errOff)
  if(NOT rcOn EQUAL 0 OR NOT rcOff EQUAL 0)
    message(SEND_ERROR "${dump}: exit ${rcOn} (with the planes) and ${rcOff} (without):\n"
                       "${errOn}\n${errOff}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(NOT errOff MATCHES "the sprite planes builds are not made on this device")
    message(SEND_ERROR "${dump}: ICO_RD_NO_SPRITE_PLANES=1 did not take:\n${errOff}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  # the tool's lines with their ";" taken out (a ";" would split a list item)
  string(REPLACE ";" "," flatOn "${outOn}")
  string(REPLACE ";" "," flatOff "${outOff}")
  set(nOn "?")
  set(nOff "?")
  if(flatOn MATCHES "${dietRe}")
    set(nOn "${CMAKE_MATCH_1}")
  endif()
  if(flatOff MATCHES "${dietRe}")
    set(nOff "${CMAKE_MATCH_1}")
  endif()
  # the path ran: the commands drawn through the plain builds this fixture declares
  rd_fixture_require(${name} "${outOff}" noplanes_fallback)
  if(nOff MATCHES "^[0-9]+$" AND nOff GREATER 0)
    math(EXPR fellBack "${fellBack} + ${nOff}")
  endif()
  if(NOT nOn STREQUAL "0" OR nOff STREQUAL "?")
    message(SEND_ERROR "${dump}: the fallback's count: ${nOn} with the planes (0 expected), "
                       "${nOff} without")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(expect STREQUAL "same" AND NOT nOff STREQUAL "0")
    message(SEND_ERROR "${dump}: expect=same, but ${nOff} commands drew through the fallback "
                       "(the frame draws screen-prim triangles with planes: expect=report)")
    math(EXPR failures "${failures} + 1")
  endif()
  if(expect STREQUAL "fallback" AND (wantN EQUAL 0 OR NOT nOff STREQUAL "${wantN}"))
    message(SEND_ERROR "${dump}: expect=fallback:${wantN}, but ${nOff} commands drew through "
                       "the fallback")
    math(EXPR failures "${failures} + 1")
  endif()
  if(expect STREQUAL "report" AND nOff STREQUAL "0")
    message(SEND_ERROR "${dump}: expect=report, but no command drew through the fallback")
    math(EXPR failures "${failures} + 1")
  endif()
  file(GLOB pngs RELATIVE "${dirOn}" "${dirOn}/*.png")
  list(LENGTH pngs npng)
  if(npng EQUAL 0)
    message(SEND_ERROR "${dump}: no target written")
    math(EXPR failures "${failures} + 1")
  endif()
  set(differ "")
  foreach(png IN LISTS pngs)
    if(NOT EXISTS "${dirOff}/${png}")
      message(SEND_ERROR "${dump}: ${png} written with the planes only")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOn}/${png}"
                            "${dirOff}/${png}" RESULT_VARIABLE same)
    math(EXPR compared "${compared} + 1")
    if(NOT same EQUAL 0)
      list(APPEND differ "${png}")
    endif()
  endforeach()
  list(LENGTH differ ndiff)
  string(REPLACE ";" ", " differText "${differ}")
  message(STATUS "${dump}: ${nOff} commands through the plain builds; ${ndiff} of ${npng} "
                 "targets differ: ${differText}")
  if(NOT expect STREQUAL "report" AND ndiff GREATER 0)
    message(SEND_ERROR "${dump}: expect=${expect}, but ${differText} differ without the planes")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(LENGTH names nnames)
if(NOT ndumps EQUAL nnames)
  message(SEND_ERROR "rd_noplanes_ab: ${ndumps} of ${nnames} fixtures compared")
  math(EXPR failures "${failures} + 1")
endif()
if(fellBack EQUAL 0)
  message(SEND_ERROR "rd_noplanes_ab: no fixture drew a command through the fallback")
  math(EXPR failures "${failures} + 1")
endif()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_noplanes_ab: ${failures} failures over ${ndumps} fixtures")
endif()
message(STATUS "rd_noplanes_ab: ${compared} targets compared over ${ndumps} fixtures, ${fellBack} "
               "commands through the plain builds")
