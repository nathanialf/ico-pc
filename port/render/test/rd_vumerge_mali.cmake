# rd_vumerge_mali: the VU draw merge on a phone without dual-source blending
# (issue 164: a Mali-G52, where every lerp draw is two passes) against the
# plain device, byte for byte (docs/port/VU1_PROGRAMS.md, "The draw merge
# without dual-source blending").  For every dump the list names
# (tools/vumerge_mali_dumps.txt: a dump, optionally expect=IN/GPU, then
# rd_replay_tool's setting arguments), rd_replay_tool's --pass-dump with
# --stats twice under ICO_RD_BLEND=fixed: on the plain device and under
# ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DUAL=1 (a Mali-G68's limits,
# vk_device.c vkr_fake_limits, and no dualSrcBlend: the two-pass blend,
# rd_pipeline.c rd__expand_no_dual).  Every named target's PNG must be equal
# byte for byte; the faked run must say both fakes took; the draw merge's
# figures ("N draws in G GPU draws (IN merged into GPU, A alone") and the
# fused pairs' count must be the plain run's, and with expect= the merged
# draws and their GPU draws that figure.  A missing list or dump is named
# and skipped; no dump read, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
if(NOT EXISTS "${LIST}")
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(mergeRe "stats: VU merge: ([0-9]+) draws in ([0-9]+) GPU draws \\(([0-9]+) merged into ([0-9]+), ([0-9]+) alone, merge (on|off)\\)")
set(fuseRe "stats: VU fuse: ([0-9]+) pairs fused")
set(compared 0)
set(failures 0)
set(ndumps 0)
set(index 0)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  separate_arguments(words UNIX_COMMAND "${l}")
  list(POP_FRONT words dump)
  if(NOT IS_ABSOLUTE "${dump}")
    set(dump "${listDir}/${dump}")
  endif()
  if(NOT EXISTS "${dump}")
    message(STATUS "${dump}: no such dump, skipped")
    continue()
  endif()
  set(expect "")
  list(LENGTH words nw)
  if(nw GREATER 0)
    list(GET words 0 w0)
    if(w0 MATCHES "^expect=([0-9]+)/([0-9]+)$")
      set(expect "${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
      list(POP_FRONT words)
    endif()
  endif()
  set(args ${words})
  math(EXPR index "${index} + 1")
  math(EXPR ndumps "${ndumps} + 1")
  set(dirPlain "${OUT}/${index}-plain")
  set(dirMali "${OUT}/${index}-mali")
  file(REMOVE_RECURSE "${dirPlain}" "${dirMali}")
  file(MAKE_DIRECTORY "${dirPlain}" "${dirMali}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed "${TOOL}" "${dump}"
                          /dev/null --pass-dump "${dirPlain}" --stats ${args}
                  RESULT_VARIABLE rcPlain OUTPUT_VARIABLE outPlain ERROR_VARIABLE errPlain)
  if(rcPlain EQUAL 77)
    message(STATUS "SKIP: no device: ${errPlain}")
    return()
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_VK_FAKE_LIMITS=mali
                          ICO_VK_FAKE_NO_DUAL=1 "${TOOL}" "${dump}" /dev/null --pass-dump
                          "${dirMali}" --stats ${args}
                  RESULT_VARIABLE rcMali OUTPUT_VARIABLE outMali ERROR_VARIABLE errMali)
  if(NOT rcPlain EQUAL 0 OR NOT rcMali EQUAL 0)
    message(SEND_ERROR "${dump}: exit ${rcPlain} (plain) and ${rcMali} (Mali limits, no "
                       "dual-source blending):\n${errPlain}\n${errMali}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(NOT errMali MATCHES "ICO_VK_FAKE_LIMITS=mali: the device's limits are clamped" OR
     NOT errMali MATCHES "no dualSrcBlend \\(ICO_VK_FAKE_NO_DUAL\\)")
    message(SEND_ERROR "${dump}: the fakes did not take (no Mali limits or no two-pass "
                       "blend line):\n${errMali}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  # the tool's lines with their ";" taken out (a ";" would split a list item)
  string(REPLACE ";" "," flatPlain "${outPlain}")
  string(REPLACE ";" "," flatMali "${outMali}")
  if(NOT flatPlain MATCHES "${mergeRe}" OR NOT CMAKE_MATCH_6 STREQUAL "on")
    message(SEND_ERROR "${dump}: the plain run's merge is not on:\n${outPlain}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  set(figPlain "${CMAKE_MATCH_1} draws in ${CMAKE_MATCH_2} GPU draws (${CMAKE_MATCH_3} merged into ${CMAKE_MATCH_4}, ${CMAKE_MATCH_5} alone)")
  set(joinedPlain "${CMAKE_MATCH_3}/${CMAKE_MATCH_4}")
  if(NOT flatMali MATCHES "${mergeRe}" OR NOT CMAKE_MATCH_6 STREQUAL "on")
    message(SEND_ERROR "${dump}: the Mali run's merge is not on:\n${outMali}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  set(figMali "${CMAKE_MATCH_1} draws in ${CMAKE_MATCH_2} GPU draws (${CMAKE_MATCH_3} merged into ${CMAKE_MATCH_4}, ${CMAKE_MATCH_5} alone)")
  set(fusePlain "?")
  set(fuseMali "?")
  if(flatPlain MATCHES "${fuseRe}")
    set(fusePlain "${CMAKE_MATCH_1}")
  endif()
  if(flatMali MATCHES "${fuseRe}")
    set(fuseMali "${CMAKE_MATCH_1}")
  endif()
  message(STATUS "${dump}: plain ${figPlain}, ${fusePlain} pairs fused; Mali limits without "
                 "dual-source blending ${figMali}, ${fuseMali} pairs fused")
  if(NOT figMali STREQUAL figPlain)
    message(SEND_ERROR "${dump}: the merge under the Mali limits without dual-source blending "
                       "(${figMali}) is not the plain device's (${figPlain})")
    math(EXPR failures "${failures} + 1")
  endif()
  if(NOT fuseMali STREQUAL fusePlain)
    message(SEND_ERROR "${dump}: ${fuseMali} pairs fused without dual-source blending, "
                       "${fusePlain} on the plain device")
    math(EXPR failures "${failures} + 1")
  endif()
  if(expect AND NOT joinedPlain STREQUAL expect)
    message(SEND_ERROR "${dump}: ${joinedPlain} (merged/GPU draws) on the plain device, "
                       "${expect} expected")
    math(EXPR failures "${failures} + 1")
  endif()
  file(GLOB pngs RELATIVE "${dirPlain}" "${dirPlain}/*.png")
  list(LENGTH pngs npng)
  if(npng EQUAL 0)
    message(SEND_ERROR "${dump}: no target written")
    math(EXPR failures "${failures} + 1")
  endif()
  foreach(png IN LISTS pngs)
    if(NOT EXISTS "${dirMali}/${png}")
      message(SEND_ERROR "${dump}: ${png} written on the plain device only")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirPlain}/${png}"
                            "${dirMali}/${png}" RESULT_VARIABLE same)
    math(EXPR compared "${compared} + 1")
    if(NOT same EQUAL 0)
      message(SEND_ERROR "${dump}: ${png} differs between the plain device and the Mali "
                         "limits without dual-source blending")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vumerge_mali: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} could be compared")
  return()
endif()
message(STATUS "rd_vumerge_mali: ${compared} targets equal over ${ndumps} dumps, the merge's "
               "figures the plain device's")
