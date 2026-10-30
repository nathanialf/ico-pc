# rd_vumerge_fallback: the draw merge's fallback when the driver refuses a
# merge build (docs/port/VU1_PROGRAMS.md, "The draw merge"; rd_replay_vu.c
# rd__vu_merge_fallback).  rd_replay_tool on DUMP (rd_pixel_vu.rddump: 64
# code-32 draws the merge joins) with --time 3 under ICO_RD_BLEND=fixed,
# three times: the merge on, ICO_RD_VU_MERGE=0, and ICO_RD_FAKE_MERGE_FAIL=1
# (the first pipeline made with a merge build is kept as failed, as a build
# FXC refused on Direct3D 11).  The faked run must log one "pipeline creation
# failed ... faked" line naming the merge build, then once "rd: VU merge:
# off: the driver could not compile <build> (<backend>); one GPU draw a VU
# draw from here" (matched with its ";" read as ","), and its timed replays (the second on: the merge is off
# from the replay after the failure) must draw what ICO_RD_VU_MERGE=0 draws
# (the time line's draws a replay), which must be more than the merge's.
# A Direct3D 11 debug-layer error in any run fails it.  No dump: SKIP; no
# device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DDUMP=<rd_pixel_vu.rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump")
  return()
endif()
file(MAKE_DIRECTORY "${OUT}")
set(timeRe "time: 3 replays, [^\n]* ([0-9]+\\.[0-9]) draws, ")
set(failures 0)
foreach(run on off fake)
  set(env_on "")
  set(env_off "ICO_RD_VU_MERGE=0")
  set(env_fake "ICO_RD_FAKE_MERGE_FAIL=1")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ${env_${run}} "${TOOL}"
                          "${DUMP}" "${OUT}/${run}.png" --time 3
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rd_vumerge_fallback ${run}: exit ${rc}:\n${out}\n${err}")
  endif()
  # the tool's lines with their ";" taken out (a ";" would split a list item)
  string(REPLACE ";" "," all_${run} "${out}\n${err}")
  if(all_${run} MATCHES "rhi_d3d11 debug (error|corruption)[^\n]*")
    message(SEND_ERROR "rd_vumerge_fallback ${run}: ${CMAKE_MATCH_0}")
    math(EXPR failures "${failures} + 1")
  endif()
  if(NOT all_${run} MATCHES "${timeRe}")
    message(FATAL_ERROR "rd_vumerge_fallback ${run}: no time line:\n${out}\n${err}")
  endif()
  set(draws_${run} "${CMAKE_MATCH_1}")
  message(STATUS "rd_vumerge_fallback ${run}: ${draws_${run}} draws a timed replay")
endforeach()
string(REGEX MATCHALL "pipeline creation failed[^\n]*" failLines "${all_fake}")
list(LENGTH failLines nFail)
if(NOT nFail EQUAL 1 OR NOT failLines MATCHES "faked .*vertex shader vu_[a-z_]*merge[a-z_]*_vs, the draw merge's build")
  message(SEND_ERROR "rd_vumerge_fallback: ${nFail} pipeline failure lines, want one faked one "
                     "naming a merge build:\n${failLines}")
  math(EXPR failures "${failures} + 1")
endif()
string(REGEX MATCHALL "rd: VU merge: off: the driver could not compile vu_[a-z_]*merge[a-z_]*_vs \\([A-Za-z0-9 ]+\\), one GPU draw a VU draw from here" offLines "${all_fake}")
list(LENGTH offLines nOff)
if(NOT nOff EQUAL 1)
  message(SEND_ERROR "rd_vumerge_fallback: ${nOff} merge-off lines, want one:\n${all_fake}")
  math(EXPR failures "${failures} + 1")
endif()
if(NOT draws_fake STREQUAL draws_off)
  message(SEND_ERROR "rd_vumerge_fallback: the faked run's timed replays drew ${draws_fake} a "
                     "replay, ICO_RD_VU_MERGE=0 ${draws_off}")
  math(EXPR failures "${failures} + 1")
endif()
if(NOT draws_on LESS draws_off)
  message(SEND_ERROR "rd_vumerge_fallback: the merge drew ${draws_on} a replay against "
                     "${draws_off} one a draw: the dump has nothing to merge")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vumerge_fallback: ${failures} failures")
endif()
message(STATUS "rd_vumerge_fallback: merge ${draws_on}, off ${draws_off}, after the faked "
               "failure ${draws_fake} draws a timed replay")
