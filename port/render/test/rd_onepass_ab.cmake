# rd_onepass_ab: the one-pass blend (port/render/rd_onepass.h) against every
# pair in two passes, byte for byte, on every fixture the list names
# (port/render/test/fixtures/lists/onepass.txt: fixture names of the
# repository, docs/port/TESTING_FIXTURES.md; a listed fixture that is absent
# or does not expand to its pinned SHA-256 FAILS), expanded into OUT, then
# rd_replay_tool --pass-dump --stats, arm 0 ICO_RD_ONEPASS=0 (today's two
# passes, code 36 fused where it fuses) against arm 1 ICO_RD_ONEPASS=full
# (both classes in one pass without the device check, its pipelines made in
# the draw), at:
#   mali    the Mali reporter's class (ICO_VK_FAKE_LIMITS=mali, no destination
#           read, no dual-source blending) with every state in the pipeline
#           keys (ICO_VK_FAKE_NO_DYNAMIC=1, the Mali-G52's: the one-pass key's
#           own static pipeline) at 1x, presented at 1612x720;
#   malidyn the same with lavapipe's dynamic blend state (the one-pass key
#           folded into the colour pass's pipeline, its blend state and the
#           constant set at each bind).
# Per fixture and setting it asserts:
#   - every PNG of --pass-dump the same bytes in both arms (the same set of
#     targets), the same traffic text;
#   - arm 0 "stats: one-pass blend off" with 0 pairs in one pass;
#   - arm 1 "stats: one-pass blend on" with at most its pairs in one pass;
#   - arm 2, arm 1 with the in-play judge at its off arm on every replay
#     (ICO_RD_AB=off): on, 0 pairs in one pass, every pair arm 1 took kept
#     as "timed without it" (the judge's way back taken), every PNG arm 0's;
# and over the list, at each setting, at least one pair in one pass and at
# least one of them code 36's (the path ran, the cut class included).  Each
# fixture's counts are printed.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder>
include("${FIXTURE_HELPERS}")
set(failures 0)
set(dumps "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
endforeach()
list(LENGTH dumps ndumps)

set(args_mali --present 1612x720)
set(args_malidyn --present 1612x720)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DUAL=1
    ICO_VK_FAKE_NO_DYNAMIC=1)
set(env_malidyn ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DUAL=1)
set(settings mali malidyn)
set(arm0 ICO_RD_ONEPASS=0)
set(arm1 ICO_RD_ONEPASS=full)
set(arm2 ICO_RD_ONEPASS=full ICO_RD_AB=off)

# (the output's semicolons read as commas: the list separator replaced)
set(opRe "stats: one-pass blend (on|off) \\(([^)]*)\\): ([0-9]+) pairs in one pass of ([0-9]+) \\(([0-9]+) of code 36\\), ([0-9]+) triangles, ([0-9]+) code 36 commands unfused")
set(trialRe "stats: one-pass blend [^\n]*, timed without it ([0-9]+)")
set(trafficRe "stats: traffic: (colour [0-9.]+ MB loaded [0-9.]+ MB stored, depth and stencil [0-9.]+/[0-9.]+ MB loaded/stored, [0-9.]+ MB cleared)")
foreach(s IN LISTS settings)
  set(pairs_${s} 0)
  set(one_${s} 0)
  set(cut_${s} 0)
endforeach()
set(compared 0)

set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(arm 0 1 2)
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
        message(SEND_ERROR "${dump} ${s} arm ${arm}: exit ${rc}, no one-pass line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        break()
      endif()
      string(REGEX MATCH "${opRe}" m "${flat}")
      set(word_${arm} "${CMAKE_MATCH_1}")
      set(one_${arm} "${CMAKE_MATCH_3}")
      set(pairs_${arm} "${CMAKE_MATCH_4}")
      set(cut_${arm} "${CMAKE_MATCH_5}")
      set(unfused_${arm} "${CMAKE_MATCH_7}")
      set(trial_${arm} -1)
      if(flat MATCHES "${trialRe}")
        set(trial_${arm} "${CMAKE_MATCH_1}")
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
    if(NOT word_1 STREQUAL "on" OR one_1 GREATER pairs_1)
      message(SEND_ERROR "${tag}: arm 1 (${arm1}) says ${word_1} with ${one_1} of ${pairs_1} pairs in one pass (want on)")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT word_2 STREQUAL "on" OR NOT one_2 EQUAL 0 OR NOT unfused_2 EQUAL 0 OR trial_2 LESS one_1)
      message(SEND_ERROR "${tag}: arm 2 (${arm2}) says ${word_2} with ${one_2} pairs in one pass, ${unfused_2} unfused, ${trial_2} timed without it (want on, 0, 0, at least arm 1's ${one_1})")
      math(EXPR failures "${failures} + 1")
    endif()
    if("${traffic_0}" STREQUAL "" OR NOT "${traffic_0}" STREQUAL "${traffic_1}")
      message(SEND_ERROR "${tag}: traffic \"${traffic_0}\" against \"${traffic_1}\" (want equal)")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR pairs_${s} "${pairs_${s}} + ${pairs_1}")
    math(EXPR one_${s} "${one_${s}} + ${one_1}")
    math(EXPR cut_${s} "${cut_${s}} + ${cut_1}")
    # every target byte for byte
    set(d0 "${OUT}/${index}-${s}-0")
    set(d1 "${OUT}/${index}-${s}-1")
    set(d2 "${OUT}/${index}-${s}-2")
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
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${d0}/${png}" "${d2}/${png}"
                      RESULT_VARIABLE same2)
      if(NOT same2 EQUAL 0)
        message(SEND_ERROR "${tag}: ${png} differs (${arm0} against ${arm2})")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    if(failures EQUAL 0)
      file(REMOVE_RECURSE "${d0}" "${d1}" "${d2}")
    endif()
    message(STATUS "${tag}: ${np} targets, ${one_1} of ${pairs_1} pairs in one pass (${cut_1} of code 36), ${unfused_1} code 36 commands unfused")
  endforeach()
endforeach()
foreach(s IN LISTS settings)
  message(STATUS "rd_onepass_ab ${s}: ${one_${s}} of ${pairs_${s}} pairs in one pass (${cut_${s}} of code 36) over ${ndumps} fixtures")
  if(one_${s} EQUAL 0 OR cut_${s} EQUAL 0)
    message(SEND_ERROR "${s}: ${one_${s}} pairs in one pass, ${cut_${s}} of code 36, over the list (the path never ran for a class)")
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
