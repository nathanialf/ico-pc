# rd_volume_fs_ab: the shadow volumes drawn by date_clear_ps (port/render/rd_volume_fs.c:
# no colour output, no discard) against sprite_ps, byte for byte, on every fixture the
# list names (port/render/test/fixtures/lists/volume_fs.txt: fixture names of the
# repository, docs/port/TESTING_FIXTURES.md; a listed fixture that is absent or does not
# expand to its pinned SHA-256 FAILS), expanded into OUT, then
# rd_replay_tool --pass-dump --stats, arm 0 ICO_RD_VOLUME_FS=0 against arm 1
# ICO_RD_VOLUME_FS=1 (ICO_RD_VOLUME_FS=sabotage with -DSABOTAGE=1), at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced 2364x1080, Full height, Full
#           pixel, presented at 2364x1080) under ICO_VK_FAKE_LIMITS=moltenvk with
#           ICO_RD_LATE_PASS=1 (the phone's shape: DATE through the stencil bit, the
#           volumes in SCENE's pass);
#   desk    lavapipe's own class, Enhanced 2x at 16:9;
#   mali    the Mali reporter's class (ICO_VK_FAKE_LIMITS=mali, no destination read, no
#           dual-source blending: the volumes' general key turned to sprite_ps_nodual)
#           at 1x, presented at 1612x720;
#   msaa    Enhanced 2x under MSAA 4x (the volumes in a multisampled pass keep sprite_ps:
#           the same bytes, the counts not asserted beyond taken <= seen).
# With -DSABOTAGE=1 (rd_volume_fs_sabotage) the phone's setting alone.
# A fixture a setting cannot show ("cannot show") is skipped there.
# Per fixture and setting it asserts:
#   - every PNG of --pass-dump the same bytes in both arms (the same set of targets);
#   - the same draws and passes ("stats: N draws, M passes") and the same traffic text
#     (colour, depth and stencil loaded, stored and cleared);
#   - the same volume draws seen in both arms; arm 0 "stats: volume fs off" with 0 by
#     date_clear_ps;
#   - arm 1 "volume fs on (self-test passed" with every volume draw by date_clear_ps at
#     phone, desk and mali (single-sampled: nothing the gate refuses), at most every one
#     at msaa;
#   - sabotage: arm 1 "off" with its self-test failed and 0 by date_clear_ps;
#   - the path ran (rd_fixture_require on arm 1 at phone, desk and mali; under the
#     sabotage at phone): the fixture's declared shadow_volume_draws (a fixture with
#     volumes shows volume draws, the one without shows none) and, not under the
#     sabotage, its declared volume_fs_taken (draws by date_clear_ps); a fixture the
#     run shows volume draws on has at least one by date_clear_ps there (every one,
#     the rule above); a fixture whose properties no setting checked FAILS;
# and over the list, at least one volume draw by date_clear_ps at phone, desk and mali
# (not under the sabotage).
# No device: SKIP (77).
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

set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(args_desk --enhanced --aspect 16:9 --resolution 2x)
set(args_mali --present 1612x720)
set(args_msaa --enhanced --resolution 2x --aa msaa4 --present 1280x720)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1)
set(env_desk "")
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DUAL=1)
set(env_msaa "")
if(SABOTAGE)
  set(settings phone)
  set(needTaken "")
  set(needRan phone)
  set(ranQuantities shadow_volume_draws)
  set(arm1 ICO_RD_VOLUME_FS=sabotage)
else()
  set(settings phone desk mali msaa)
  set(needTaken phone desk mali)
  set(needRan phone desk mali)
  set(ranQuantities shadow_volume_draws volume_fs_taken)
  set(arm1 ICO_RD_VOLUME_FS=1)
endif()
set(arm0 ICO_RD_VOLUME_FS=0)

# (the output's semicolons read as commas: the list separator replaced)
set(volRe "stats: volume fs (on|off) \\(self-test ([^)]*)\\): ([0-9]+) volume draws by date_clear_ps of ([0-9]+)[;,]")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(trafficRe "stats: traffic: (colour [0-9.]+ MB loaded [0-9.]+ MB stored, depth and stencil [0-9.]+/[0-9.]+ MB loaded/stored, [0-9.]+ MB cleared)")
foreach(s IN LISTS settings)
  set(seen_${s} 0)
  set(taken_${s} 0)
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
      set(out_${arm} "${out}")
      string(REPLACE ";" "," flat "${out}")
      if(NOT rc EQUAL 0 OR NOT flat MATCHES "${volRe}")
        message(SEND_ERROR "${dump} ${s} arm ${arm}: exit ${rc}, no volume fs line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        break()
      endif()
      string(REGEX MATCH "${volRe}" m "${flat}")
      set(word_${arm} "${CMAKE_MATCH_1}")
      set(test_${arm} "${CMAKE_MATCH_2}")
      set(taken_${arm} "${CMAKE_MATCH_3}")
      set(seen_${arm} "${CMAKE_MATCH_4}")
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
    if(NOT word_0 STREQUAL "off" OR NOT taken_0 EQUAL 0)
      message(SEND_ERROR "${tag}: arm 0 (${arm0}) says ${word_0} with ${taken_0} volume draws by date_clear_ps")
      math(EXPR failures "${failures} + 1")
    endif()
    if(SABOTAGE)
      if(NOT word_1 STREQUAL "off" OR NOT taken_1 EQUAL 0 OR NOT test_1 MATCHES "^failed at texel")
        message(SEND_ERROR "${tag}: arm 1 (${arm1}) says ${word_1} (self-test ${test_1}) with ${taken_1} volume draws by date_clear_ps (want off, failed at texel, 0)")
        math(EXPR failures "${failures} + 1")
      endif()
    else()
      set(allTaken TRUE)
      if(NOT s STREQUAL "msaa" AND NOT taken_1 EQUAL seen_1)
        set(allTaken FALSE)
      endif()
      if(NOT word_1 STREQUAL "on" OR NOT test_1 MATCHES "^passed" OR taken_1 GREATER seen_1 OR NOT allTaken)
        message(SEND_ERROR "${tag}: arm 1 (${arm1}) says ${word_1} (self-test ${test_1}) with ${taken_1} of ${seen_1} volume draws by date_clear_ps (want on, passed, every one single-sampled)")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    if(NOT seen_0 EQUAL seen_1 OR NOT draws_0 EQUAL draws_1 OR NOT passes_0 EQUAL passes_1 OR draws_0 LESS 0)
      message(SEND_ERROR "${tag}: volume draws seen ${seen_0}/${seen_1}, draws ${draws_0}/${draws_1}, passes ${passes_0}/${passes_1} (arm 0/arm 1: want equal)")
      math(EXPR failures "${failures} + 1")
    endif()
    # the path ran: the fixture's volume draws, and the entry taken for them
    list(FIND needRan "${s}" ranAt)
    if(ranAt GREATER -1)
      rd_fixture_require(${name_${dump}} "${out_1}" ${ranQuantities})
      set(required_${dump} TRUE)
    endif()
    if("${traffic_0}" STREQUAL "" OR NOT "${traffic_0}" STREQUAL "${traffic_1}")
      message(SEND_ERROR "${tag}: traffic \"${traffic_0}\" against \"${traffic_1}\" (want equal)")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR seen_${s} "${seen_${s}} + ${seen_1}")
    math(EXPR taken_${s} "${taken_${s}} + ${taken_1}")
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
    message(STATUS "${tag}: ${np} targets, ${seen_1} volume draws seen, ${taken_1} by date_clear_ps")
  endforeach()
endforeach()
foreach(s IN LISTS settings)
  message(STATUS "rd_volume_fs_ab ${s}: ${taken_${s}} of ${seen_${s}} volume draws by date_clear_ps over ${ndumps} fixtures")
endforeach()
foreach(s IN LISTS needTaken)
  if(taken_${s} EQUAL 0)
    message(SEND_ERROR "${s}: no volume draw by date_clear_ps over the list (the shadow fixtures draw volumes)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
foreach(dump IN LISTS dumps)
  if(NOT required_${dump})
    message(SEND_ERROR "${name_${dump}}: its properties were never checked (no setting could show it): a fixture this test cannot measure is no test")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(compared EQUAL 0)
  message(SEND_ERROR "rd_volume_fs_ab: no target compared over ${ndumps} fixtures")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_volume_fs_ab: ${failures} failures over ${ndumps} fixtures")
endif()
message(STATUS "rd_volume_fs_ab: ${compared} targets the same bytes over ${ndumps} fixtures")
