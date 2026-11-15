# rd_paste_spec_ab: the glow's paste drawn by its own entry (port/render/rd_paste_spec.c,
# fx_paste_ps: fx_frame_ps's paste path alone) against fx_frame_ps, byte for byte, on
# every fixture of the list (port/render/test/fixtures/lists/paste_spec.txt: committed
# frames with no art, docs/port/TESTING_FIXTURES.md; a line's word nocull replays that
# fixture with ICO_RD_CULL=off in both arms, the generated lamps in the chain: the
# LAMPS plan's paste), rd_replay_tool --pass-dump --stats, arm 0 ICO_RD_PASTE_SPEC=0
# against arm 1 ICO_RD_PASTE_SPEC=1 (ICO_RD_PASTE_SPEC=sabotage with -DSABOTAGE=1), at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced 2364x1080, Full height,
#           Full pixel, presented at 2364x1080) under ICO_VK_FAKE_LIMITS=moltenvk with
#           ICO_RD_LATE_PASS=1 (the paste deferred to list 11's pass, the crack fill in
#           the paste's pass where it lands: the phone's shape);
#   desk    lavapipe's own class, Enhanced 2x at 16:9;
#   orig    Original 4:3 at 1x.
# With BACKEND opengl (rd_paste_spec_gl): the phone's setting on OpenGL alone; with
# -DSABOTAGE=1 (rd_paste_spec_sabotage) the phone's setting alone.
# Per fixture and setting it asserts:
#   - every PNG of --pass-dump the same bytes in both arms (the same set of targets);
#   - the same draws and passes ("stats: N draws, M passes");
#   - arm 0's "stats: paste spec off" with 0 pastes by fx_paste_ps;
#   - Vulkan: arm 1's "paste spec on (self-test passed" (a premultiplied add the gate
#     refuses, another key or form, keeps fx_frame_ps: counted, not failed);
#   - OpenGL: arm 1 "off" with 0 pastes by it (the module is Vulkan's alone);
#   - sabotage: arm 1 "off" with its self-test failed and 0 pastes by it;
#   - the path ran: at the phone's setting (OpenGL: its one) arm 1 shows the pastes
#     the fixture declares (paste_spec_seen, on the fixture or its list line) and,
#     on Vulkan without the sabotage, the ones by fx_paste_ps (paste_spec_taken);
#     a fixture no setting checked fails;
# and over the list, at least one paste seen at phone and desk and, on Vulkan without
# the sabotage, at least one drawn by fx_paste_ps.
# No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the fixtures'
# folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<the list> -DOUT=<a folder>
# -DBACKEND=vulkan|opengl [-DSABOTAGE=1]
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(failures 0)
rd_fixture_list("${LIST}" names)
list(LENGTH names ndumps)

set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(args_desk --enhanced --aspect 16:9 --resolution 2x)
set(args_orig --aspect 4:3)
set(args_gl ${args_phone})
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1)
set(env_desk "")
set(env_orig "")
set(env_gl ICO_RHI_BACKEND=opengl ICO_RD_LATE_PASS=1)
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
  set(needSeen gl)
elseif(SABOTAGE)
  set(settings phone)
  set(needSeen phone)
else()
  set(settings phone desk orig)
  set(needSeen phone desk)
endif()
set(arm0 ICO_RD_PASTE_SPEC=0)
if(SABOTAGE)
  set(arm1 ICO_RD_PASTE_SPEC=sabotage)
else()
  set(arm1 ICO_RD_PASTE_SPEC=1)
endif()

# (the output's semicolons read as commas: the list separator replaced)
set(specRe "stats: paste spec (on|off) \\(self-test ([^)]*)\\): ([0-9]+) pastes by fx_paste_ps of ([0-9]+)[;,]")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
foreach(s IN LISTS settings)
  set(seen_${s} 0)
  set(taken_${s} 0)
endforeach()

foreach(index IN LISTS names)
  rd_fixture_expand(${index} "${OUT}/fixtures" dump)
  rd_fixture_list_props(${index})
  rd_fixture_list_word(${index} nocull nocull)
  set(cull "")
  if(nocull)
    set(cull ICO_RD_CULL=off)
  endif()
  set(checked FALSE)
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(arm 0 1)
      set(env ${env_${s}} ${cull} ${arm${arm}})
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
        message(STATUS "${index} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        break()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT rc EQUAL 0 OR NOT flat MATCHES "${specRe}")
        message(SEND_ERROR "${index} ${s} arm ${arm}: exit ${rc}, no paste spec line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        break()
      endif()
      string(REGEX MATCH "${specRe}" m "${flat}")
      set(word_${arm} "${CMAKE_MATCH_1}")
      set(test_${arm} "${CMAKE_MATCH_2}")
      set(taken_${arm} "${CMAKE_MATCH_3}")
      set(seen_${arm} "${CMAKE_MATCH_4}")
      set(out_${arm} "${out}")
      set(draws_${arm} -1)
      set(passes_${arm} -1)
      if(flat MATCHES "${drawsRe}")
        set(draws_${arm} "${CMAKE_MATCH_1}")
        set(passes_${arm} "${CMAKE_MATCH_2}")
      endif()
    endforeach()
    if(skipped)
      continue()
    endif()
    set(tag "${index} ${s}")
    # the path ran: the pastes the fixture declares, and the ones by fx_paste_ps
    if(s STREQUAL "phone" OR s STREQUAL "gl")
      if("${BACKEND}" STREQUAL "opengl" OR SABOTAGE)
        rd_fixture_require(${index} "${out_1}" paste_spec_seen)
      else()
        rd_fixture_require(${index} "${out_1}" paste_spec_seen paste_spec_taken)
      endif()
      set(checked TRUE)
    endif()
    # the words and the counts
    if(NOT word_0 STREQUAL "off" OR NOT taken_0 EQUAL 0)
      message(SEND_ERROR "${tag}: arm 0 (${arm0}) says ${word_0} with ${taken_0} pastes by fx_paste_ps")
      math(EXPR failures "${failures} + 1")
    endif()
    if("${BACKEND}" STREQUAL "opengl" OR SABOTAGE)
      if(NOT word_1 STREQUAL "off" OR NOT taken_1 EQUAL 0)
        message(SEND_ERROR "${tag}: arm 1 (${arm1}) says ${word_1} with ${taken_1} pastes by fx_paste_ps (want off, 0)")
        math(EXPR failures "${failures} + 1")
      endif()
      if(SABOTAGE AND NOT test_1 MATCHES "^failed at texel")
        message(SEND_ERROR "${tag}: the sabotaged self-test says \"${test_1}\" (want failed at texel)")
        math(EXPR failures "${failures} + 1")
      endif()
    else()
      if(NOT word_1 STREQUAL "on" OR NOT test_1 MATCHES "^passed" OR taken_1 GREATER seen_1)
        message(SEND_ERROR "${tag}: arm 1 (${arm1}) says ${word_1} (self-test ${test_1}) with ${taken_1} of ${seen_1} pastes by fx_paste_ps (want on, passed)")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    if(NOT seen_0 EQUAL seen_1 OR NOT draws_0 EQUAL draws_1 OR NOT passes_0 EQUAL passes_1 OR draws_0 LESS 0)
      message(SEND_ERROR "${tag}: pastes seen ${seen_0}/${seen_1}, draws ${draws_0}/${draws_1}, passes ${passes_0}/${passes_1} (arm 0/arm 1: want equal)")
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
      endif()
    endforeach()
    message(STATUS "${tag}: ${np} targets, ${seen_1} pastes seen, ${taken_1} by fx_paste_ps")
  endforeach()
  if(NOT checked)
    message(SEND_ERROR "${index}: its declared properties were checked at no setting (the "
                       "phone's setting did not run it)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
foreach(s IN LISTS needSeen)
  message(STATUS "rd_paste_spec_ab ${s}: ${taken_${s}} of ${seen_${s}} pastes by fx_paste_ps over ${ndumps} fixtures")
  if(seen_${s} EQUAL 0)
    message(SEND_ERROR "${s}: no paste seen over the list (the lamps frames paste the glow)")
    math(EXPR failures "${failures} + 1")
  endif()
  if(NOT "${BACKEND}" STREQUAL "opengl" AND NOT SABOTAGE AND taken_${s} EQUAL 0)
    message(SEND_ERROR "${s}: no paste drawn by fx_paste_ps over the list")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_paste_spec_ab: ${failures} failures over ${ndumps} fixtures")
endif()
