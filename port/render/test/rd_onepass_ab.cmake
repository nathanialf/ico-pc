# rd_onepass_ab: the two-pass blend's LERP As pairs drawn in one pass where the CPU
# proves the same bytes (port/render/rd_onepass.c) against every pair in two passes,
# byte for byte, on every fixture the list names (port/render/test/fixtures/lists/
# onepass.txt: fixture names of the repository, docs/port/TESTING_FIXTURES.md; a listed
# fixture that is absent or does not expand to its pinned SHA-256 FAILS), expanded into
# OUT, then rd_replay_tool --pass-dump --stats, arm 0 ICO_RD_ONEPASS=0 (today's two
# passes, the colour pass without A) against arm 1 ICO_RD_ONEPASS=1
# (ICO_RD_ONEPASS=sabotage with -DSABOTAGE=1), at:
#   mali    the Mali reporter's class (ICO_VK_FAKE_LIMITS=mali, no destination read, no
#           dual-source blending) with no dynamic state (ICO_VK_FAKE_NO_DYNAMIC=1, as the
#           Mali-G52's device table row has it: the colour pass's own static pipeline) at
#           1x, presented at 1612x720;
#   malidyn the same with lavapipe's dynamic blend state (the colour pass's state set at
#           each bind);
#   phone   the PowerVR phone's Resolution Auto (Enhanced 2364x1080, Full height, Full
#           pixel: a fractional scale, the no-planes twins) under
#           ICO_VK_FAKE_LIMITS=moltenvk with the two-pass blend forced (ICO_RD_NO_DUAL=1),
#           no destination read and no dynamic state.
# With -DSABOTAGE=1 (rd_onepass_sabotage) the Mali's setting alone.
# A fixture a setting cannot show ("cannot show") is skipped there.
# Per fixture and setting it asserts:
#   - every PNG of --pass-dump the same bytes in both arms (the same set of targets);
#   - the same passes ("stats: N draws, M passes"), arm 1 at most arm 0's draws, the same
#     traffic text, the same pairs in both arms;
#   - arm 0 "stats: one pass off" with 0 pairs in one pass;
#   - arm 1 "one pass on (self-test passed" with at most its pairs in one pass;
#   - sabotage: arm 1 "off" with its self-test failed and 0 in one pass;
# and over the list, at each setting but the sabotage, at least one pair in one pass (the
# path ran).  Each fixture's share of pairs in one pass is printed.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the fixtures
# folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a fixture list>
# -DOUT=<a folder> [-DSABOTAGE=1]
include("${FIXTURE_HELPERS}")
set(failures 0)
set(dumps "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
endforeach()
list(LENGTH dumps ndumps)

set(args_mali --present 1612x720)
set(args_malidyn --present 1612x720)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DUAL=1
    ICO_VK_FAKE_NO_DYNAMIC=1)
set(env_malidyn ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DUAL=1)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_VK_FAKE_NO_DST=1 ICO_RD_NO_DUAL=1
    ICO_VK_FAKE_NO_DYNAMIC=1)
if(SABOTAGE)
  set(settings mali)
  set(arm1 ICO_RD_ONEPASS=sabotage)
else()
  set(settings mali malidyn phone)
  set(arm1 ICO_RD_ONEPASS=1)
endif()
set(arm0 ICO_RD_ONEPASS=0)

# (the output's semicolons read as commas: the list separator replaced)
set(opRe "stats: one pass (on|off) \\(self-test ([^)]*)\\): ([0-9]+) pairs in one pass of ([0-9]+)[;,]")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(trafficRe "stats: traffic: (colour [0-9.]+ MB loaded [0-9.]+ MB stored, depth and stencil [0-9.]+/[0-9.]+ MB loaded/stored, [0-9.]+ MB cleared)")
foreach(s IN LISTS settings)
  set(pairs_${s} 0)
  set(one_${s} 0)
endforeach()
set(compared 0)

set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(arm 0 1)
      set(env ${env_${s}} ${arm${arm}})
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
        message(STATUS "${dump} ${s}: skipped (the setting cannot show it)")
        set(skipped TRUE)
        break()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT rc EQUAL 0 OR NOT flat MATCHES "${opRe}")
        message(SEND_ERROR "${dump} ${s} arm ${arm}: exit ${rc}, no one pass line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        break()
      endif()
      string(REGEX MATCH "${opRe}" m "${flat}")
      set(word_${arm} "${CMAKE_MATCH_1}")
      set(test_${arm} "${CMAKE_MATCH_2}")
      set(one_${arm} "${CMAKE_MATCH_3}")
      set(pairs_${arm} "${CMAKE_MATCH_4}")
      set(draws_${arm} -1)
      set(passes_${arm} -1)
      if(flat MATCHES "${drawsRe}")
        set(draws_${arm} "${CMAKE_MATCH_1}")
        set(passes_${arm} "${CMAKE_MATCH_2}")
      endif()
      set(traffic_${arm} "")
      if(flat MATCHES "${trafficRe}")
        set(traffic_${arm} "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    if(skipped)
      continue()
    endif()
    set(tag "${dump} ${s}")
    if(NOT word_0 STREQUAL "off" OR NOT one_0 EQUAL 0)
      message(SEND_ERROR "${tag}: arm 0 (${arm0}) says ${word_0} with ${one_0} pairs in one pass")
      math(EXPR failures "${failures} + 1")
    endif()
    if(SABOTAGE)
      if(NOT word_1 STREQUAL "off" OR NOT one_1 EQUAL 0 OR NOT test_1 MATCHES "^failed at texel")
        message(SEND_ERROR "${tag}: arm 1 (${arm1}) says ${word_1} (self-test ${test_1}) with ${one_1} pairs in one pass (want off, failed at texel, 0)")
        math(EXPR failures "${failures} + 1")
      endif()
    elseif(NOT word_1 STREQUAL "on" OR NOT test_1 MATCHES "^passed" OR one_1 GREATER pairs_1)
      message(SEND_ERROR "${tag}: arm 1 (${arm1}) says ${word_1} (self-test ${test_1}) with ${one_1} of ${pairs_1} pairs in one pass (want on, passed)")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT pairs_0 EQUAL pairs_1 OR NOT passes_0 EQUAL passes_1 OR draws_0 LESS 0 OR draws_1 GREATER draws_0)
      message(SEND_ERROR "${tag}: pairs ${pairs_0}/${pairs_1}, draws ${draws_0}/${draws_1}, passes ${passes_0}/${passes_1} (arm 0/arm 1: pairs and passes equal, arm 1's draws at most arm 0's)")
      math(EXPR failures "${failures} + 1")
    endif()
    if("${traffic_0}" STREQUAL "" OR NOT "${traffic_0}" STREQUAL "${traffic_1}")
      message(SEND_ERROR "${tag}: traffic \"${traffic_0}\" against \"${traffic_1}\" (want equal)")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR pairs_${s} "${pairs_${s}} + ${pairs_1}")
    math(EXPR one_${s} "${one_${s}} + ${one_1}")
    # every target byte for byte
    set(d0 "${OUT}/${index}-${s}-0")
    set(d1 "${OUT}/${index}-${s}-1")
    file(GLOB pngs0 RELATIVE "${d0}" "${d0}/*.png")
    file(GLOB pngs1 RELATIVE "${d1}" "${d1}/*.png")
    list(SORT pngs0)
    list(SORT pngs1)
    list(LENGTH pngs0 np)
    if(np EQUAL 0 OR NOT "${pngs0}" STREQUAL "${pngs1}")
      message(SEND_ERROR "${tag}: the targets written differ or none: ${pngs0} against ${pngs1}")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs0)
      if(NOT EXISTS "${d1}/${png}")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${d0}/${png}" "${d1}/${png}"
                      RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${tag}: ${png} differs (${arm0} against ${arm1})")
        math(EXPR failures "${failures} + 1")
      else()
        math(EXPR compared "${compared} + 1")
      endif()
    endforeach()
    if(failures EQUAL 0)
      file(REMOVE_RECURSE "${d0}" "${d1}")
    endif()
    message(STATUS "${tag}: ${np} targets, ${one_1} of ${pairs_1} pairs in one pass, draws ${draws_0} -> ${draws_1}")
  endforeach()
endforeach()
foreach(s IN LISTS settings)
  message(STATUS "rd_onepass_ab ${s}: ${one_${s}} of ${pairs_${s}} pairs in one pass over ${ndumps} fixtures")
  if(NOT SABOTAGE AND one_${s} EQUAL 0)
    message(SEND_ERROR "${s}: no pair in one pass over the list (the path never ran)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(compared EQUAL 0)
  message(SEND_ERROR "rd_onepass_ab: no target compared over ${ndumps} fixtures")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_onepass_ab: ${failures} failures over ${ndumps} fixtures")
endif()
message(STATUS "rd_onepass_ab: ${compared} targets the same bytes over ${ndumps} fixtures")
