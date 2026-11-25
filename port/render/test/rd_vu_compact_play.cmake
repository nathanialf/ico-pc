# rd_vu_compact_play: the index compaction's Mali-G52 form turned off in
# play by v0.11.6's sign (port/render/rd_vu_compact.h,
# rd__vu_compact_play_health).  Issue 179's Mali-G52 frame (the fixture
# fx-mali-frame of test/fixtures/lists/vu_flat0.txt) under the Arm fakes and
# its class (ICO_RD_VU_COMPACT_ARM=1, on by ICO_RD_VU_COMPACT=1 without the
# self-tests), its settings, --repeat 6: with ICO_RD_HEALTH_FAKE=play every
# collected record with VU draws outside a self-test reads as recorded
# stamps unwritten after the work was signalled finished.  The way back
# must be taken: the line "off for the run (in play: ...): the next picture
# without it", the ledger's state refused (the stats line), the mark file
# "failed v0.0.0" (this version leaves it off at later starts).  The same
# replay without the fake: none of it, the mark untouched.  No device (77):
# SKIP.
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=... -DFIXTURES=... -DFIXTURE_HELPERS=...
# -DLIST=<vu_flat0.txt> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
rd_fixture_list("${LIST}" names)
if(NOT "fx-mali-frame" IN_LIST names)
  message(FATAL_ERROR "rd_vu_compact_play: ${LIST} has no fx-mali-frame")
endif()
rd_fixture_expand(fx-mali-frame "${OUT}/fixtures" dump)
set(mark "${OUT}/compact-mark.txt")
set(env ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute
        ICO_RD_VU_COMPACT_ARM=1 ICO_RD_VU_COMPACT=1 ICO_COMPACT_VERSION=v0.0.0
        "ICO_COMPACT_MARK=${mark}")
set(failures 0)
foreach(arm fake plain)
  file(REMOVE "${mark}")
  if(arm STREQUAL "fake")
    set(extra ICO_RD_HEALTH_FAKE=play)
  else()
    set(extra "")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env} ${extra} "${TOOL}" "${dump}" /dev/null
                          --stats --aspect 21:9 --repeat 6
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rd_vu_compact_play (${arm}): exit ${rc}:\n${out}\n${err}")
  endif()
  # a ";" turned to "," (the patterns write ", " where the log has "; ")
  string(REPLACE ";" "," flat "${out}\n${err}")
  set(off "rd: VU index compaction: off for the run \\(in play: replay [0-9]+, frame [0-9]+, 1 of its recorded stamps unwritten after its work was signalled finished, the sign of v0.11.6's black pictures on the Mali-G52, with it on\\): the next picture without it, every draw on its own index list, its mark says failed")
  set(state "stats: VU index compaction refused by its self-test: [0-9]+ of [1-9][0-9]* list 0 triangles left out")
  set(text "")
  if(EXISTS "${mark}")
    file(READ "${mark}" text)
  endif()
  if(arm STREQUAL "fake")
    foreach(re "rd: picture health: ICO_RD_HEALTH_FAKE=play: every picture with VU draws" "${off}"
               "${state}")
      if(NOT flat MATCHES "${re}")
        message(SEND_ERROR "rd_vu_compact_play (fake): no line matching \"${re}\"")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    if(NOT text STREQUAL "failed v0.0.0\n")
      message(SEND_ERROR "rd_vu_compact_play (fake): the mark says \"${text}\", not failed v0.0.0")
      math(EXPR failures "${failures} + 1")
    endif()
  else()
    if(flat MATCHES "off for the run \\(in play" OR NOT flat MATCHES "stats: VU index compaction on: [0-9]+ of [1-9][0-9]* list 0")
      message(SEND_ERROR "rd_vu_compact_play (plain): turned off without the sign, or not on")
      math(EXPR failures "${failures} + 1")
    endif()
    if(text MATCHES "failed")
      message(SEND_ERROR "rd_vu_compact_play (plain): the mark says \"${text}\"")
      math(EXPR failures "${failures} + 1")
    endif()
  endif()
  if(failures GREATER 0)
    message(STATUS "${out}\n${err}")
  endif()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vu_compact_play: ${failures} failures")
endif()
message(STATUS "rd_vu_compact_play: the sign in play turned it off, the mark failed; without it on")
