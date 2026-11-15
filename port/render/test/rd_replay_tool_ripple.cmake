# rd_replay_tool_ripple*, rd_replay_tool_mirror_floor: a committed fixture
# (port/render/test/fixtures, docs/port/TESTING_FIXTURES.md) replayed by
# rd_replay_tool at the given options, its log and its picture checked (issue
# 106: with Full pixel on, the open border clamped every render-target read at
# its edges, and the puddle's ripples, screen prims reading a block smaller
# than the frame, took the block's edge texels: bright white segments on the
# game's picture; rd_replay.c drawWrap, rd_water.c rd__block_keeps_repeat).
# The fixture keeps the frame's commands (the block, the draws into it, the
# ripples' screen prims and their REPEAT, the scene copy the mirror floor's
# grids read) with generic textures and geometry, so the checks are:
#   - the water line (--list): every block read on REPEAT kept on REPEAT, the
#     block's scale; the path ran: the fixture's declared water quantities
#     (rd_fixture_require on the replay's output, REQUIRE)
#   - the picture: its PNG's SHA-256, the fixture's own picture as the release
#     before drew it (v0.11.6's rd_replay_tool, the arguments of the
#     registration; checked only on lavapipe, the tool's device line naming
#     llvmpipe, where it was taken)
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake>
# -DFIXTURE=<a fixture's name> -DOUT=<png> -DARGS=<a|b|...>
# -DEXPECT=<regex>, -DEXPECT2=<regex> (optional): each must match the tool's
#   output at least once (ARGS should carry --list: the water line)
# -DREQUIRE=<quantity|quantity...>: the fixture's declared properties checked
# -DSHA=<hex> (optional): the SHA-256 of the PNG OUT
include("${FIXTURE_HELPERS}")
# (a folder of this test's own: the twins of one fixture may run at once)
rd_fixture_expand(${FIXTURE} "${OUT}.fixture" dump)
if(ARGS)
  string(REPLACE "|" ";" ARGS "${ARGS}")
endif()
file(REMOVE "${OUT}")
execute_process(COMMAND "${TOOL}" "${dump}" "${OUT}" ${ARGS} RESULT_VARIABLE rc
                OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77 OR out MATCHES "SKIP:")
  message(STATUS "SKIP: no usable Vulkan device or mode")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${TOOL} exited ${rc}:\n${out}\n${err}")
endif()
string(REGEX MATCH "water: [^\n]*" water "${out}")
message(STATUS "${water}")
set(all "${out}\n${err}")
foreach(_x EXPECT EXPECT2)
  if(${_x})
    if(NOT all MATCHES "${${_x}}")
      message(FATAL_ERROR "no \"${${_x}}\" in the output:\n${water}")
    endif()
  endif()
endforeach()
# every block read on REPEAT kept on REPEAT (rd__block_keeps_repeat: a block
# smaller than the frame read by a screen prim)
string(REGEX MATCHALL "REPEAT [0-9]+, [0-9]+ of them kept on REPEAT" reps "${water}")
foreach(r IN LISTS reps)
  if(r MATCHES "REPEAT ([0-9]+), ([0-9]+) of them" AND NOT CMAKE_MATCH_1 EQUAL CMAKE_MATCH_2)
    message(FATAL_ERROR "${FIXTURE}: a block read on REPEAT was clamped (${r}):\n${water}")
  endif()
endforeach()
# the path ran
string(REPLACE "|" ";" REQUIRE "${REQUIRE}")
rd_fixture_require(${FIXTURE} "${out}" ${REQUIRE})
if(RDFX_FAILURES GREATER 0)
  message(FATAL_ERROR "${FIXTURE}: ${RDFX_FAILURES} declared properties did not hold")
endif()
if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "${TOOL} wrote no picture ${OUT}")
endif()
file(SHA256 "${OUT}" sha)
message(STATUS "${OUT}: SHA-256 ${sha}")
if(SHA)
  if(NOT err MATCHES "llvmpipe")
    message(STATUS "the picture's SHA-256 is lavapipe's: not compared on this device")
  elseif(NOT sha STREQUAL SHA)
    message(FATAL_ERROR "the picture's SHA-256 ${sha}, not ${SHA} (the release before's picture "
                        "of ${FIXTURE})")
  endif()
endif()
message(STATUS "${FIXTURE}: the log, the path and the picture checked")
