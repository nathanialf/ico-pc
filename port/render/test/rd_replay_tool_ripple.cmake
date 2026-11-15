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
# -DDIFFER=<hex> (optional): a SHA-256 the PNG must not have (the picture of
#   the same fixture at another setting: the block at its GS size against
#   the scene's scale, which a fixture made with @water place tells apart)
# -DCLAMP_DIFFERS=1 (optional): replay again under ICO_RD_BLOCK_CLAMP=1 (the
#   tests' sabotage of issue 106's fix, rd_water.c): its water line must say
#   0 block reads kept and its picture must differ from OUT's, so the test's
#   picture would show the block's reads clamped where the game keeps REPEAT
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
if(DIFFER AND sha STREQUAL DIFFER)
  message(FATAL_ERROR "the picture's SHA-256 ${sha} is the one this test must differ from: the "
                      "block's scale did not reach the picture")
endif()
if(CLAMP_DIFFERS)
  set(clampOut "${OUT}.clamp.png")
  file(REMOVE "${clampOut}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLOCK_CLAMP=1 "${TOOL}" "${dump}"
                          "${clampOut}" ${ARGS}
                  RESULT_VARIABLE crc OUTPUT_VARIABLE cout ERROR_VARIABLE cerr)
  if(NOT crc EQUAL 0 OR NOT EXISTS "${clampOut}")
    message(FATAL_ERROR "the run under ICO_RD_BLOCK_CLAMP=1 exited ${crc}:\n${cout}\n${cerr}")
  endif()
  if(NOT "${cout}${cerr}" MATCHES "ICO_RD_BLOCK_CLAMP=1 \\(the tests' sabotage\\)" OR
     NOT cout MATCHES " 0 block reads kept on REPEAT")
    message(FATAL_ERROR "ICO_RD_BLOCK_CLAMP=1 did not take (no switch line, or reads kept)")
  endif()
  file(SHA256 "${clampOut}" csha)
  if(csha STREQUAL sha)
    message(FATAL_ERROR "${FIXTURE}: the block's reads clamped draw the same picture "
                        "(${csha}): the test would not see issue 106's defect")
  endif()
  message(STATUS "${FIXTURE}: under ICO_RD_BLOCK_CLAMP=1 the picture differs (${csha})")
endif()
message(STATUS "${FIXTURE}: the log, the path and the picture checked")
