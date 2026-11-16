# rd_vu_audit: the VU record path's bounds audit (port/render/rd_vu_audit.h)
# over issue 179's Mali-G52 frames under the Arm fakes at the reporter's
# settings (1x, 21:9, half height, presented at 1612x720), the record by
# compute dispatches and made at start-up (ICO_RD_EARLY_PREPARE=1: 512
# rows, as the phone made it), the volumes' stencil-only entry on as the
# phone ran it.  Each frame is replayed four times (both frame slots, each
# slot's copy read back when the slot comes round, the last two after the
# --stats wait) with ICO_RD_VU_AUDIT=1 in three arms:
#   on       the index compaction on (ICO_RD_VU_COMPACT=1), the draw merge on:
#            v0.11.6's path on the phone
#   nomerge  the compaction on, the merge off (ICO_RD_VU_MERGE=0: the values
#            not raised)
#   off      the compaction off (ICO_RD_VU_COMPACT=0): v0.11.5's path
# Every run must print the audit's run line with 0 violations and no
# violation line; the on arms must have read compacted lists back (more
# than 0 lists from more than 0 readbacks) and drawn from the fourth plane.
# The run lines are printed (the numbers of the hand-back).  The frames are
# committed fixtures (lists/vu_audit.txt; docs/port/TESTING_FIXTURES.md: the
# command structure kept, the geometry generated), so the counts are the
# fixtures'; the path ran: each run's declared vu_audit_entries and
# vu_audit_violations, and the on arms' vu_audit_lists and
# vu_audit_plane_draws (rd_fixture_helpers.cmake).  No device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<dir>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<lists/vu_audit.txt>
# -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
rd_fixture_list("${LIST}" names)
set(env_base ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute
             ICO_RD_EARLY_PREPARE=1 ICO_RD_VOLUME_FS=1 ICO_RD_VU_AUDIT=1)
set(env_on ICO_RD_VU_COMPACT=1)
set(env_nomerge ICO_RD_VU_COMPACT=1 ICO_RD_VU_MERGE=0)
set(env_off ICO_RD_VU_COMPACT=0)
set(runRe "rd: VU audit: ([0-9]+) entries \\(([0-9]+) index values\\), ([0-9]+) compacted lists from ([0-9]+) readbacks \\(([0-9]+) values, ([0-9]+) filler triangles\\), draws: ([0-9]+) of the fourth plane[^\n]*; ([0-9]+) violations")
set(failures 0)
set(index 0)
foreach(name IN LISTS names)
  rd_fixture_list_props(${name})
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)
  set(offSpan "")
  set(offRatio "")
  foreach(arm off on nomerge)
    math(EXPR index "${index} + 1")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_base} ${env_${arm}} "${TOOL}"
                            "${dump}" "${OUT}/${index}-${arm}.png" --aspect 21:9
                            --present 1612x720 --repeat 4 --stats
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    set(all "${out}\n${err}")
    file(WRITE "${OUT}/${index}-${arm}.log" "${all}")
    string(REGEX MATCHALL "rd: VU audit: violation: [^\n]*" lines "${all}")
    foreach(l IN LISTS lines)
      message(STATUS "rd_vu_audit: ${name} ${arm}: ${l}")
    endforeach()
    if(NOT rc EQUAL 0)
      message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: the tool exited ${rc}")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT all MATCHES "${runRe}")
      message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: no audit run line (${OUT}/${index}-${arm}.log)")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    # the path ran (the fixture's declared quantities)
    if(arm STREQUAL "off")
      rd_fixture_require(${name} "${all}" vu_audit_entries vu_audit_violations)
    else()
      rd_fixture_require(${name} "${all}" vu_audit_entries vu_audit_violations vu_audit_lists
                         vu_audit_plane_draws)
    endif()
    string(REGEX MATCH "rd: VU audit: [0-9]+ entries[^\n]*" runLine "${all}")
    message(STATUS "rd_vu_audit: ${name} ${arm}: ${runLine}")
    string(REGEX MATCH "${runRe}" _m "${all}")
    set(entries "${CMAKE_MATCH_1}")
    set(lists "${CMAKE_MATCH_3}")
    set(readbacks "${CMAKE_MATCH_4}")
    set(planeDraws "${CMAKE_MATCH_7}")
    set(bad "${CMAKE_MATCH_8}")
    if(NOT bad EQUAL 0 OR lines)
      message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: ${bad} violations")
      math(EXPR failures "${failures} + 1")
    endif()
    if(entries EQUAL 0)
      message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: no recorded entry was audited")
      math(EXPR failures "${failures} + 1")
    endif()
    # the Arm estimate (rd_vu_audit.h): the worst pass's summed index
    # ranges and the largest span over indices of a draw, held to the off
    # arm's (v0.11.5's path, which ran on the phone) with 1 % for a pass
    # split differently; the span sums of a merged run and of its draws one
    # by one are the same by construction (contiguous ranges)
    set(worstRe "rd: VU audit: worst pass: [^\n]*index ranges summed ([0-9]+) vertices[^\n]* ([0-9.]+) MB at 164 bytes a vertex[^\n]*the largest span over indices of a draw ([0-9.]+) ")
    if(NOT all MATCHES "${worstRe}")
      message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: no worst-pass line")
      math(EXPR failures "${failures} + 1")
    else()
      set(span "${CMAKE_MATCH_1}")
      set(mbs "${CMAKE_MATCH_2}")
      set(ratioText "${CMAKE_MATCH_3}")
      string(REPLACE "." "" ratio "${ratioText}")
      string(REGEX REPLACE "^0+([0-9])" "\\1" ratio "${ratio}")
      string(REGEX MATCH "rd: VU audit: worst pass: [^\n]*" worstLine "${all}")
      message(STATUS "rd_vu_audit: ${name} ${arm}: ${worstLine}")
      if(arm STREQUAL "off")
        set(offSpan "${span}")
        set(offRatio "${ratio}")
        set(offMb "${mbs}")
      elseif(NOT offSpan STREQUAL "")
        math(EXPR spanCap "${offSpan} + ${offSpan} / 100")
        math(EXPR ratioCap "${offRatio} + ${offRatio} / 100")
        if(span GREATER spanCap)
          message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: the worst pass sums ${span} vertices (${mbs} MB), past the off arm's ${offSpan} (${offMb} MB) by more than 1 %")
          math(EXPR failures "${failures} + 1")
        endif()
        if(ratio GREATER ratioCap)
          message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: a draw's span over its indices ${ratioText}, past the off arm's by more than 1 %")
          math(EXPR failures "${failures} + 1")
        endif()
      endif()
    endif()
    if(NOT arm STREQUAL "off" AND (lists EQUAL 0 OR readbacks EQUAL 0 OR planeDraws EQUAL 0))
      message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: the compaction's lists not audited (${lists} lists, ${readbacks} readbacks, ${planeDraws} plane draws)")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vu_audit: ${failures} failures")
endif()
message(STATUS "rd_vu_audit: every run 0 violations")
