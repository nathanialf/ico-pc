# rd_lamps_fillpaste_ab: the crack fill in the paste's pass (rd_lamps.h
# RD_LAMPS_FILLPASTE, rd_crack_fill.c deferred) against it off, byte for byte.
# For every fixture the list names (port/render/test/fixtures/lists/lamps.txt:
# fixture names of the repository, docs/port/TESTING_FIXTURES.md; a listed
# fixture that is absent or does not expand to its pinned SHA-256 FAILS; a
# line's word nocull replays that fixture with ICO_RD_CULL=off in every arm:
# the generated lamps sit about their models' origins, off the picture, and
# the visibility test drops them, so only with it off are they in the chain
# and the glow's plan the lamps'), expanded into OUT, then rd_replay_tool
# --pass-dump --stats at the PowerVR phone's
# Resolution Auto (Enhanced 2364x1080, Full height and pixel) under the tile
# class's fakes (ICO_VK_FAKE_LIMITS=moltenvk), ICO_RD_VU_PREPASS=0,
# ICO_RD_LATE_PASS=1, ICO_RD_CRACK_SWAP=1, the lamps on WORK2's own depth by
# the plain lowres (the 4-sample form is not taken on that class):
#   off     ICO_RD_LAMPS=pad+owed+wholepad+lowres
#   on      ...+fillpaste
#   poison  ...+fillpaste with ICO_RD_LAMPS_POISON=1 (the merged pass's
#           colour cleared to magenta where it loads nothing)
# Each on run against off: every PNG the same bytes; the stats' "lamps
# fillpaste on" with L fills drawn in the paste's pass; per landing one pass
# fewer (no aura) or the snapshot copy gone and at least 9 MB less colour
# loaded (in an aura's paste pass, which the fill shared already); colour
# loaded never more than off.  The path ran (rd_fixture_require on the on
# run): each fixture's declared lamps_fillpaste_fills (>=1 on the lamps'
# frames: the fill lands in the glow's paste drawn at list 11's first draw
# into SCENE; =0 where the chain is drawn whole for the sun's fans: it must
# stay byte-equal and cost no more); a fixture whose property was not checked
# FAILS; at least one fill drawn in the paste's pass over the list.  No
# device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
set(failures 0)
set(dumps "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
endforeach()
set(common ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0 ICO_RD_LATE_PASS=1 ICO_RD_CRACK_SWAP=1)
set(args --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_off "ICO_RD_LAMPS=pad+owed+wholepad+lowres")
set(env_on "ICO_RD_LAMPS=pad+owed+wholepad+lowres+fillpaste")
set(env_poison "ICO_RD_LAMPS=pad+owed+wholepad+lowres+fillpaste" ICO_RD_LAMPS_POISON=1)
set(passRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(fpRe "stats: lamps fillpaste (on|off): ([0-9]+) crack fills drawn in the paste's pass by the swap, ([0-9]+) flushed")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored")
set(landedTotal 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(name "${name_${dump}}")
  # the list's word: the cull off in every arm (the generated lamps kept)
  set(cull "")
  if(" ${RDFX_FIELDS_${name}} " MATCHES " nocull ")
    set(cull ICO_RD_CULL=off)
  endif()
  set(checked FALSE)
  foreach(arm off on poison)
    set(dir "${OUT}/${index}-${arm}")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${cull} ${env_${arm}} "${TOOL}"
                            "${dump}" "${dir}/out.png" --pass-dump "${dir}" --stats ${args}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    string(REPLACE ";" "," flat "${out}")
    if(NOT rc EQUAL 0 OR NOT flat MATCHES "${passRe}")
      message(SEND_ERROR "${dump} ${arm}: exit ${rc}:\n${err}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    if(arm STREQUAL "on")
      # the path ran: the fills this fixture declares, drawn in the paste's pass
      rd_fixture_require(${name} "${out}" lamps_fillpaste_fills)
      set(checked TRUE)
    endif()
    string(REGEX MATCH "${passRe}" m "${flat}")
    set(passes_${arm} "${CMAKE_MATCH_2}")
    set(landed_${arm} 0)
    if(flat MATCHES "${fpRe}")
      set(landed_${arm} "${CMAKE_MATCH_2}")
    endif()
    set(load_${arm} "?")
    if(flat MATCHES "${trafficRe}")
      set(load_${arm} "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  message(STATUS "${dump}: passes ${passes_off} -> ${passes_on} (${landed_on} fills in the paste's "
                 "pass), colour loaded ${load_off} -> ${load_on} MB")
  # per frame kind, a fill landed in the paste's pass:
  #   no aura (the title: the glow paste drawn early at list 11's first draw):
  #     the fill's own pass gone: one pass fewer;
  #   an aura (issue 163's gate): the fill already shared the aura's paste
  #     pass with the word off, by a snapshot copy: the passes equal, the copy
  #     gone and SCENE's colour no longer loaded there (at least 9 MB less);
  # each landing one or the other
  set(lessLoad 0)
  if(NOT load_on STREQUAL "?" AND NOT load_off STREQUAL "?")
    string(REGEX REPLACE "[.]" "" a0 "${load_off}")
    string(REGEX REPLACE "[.]" "" a1 "${load_on}")
    math(EXPR lessLoad "${a0} - ${a1}")
  endif()
  math(EXPR fewer "${passes_off} - ${passes_on}")
  math(EXPR copyKind "${landed_on} - ${fewer}")
  math(EXPR needLess "${copyKind} * 900")
  if(NOT landed_off EQUAL 0 OR fewer LESS 0 OR fewer GREATER landed_on OR
     lessLoad LESS needLess)
    message(SEND_ERROR "${dump}: ${passes_on} passes with fillpaste, ${passes_off} without, "
                       "${landed_on} fills in the paste's pass, colour loaded ${load_off} -> "
                       "${load_on} MB: each landing one pass fewer (no aura) or 9 MB of colour "
                       "less loaded (an aura's paste pass)")
    math(EXPR failures "${failures} + 1")
  endif()
  # never worse: colour loaded with the word on at most what it is off
  if(NOT load_on STREQUAL "?" AND NOT load_off STREQUAL "?")
    string(REGEX REPLACE "[.]" "" lon "${load_on}")
    string(REGEX REPLACE "[.]" "" loff "${load_off}")
    math(EXPR lon "${lon}")
    math(EXPR loff "${loff}")
    if(lon GREATER loff)
      message(SEND_ERROR "${dump}: colour loaded ${load_on} MB with fillpaste, ${load_off} MB "
                         "without: it must never load more")
      math(EXPR failures "${failures} + 1")
    endif()
  endif()
  math(EXPR landedTotal "${landedTotal} + ${landed_on}")
  if(NOT checked)
    message(SEND_ERROR "${name}: its declared fills were never checked (the on run did not replay)")
    math(EXPR failures "${failures} + 1")
  endif()
  foreach(arm on poison)
    file(GLOB pngs RELATIVE "${OUT}/${index}-off" "${OUT}/${index}-off/*.png")
    foreach(png IN LISTS pngs)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/${index}-off/${png}"
                              "${OUT}/${index}-${arm}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${arm}: ${png} differs: fillpaste must give the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(landedTotal EQUAL 0)
  message(SEND_ERROR "rd_lamps_fillpaste_ab: no crack fill drawn in a paste's pass over the list")
  math(EXPR failures "${failures} + 1")
endif()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_lamps_fillpaste_ab: ${failures} failures")
endif()
message(STATUS "rd_lamps_fillpaste_ab: ${landedTotal} fills in a paste's pass, the same bytes")
