# rd_vu_skin_check_dump, rd_vu_skin_check_163*, rd_vu_skin_check_123*:
# rd_replay_tool --skin-check on a frame (rd_vu_skin_check.c;
# docs/port/VU_SKIN_CHECK.md): the summary line "<dump>: skin check: ..."
# must match LINE; SKIP without a device (77).
#
# On a committed fixture (docs/port/TESTING_FIXTURES.md):
#   -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<dir>
#   -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<lists/skin_check.txt>
#   -DROLE=<word> -DREQUIRE=<quantity|quantity...> -DOUT=<dir> -DLINE=<regex>
#   [-DLOG=<regex>]: a line the tool's log (stderr) must hold
# the one fixture of LIST whose line holds the word ROLE is expanded and
# replayed; the path ran: each quantity of REQUIRE (rd_fixture_helpers.cmake's
# table, read from the summary line) holds as the fixture or its list line
# declares it.  The skinned draws' weights and positions are generated
# (half on two bones, rd_fixture.c genMesh), the poses kept, so the counts
# in LINE are the fixture's: re-derived from its run, not the reporter's.
#
# On a dump (the rows not converted yet): -DTOOL -DDUMP=<dump> -DOUT -DLINE
# [-DLOG]; SKIP without the dump.
if(FIXTURE_TOOL)
  include("${FIXTURE_HELPERS}")
  include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
  rd_fixture_list("${LIST}" names)
  set(fx "")
  foreach(n IN LISTS names)
    rd_fixture_list_word(${n} "${ROLE}" has)
    if(has)
      if(fx)
        message(FATAL_ERROR "${LIST}: the word ${ROLE} is on ${fx} and ${n}: one fixture a word")
      endif()
      set(fx "${n}")
    endif()
  endforeach()
  if(NOT fx)
    message(FATAL_ERROR "${LIST}: no fixture's line holds the word ${ROLE}")
  endif()
  rd_fixture_list_props(${fx})
  rd_fixture_expand(${fx} "${OUT}/fixtures" DUMP)
elseif(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: no ${DUMP}")
  return()
endif()
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/skin-check.png" --skin-check
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  message(STATUS "SKIP: no usable device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_replay_tool --skin-check exited ${rc}:\n${out}\n${err}")
endif()
string(REPLACE ";" "," flat "${out}")
if(NOT flat MATCHES "skin check: ${LINE}")
  message(FATAL_ERROR "the summary does not match \"skin check: ${LINE}\":\n${out}\n${err}")
endif()
message(STATUS "rd_replay_tool --skin-check: ${CMAKE_MATCH_0}")
if(LOG)
  string(REPLACE ";" "," flatErr "${err}")
  if(NOT flatErr MATCHES "${LOG}")
    message(FATAL_ERROR "the log has no line matching \"${LOG}\":\n${err}")
  endif()
  message(STATUS "the log: ${CMAKE_MATCH_0}")
endif()
if(FIXTURE_TOOL)
  string(REPLACE "|" ";" req "${REQUIRE}")
  if(NOT req)
    message(FATAL_ERROR "-DREQUIRE names no quantity: a fixture test asserts its path ran")
  endif()
  rd_fixture_require(${fx} "${out}" ${req})
  if(RDFX_FAILURES GREATER 0)
    message(FATAL_ERROR "${fx}: ${RDFX_FAILURES} declared properties did not hold")
  endif()
endif()
