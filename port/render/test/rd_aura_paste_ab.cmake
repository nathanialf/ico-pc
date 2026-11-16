# rd_aura_paste_ab: the aura's paste as the alpha alone over the rows that read
# only proven alpha 0 rows of FEED128 (port/render/rd_aura_paste.c), the split
# off (ICO_RD_AURA_PASTE=0) against on (=1).  For every fixture the list names
# (port/render/test/fixtures/lists/aura_paste.txt: fixture names of the
# repository, docs/port/TESTING_FIXTURES.md; a listed fixture that is absent or
# does not expand to its pinned SHA-256 FAILS; after the name the line's own
# properties, rd_fixture_list_props.cmake), rd_replay_tool's --pass-dump with
# --stats and ICO_RD_VU_PREPASS=0 at the PowerVR phone's Resolution Auto and
# Enhanced 2x 4:3 (both ICO_VK_FAKE_LIMITS=moltenvk: the local read) and a
# desktop 16:9 window (lavapipe's own profile): every PNG byte for byte, the
# same passes and attachment bytes (the split draws in the paste's own pass),
# the off run splitting nothing, every split at the phone's setting with at
# most 32 band rows, at least one split over the list.  The path ran
# (rd_fixture_require on the on run at the phone's setting): each fixture's
# declared aura_paste_split, the pastes split (the count of the frame's
# aura pastes whose FEED128 rows the chain proves alpha 0: the chain is the
# frame's commands, which the fixture keeps); a fixture the phone's setting
# did not replay FAILS.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(failures 0)
set(dumps "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  rd_fixture_list_props(${name})
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
  set(checked_${name} FALSE)
endforeach()
list(LENGTH dumps ndumps)

set(common ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(args_enh43 --enhanced --resolution 2x --aspect 1.3333 --present 1280x960)
set(env_enh43 ICO_VK_FAKE_LIMITS=moltenvk)
set(args_desk --enhanced --resolution 1920x1080 --full-height --full-pixel --present 1920x1080)
set(env_desk "")
set(settings phone enh43 desk)

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(apRe "stats: aura paste (on|off): ([0-9]+) split \\(([0-9]+) rows as the alpha alone, ([0-9]+) band rows\\), ([0-9]+) drawn whole")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored, depth and stencil ([0-9.]+)/([0-9.]+) MB loaded/stored, [0-9.]+ MB cleared, ([0-9.]+) MB copied")

set(compared 0)
set(splitTotal 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(ap 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${ap}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}} ICO_RD_AURA_PASTE=${ap}
                              "${TOOL}" "${dump}" "${dir}-out.png" --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND ap EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} aura paste ${ap}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      set(out_${ap} "${out}")
      set(ok TRUE)
      foreach(re drawsRe apRe trafficRe)
        if(NOT flat MATCHES "${${re}}")
          message(SEND_ERROR "${dump} ${s} aura paste ${ap}: no ${re} line:\n${out}")
          math(EXPR failures "${failures} + 1")
          set(ok FALSE)
        endif()
      endforeach()
      if(NOT ok)
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(passes_${ap} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${apRe}" m "${flat}")
      set(word_${ap} "${CMAKE_MATCH_1}")
      set(split_${ap} "${CMAKE_MATCH_2}")
      set(rows_${ap} "${CMAKE_MATCH_3}")
      set(band_${ap} "${CMAKE_MATCH_4}")
      set(whole_${ap} "${CMAKE_MATCH_5}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(traffic_${ap} "${CMAKE_MATCH_1}/${CMAKE_MATCH_2} ${CMAKE_MATCH_3}/${CMAKE_MATCH_4} ${CMAKE_MATCH_5}")
      message(STATUS "${dump} ${s} aura paste ${ap}: ${passes_${ap}} passes; ${split_${ap}} split "
                     "(${rows_${ap}} rows as the alpha alone, ${band_${ap}} band rows), "
                     "${whole_${ap}} whole; traffic ${traffic_${ap}}")
    endforeach()
    if(skipped)
      continue()
    endif()
    if(NOT word_0 STREQUAL "off" OR NOT split_0 EQUAL 0 OR NOT word_1 STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: the split says ${word_0} (${split_0}) off and ${word_1} on")
      math(EXPR failures "${failures} + 1")
    endif()
    # the split draws in the paste's own pass: the same passes and bytes moved
    if(NOT passes_1 EQUAL passes_0 OR NOT traffic_1 STREQUAL traffic_0)
      message(SEND_ERROR "${dump} ${s}: ${passes_1} passes, traffic ${traffic_1} with the split; "
                         "${passes_0} and ${traffic_0} without")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "phone")
      # the path ran: the pastes this fixture declares split at the phone's setting
      set(name "${name_${dump}}")
      rd_fixture_require(${name} "${out_1}" aura_paste_split)
      set(checked_${name} TRUE)
      if(split_1 GREATER 0 AND band_1 GREATER 32)
        message(SEND_ERROR "${dump} ${s}: ${split_1} split with ${band_1} band rows; the band "
                           "within 32 rows (2 FEED128 rows of B-spline reach at 9.6 SCENE rows "
                           "each, and the margins)")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    math(EXPR splitTotal "${splitTotal} + ${split_1}")
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written with the split off, ${npngOn} on")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the split on: it must be the same "
                           "bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
foreach(name IN LISTS names)
  if(NOT checked_${name})
    message(SEND_ERROR "${name}: not replayed at the phone's setting: its split not checked")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_aura_paste_ab: ${failures} failures over ${ndumps} fixtures")
endif()
if(compared EQUAL 0 OR splitTotal EQUAL 0)
  message(FATAL_ERROR "rd_aura_paste_ab: ${compared} targets compared, ${splitTotal} pastes split "
                      "over ${ndumps} fixtures: one at least expected")
endif()
message(STATUS "rd_aura_paste_ab: ${compared} targets the same bytes over ${ndumps} fixtures; "
               "${splitTotal} pastes split")
