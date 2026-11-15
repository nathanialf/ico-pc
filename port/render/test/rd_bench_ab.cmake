# rd_bench_ab: the rc bench (port/render/rd_bench_run.h rd_bench_tool) over
# a dump through rd_replay_tool --bench 1 on lavapipe: W 1 and K 1 a block,
# one round, every variant the frame has.  Each run must exit 0 and say
#   "bench: done; every variant ran: N of N"
#   "bench: the picture after the bench: the same bytes as before it", or
#   that the frame's picture differs from one baseline replay to the next (a
#   feedback effect: then not compared)
# and hold a baseline line and every variant line its list entry names.
# The dumps: DUMP (rd_pixel's, may be absent) at the tool's own setting with
# a 640x480 present, and the fixtures LIST names
# (port/render/test/fixtures/lists/bench.txt: fixture names of the
# repository, docs/port/TESTING_FIXTURES.md; a listed fixture that is absent or
# does not expand to its pinned SHA-256 FAILS; after the name the line's own
# properties, rd_fixture_list_props.cmake, then '|' and the variant lines
# its run must hold, '|'-separated, from the frame's own command listing:
# rd_replay_tool --list --no-device; the fixture keeps every command) at the
# PowerVR phone's setting (ICO_VK_FAKE_LIMITS=moltenvk, Enhanced, --resolution
# 2364x1080 --full-height --full-pixel --present 2364x1080).  The path ran
# (rd_fixture_require on a fixture's bench run): bench_variants, the variants
# the bench ran (every one of them, above).  No device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DFIXTURE_TOOL=<rd_fixture_tool>
# -DFIXTURES=<the fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake>
# -DLIST=<a fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
set(dumps "")
set(wants "")
if(DUMP AND EXISTS "${DUMP}")
  list(APPEND dumps "${DUMP}")
  list(APPEND wants "-")
endif()
rd_fixture_list("${LIST}" fxNames)
foreach(name IN LISTS fxNames)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  rd_fixture_list_props(${name})
  set(names "-")
  string(FIND "${RDFX_FIELDS_${name}}" "|" bar)
  if(bar GREATER_EQUAL 0)
    math(EXPR from "${bar} + 1")
    string(SUBSTRING "${RDFX_FIELDS_${name}}" ${from} -1 names)
    string(REPLACE "|" "@@" names "${names}")
  endif()
  list(APPEND dumps "${d}")
  list(APPEND wants "${names}")
  set(name_${d} "${name}")
endforeach()
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump is here")
  return()
endif()

set(first TRUE)
math(EXPR last "${ndumps} - 1")
foreach(i RANGE ${last})
  list(GET dumps ${i} dump)
  list(GET wants ${i} want)
  if(dump STREQUAL "${DUMP}")
    set(envs "")
    set(args --present 640x480)
  else()
    set(envs "ICO_VK_FAKE_LIMITS=moltenvk")
    set(args --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envs}
                          "${TOOL}" "${dump}" "${OUT}/d${i}.png" ${args} --bench 1
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(first AND rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  set(first FALSE)
  file(WRITE "${OUT}/d${i}.txt" "${out}\n--- stderr ---\n${err}")
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${dump}: the bench's run exited ${rc} (${OUT}/d${i}.txt):\n${out}")
  endif()
  if(NOT out MATCHES "bench: done; every variant ran: ([0-9]+) of ([0-9]+)")
    message(FATAL_ERROR "${dump}: no \"every variant ran\" line (${OUT}/d${i}.txt)")
  endif()
  if(NOT CMAKE_MATCH_1 EQUAL CMAKE_MATCH_2)
    message(FATAL_ERROR "${dump}: ${CMAKE_MATCH_1} of ${CMAKE_MATCH_2} variants ran")
  endif()
  set(nvar ${CMAKE_MATCH_2})
  if(NOT dump STREQUAL "${DUMP}")
    # the path ran: the variants the bench ran on this fixture
    rd_fixture_require(${name_${dump}} "${out}" bench_variants)
  endif()
  if(NOT out MATCHES "bench: the picture after the bench: the same bytes as before it" AND
     NOT out MATCHES "bench: the frame's picture differs from one baseline replay to the next")
    message(FATAL_ERROR "${dump}: the picture after the bench is not the one before it "
                        "(${OUT}/d${i}.txt)")
  endif()
  if(NOT out MATCHES "bench: frame 1 \\| baseline \\| [0-9]+ passes")
    message(FATAL_ERROR "${dump}: no baseline line with a timed replay (${OUT}/d${i}.txt)")
  endif()
  if(NOT want STREQUAL "-")
    string(REPLACE "@@" ";" names "${want}")
    foreach(n IN LISTS names)
      string(FIND "${out}" "bench: frame 1 | ${n} | " at)
      if(at LESS 0)
        message(FATAL_ERROR "${dump}: no line for \"${n}\" (${OUT}/d${i}.txt)")
      endif()
    endforeach()
  endif()
  message(STATUS "${dump}: ${nvar} variants ran, the picture after the bench the one before it")
endforeach()
if(RDFX_FAILURES GREATER 0)
  message(FATAL_ERROR "rd_bench_ab: ${RDFX_FAILURES} fixtures' declared properties did not hold")
endif()
