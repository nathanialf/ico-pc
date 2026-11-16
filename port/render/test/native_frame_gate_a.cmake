# native_frame_gate_a: the native-frame gate (tools/native_frame_check.py
# gate-a, docs/port/TESTING.md) on the committed fixtures of the list
# (port/render/test/fixtures/lists/native_frame.txt): each expanded into OUT,
# rendered at the gate's two settings (the Original preset 4:3 1x and
# Enhanced 4:3 2x, every named target and the present) and compared with the
# SHA-256 of docs/native_frame_baseline.json, exactly.  The manifest holds the
# fixtures' own pictures as the release before (v0.11.6, main 1c09ef6b1)
# drew them (`native_frame_check.py baseline --tool <v0.11.6's rd_replay_tool>
# --dumps <the expanded fixtures>`), and the expansions' own SHA-256 and size,
# which the gate checks first.  The path ran: one --stats replay a fixture at
# Enhanced 4:3 2x under the PS2's region rule (the gate's) shows what the
# fixture declares among prepass_vertices and shadow_volume_draws.
# No device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DPYTHON=<python3> -DNFC=<tools/native_frame_check.py>
# -DMANIFEST=<docs/native_frame_baseline.json> -DFIXTURE_TOOL=<rd_fixture_tool>
# -DFIXTURES=<the fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake>
# -DLIST=<the fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
rd_fixture_list("${LIST}" names)
set(failures 0)
set(RDFX_FAILURES 0)
set(plist "# the expanded fixtures (native_frame_gate_a)\n")
foreach(n IN LISTS names)
  rd_fixture_expand(${n} "${OUT}/fixtures" dump)
  string(APPEND plist "${dump}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_REGION_RULE=ps2
                          "${TOOL}" "${dump}" "${OUT}/${n}-stats.png" --enhanced --aspect 4:3
                          --resolution 2x --stats
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${n}: the --stats replay exited ${rc}:\n${out}\n${err}")
  endif()
  rd_fixture_require(${n} "${out}${err}" prepass_vertices shadow_volume_draws)
endforeach()
file(WRITE "${OUT}/dumps.txt" "${plist}")
execute_process(COMMAND "${PYTHON}" -I "${NFC}" gate-a --tool "${TOOL}" --dumps "${OUT}/dumps.txt"
                        --manifest "${MANIFEST}"
                RESULT_VARIABLE rcG OUTPUT_VARIABLE outG ERROR_VARIABLE errG)
message(STATUS "native_frame_gate_a:\n${outG}${errG}")
if(rcG EQUAL 77)
  message(STATUS "SKIP: native_frame_check.py skipped (no device or no Pillow)")
  return()
endif()
if(NOT rcG EQUAL 0)
  math(EXPR failures "${failures} + 1")
endif()
list(LENGTH names nf)
if(NOT outG MATCHES "gate-a: ([0-9]+) targets checked, 0 mismatches")
  message(SEND_ERROR "native_frame_gate_a: no clean 'gate-a: N targets checked, 0 mismatches' line")
  math(EXPR failures "${failures} + 1")
else()
  set(nt "${CMAKE_MATCH_1}")
  math(EXPR want "${nf} * 2 * 18")
  if(NOT nt EQUAL want)
    message(SEND_ERROR "native_frame_gate_a: ${nt} targets checked, want ${want} (${nf} fixtures, 2 "
                       "settings, 18 targets: a fixture left out of the manifest)")
    math(EXPR failures "${failures} + 1")
  endif()
endif()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "native_frame_gate_a: ${failures} checks failed (above)")
endif()
message(STATUS "native_frame_gate_a: ${nf} fixtures, every target the release before's picture")
