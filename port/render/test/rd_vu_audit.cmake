# rd_vu_audit: the VU record path's bounds audit (port/render/rd_vu_audit.h)
# over issue 179's Mali-G52 dumps under the Arm fakes at the reporter's
# settings (1x, 21:9, half height, presented at 1612x720), the record by
# compute dispatches and made at start-up (ICO_RD_EARLY_PREPARE=1: 512
# rows, as the phone made it), the volumes' stencil-only entry on as the
# phone ran it.  Each dump is replayed four times (both frame slots, each
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
# The run lines are printed (the numbers of the hand-back).  Where
# /primary/dev/ico-dumps exists every listed dump must be there; elsewhere:
# SKIP.  No device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DOUT=<a folder the test may write>
if(NOT EXISTS "/primary/dev/ico-dumps")
  message(STATUS "SKIP: no /primary/dev/ico-dumps here")
  return()
endif()
set(dumps
    /primary/dev/ico-dumps/v0117-mali179/loss-20261114-075659-lost.rddump
    /primary/dev/ico-dumps/v0117-mali179/loss-20261114-075659-before.rddump
    /primary/dev/ico-dumps/v0116-mali179/frame-20261112-195947-v17565.zip
    /primary/dev/ico-dumps/v0116-mali179/slow-20261112-195541.zip
    /primary/dev/ico-dumps/v0116-mali179/slow-20261112-195621.zip
    /primary/dev/ico-dumps/v0116-mali179/slow-20261112-195651.zip)
set(env_base ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute
             ICO_RD_EARLY_PREPARE=1 ICO_RD_VOLUME_FS=1 ICO_RD_VU_AUDIT=1)
set(env_on ICO_RD_VU_COMPACT=1)
set(env_nomerge ICO_RD_VU_COMPACT=1 ICO_RD_VU_MERGE=0)
set(env_off ICO_RD_VU_COMPACT=0)
set(runRe "rd: VU audit: ([0-9]+) entries \\(([0-9]+) index values\\), ([0-9]+) compacted lists from ([0-9]+) readbacks \\(([0-9]+) values, ([0-9]+) filler triangles\\), draws: ([0-9]+) of the fourth plane[^\n]*; ([0-9]+) violations")
set(failures 0)
set(index 0)
foreach(dump IN LISTS dumps)
  if(NOT EXISTS "${dump}")
    message(SEND_ERROR "rd_vu_audit: ${dump}: absent")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  get_filename_component(name "${dump}" NAME)
  foreach(arm on nomerge off)
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
    if(NOT arm STREQUAL "off" AND (lists EQUAL 0 OR readbacks EQUAL 0 OR planeDraws EQUAL 0))
      message(SEND_ERROR "rd_vu_audit: ${name} ${arm}: the compaction's lists not audited (${lists} lists, ${readbacks} readbacks, ${planeDraws} plane draws)")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vu_audit: ${failures} failures")
endif()
message(STATUS "rd_vu_audit: every run 0 violations")
