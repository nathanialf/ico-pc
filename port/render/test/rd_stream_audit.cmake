# rd_stream_audit: the VU stream bounds audit (test/rd_stream_audit_test.c; docs/port/RENDER.md,
# "The stream bounds audit") on the committed fixtures of LIST (rd_fixture.cmake,
# docs/port/TESTING_FIXTURES.md): each expanded into OUT, then rd_stream_audit_test on every
# expansion at once (its self-test always runs first).  Every fixture must be audited (its
# line "<dump>: frame N, D VU draws, 0 with reads past the end" with D > 0: the path ran), the
# program must exit 0 and its summary must count every fixture.  A fixture's strip flags are
# rebuilt from its kept index list (TESTING_FIXTURES.md, "The generated geometry"); the kicks,
# index ranges, batches and bone addresses are the frame's own.
# -DTEST=<rd_stream_audit_test> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<fixtures folder>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<fixture list> -DOUT=<a folder the test may
# write>
include("${FIXTURE_HELPERS}")
rd_fixture_list("${LIST}" names)
set(dumps "")
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)
  list(APPEND dumps "${dump}")
endforeach()
list(LENGTH names n)
execute_process(COMMAND "${TEST}" ${dumps} WORKING_DIRECTORY "${OUT}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
set(failures 0)
if(NOT rc EQUAL 0)
  message(SEND_ERROR "rd_stream_audit_test exit ${rc}:\n${err}")
  math(EXPR failures "${failures} + 1")
endif()
foreach(name IN LISTS names)
  if(NOT out MATCHES "rd_stream_audit: [^\n]*/${name}\\.rddump: frame [0-9]+, ([0-9]+) VU draws, ([0-9]+) with reads past the end")
    message(SEND_ERROR "${name}: not audited")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(CMAKE_MATCH_1 EQUAL 0 OR NOT CMAKE_MATCH_2 EQUAL 0)
    message(SEND_ERROR "${name}: ${CMAKE_MATCH_1} VU draws audited, ${CMAKE_MATCH_2} with reads past the end")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(NOT out MATCHES "rd_stream_audit: ${n} dumps, [0-9]+ VU draws")
  message(SEND_ERROR "the summary does not count the ${n} fixtures")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_stream_audit: ${failures} failures over ${n} fixtures")
endif()
message(STATUS "rd_stream_audit: ${n} fixtures audited, no read past a stream")
