# rd_lamps_fillpaste_ab: the crack fill in the paste's pass (rd_lamps.h
# RD_LAMPS_FILLPASTE, rd_crack_fill.c deferred) against it off, byte for byte.
# For every dump of tools/lamps_dumps.txt (machine-local; a listed dump that is
# absent FAILS), rd_replay_tool --pass-dump --stats at the PowerVR phone's
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
# loaded (in an aura's paste pass, which the fill shared already); colour loaded never more than off (and fewer by about L SCENE
# textures, printed).  At least
# one fill drawn in the paste's pass over the list (issue 163's gate).  The
# corpus title's lamps frame cannot land (a sprite reads SCENE before its
# aura's paste: the fill must precede it): it must stay byte-equal and cost
# no more.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# The lists name dumps of the build machine alone (the owner's rule there: a
# listed dump that is absent FAILS). A machine without that folder has none of
# them: the test is skipped as the older dump tests are.
if(NOT IS_DIRECTORY "/primary/dev/ico-dumps")
  message(STATUS "SKIP: no dump folder on this machine (/primary/dev/ico-dumps): the list's dumps are the build machine's")
  return()
endif()
set(failures 0)
set(dumps "")
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_lamps_fillpaste_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${listDir}/${l}")
  endif()
  if(EXISTS "${l}")
    list(APPEND dumps "${l}")
  else()
    message(SEND_ERROR "${l}: listed but absent")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES dumps)
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
  foreach(arm off on poison)
    set(dir "${OUT}/${index}-${arm}")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${arm}} "${TOOL}" "${dump}" -
                            --pass-dump "${dir}" --stats ${args}
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
  # the phone's opening LAMPS frame (rc33 v746): the fill lands in the glow
  # paste drawn at list 11's first draw into SCENE
  if(dump MATCHES "v746[.]zip$" AND landed_on LESS 1)
    message(SEND_ERROR "${dump}: the phone's opening lamps frame must land")
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
if(failures GREATER 0)
  message(FATAL_ERROR "rd_lamps_fillpaste_ab: ${failures} failures")
endif()
message(STATUS "rd_lamps_fillpaste_ab: ${landedTotal} fills in a paste's pass, the same bytes")
