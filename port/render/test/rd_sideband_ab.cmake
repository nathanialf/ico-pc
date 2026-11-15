# rd_sideband_ab: the region rule's side bands (issue 29; vu_common.hlsli
# VU_RULE_CLIP_BESIDE, docs/port/VU1_PROGRAMS.md "The region rule") against
# the PS2's drop, on the committed fixtures of the list (port/render/test/
# fixtures/lists/wide.txt: a fixture name and the frame's aspect a line, then
# the line's own properties; a 4:3 frame is the control: there the band is
# the frame).  For every fixture, at its own aspect, two settings:
#   orig     the Original preset at 1x (--aspect A);
#   enh2x    Enhanced at 2x (--enhanced --resolution 2x --aspect A);
# rd_replay_tool's --pass-dump twice: A with ICO_RD_VU_BESIDE=0 (every VU
# draw keeps the PS2's drop, ICO_VU_NO_BESIDE) and B without (with --stats),
# then tools/cmp_dirs_band.py A B --gs-width W --band-files SCENE.png
# --fog-from-a, W from the replay's "frame W GS wide" line: SCENE's centre
# 512 GS pixels (512 * scale texels) identical byte for byte, its side
# columns reported, and every side pixel B changes one A left at its fog
# colour (A's most common side colour); every other target reported, not
# held (the post effects read across the band's edge).
# The path ran: on B at every setting the fixture's side_band_card_draws
# ("side bands: N draws with cards", counted only for a draw the side bands'
# mode holds: rd_replay_vu.c vuSource's RD_DROPS_BESIDE branch), at least 1
# on a wide frame and 0 at 4:3 (the mode is off where the picture is the
# frame: vuDropMode's gsW test).  The generated geometry of a fixture drops
# no triangle the side bands would draw (rd_replay_tool --region-stats:
# "beside 0" on every committed fixture, and on every source frame too), so
# the compare holds the picture the same with the mode on; what a drop drawn
# beside the picture does is not exercised (docs/port/TESTING_FIXTURES.md).
# A fixture no setting checked fails; no device (77): SKIP.
# Both replays run under the PS2's region rule (ICO_RD_REGION_RULE=ps2: the
# side bands are its; the camera rule, the default, has none and is
# rd_region_camera_ab's).
# -DTOOL=<rd_replay_tool> -DPYTHON=<python3> -DCMP=<tools/cmp_dirs_band.py>
# -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the fixtures folder>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<the fixture list>
# -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
get_filename_component(_sb_here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
include("${_sb_here}/rd_fixture_list_props.cmake")
file(REMOVE_RECURSE "${OUT}")
rd_fixture_list("${LIST}" names)
set(dumps "")
set(aspects "")
foreach(n IN LISTS names)
  if(NOT RDFX_FIELDS_${n} MATCHES "^([0-9.:]+)")
    message(FATAL_ERROR "${LIST}: ${n}: no aspect after the name ('NAME ASPECT [QUANTITY OP VALUE]...')")
  endif()
  list(APPEND aspects "${CMAKE_MATCH_1}")
  rd_fixture_list_props(${n})
  rd_fixture_expand(${n} "${OUT}/fixtures" dump)
  list(APPEND dumps "${dump}")
endforeach()
list(LENGTH dumps nd)

set(settings orig enh2x)
set(args_orig "")
set(args_enh2x --enhanced --resolution 2x)
set(failures 0)
set(RDFX_FAILURES 0)
math(EXPR last "${nd} - 1")
foreach(i RANGE ${last})
  list(GET names ${i} name)
  list(GET dumps ${i} dump)
  list(GET aspects ${i} aspect)
  set(checked_${name} 0)
  foreach(s IN LISTS settings)
    set(dirA "${OUT}/${name}-${s}-drop")
    set(dirB "${OUT}/${name}-${s}-beside")
    file(MAKE_DIRECTORY "${dirA}" "${dirB}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_BESIDE=0 ICO_RD_REGION_RULE=ps2
                            "${TOOL}" "${dump}" "${dirA}-out.png" --pass-dump "${dirA}"
                            --aspect ${aspect} ${args_${s}}
                    RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
    if(rcA EQUAL 77)
      message(STATUS "SKIP: no device: ${errA}")
      return()
    endif()
    if(NOT rcA EQUAL 0)
      message(FATAL_ERROR "${name} (${s}): the drop's replay exit ${rcA}:\n${outA}\n${errA}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_REGION_RULE=ps2
                            "${TOOL}" "${dump}" "${dirB}-out.png" --pass-dump "${dirB}"
                            --aspect ${aspect} ${args_${s}} --stats
                    RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
    if(NOT rcB EQUAL 0)
      message(FATAL_ERROR "${name} (${s}): the side bands' replay exit ${rcB}:\n${outB}\n${errB}")
    endif()
    if(NOT "${outB}${errB}" MATCHES "frame ([0-9]+) GS wide")
      message(FATAL_ERROR "${name} (${s}): no 'frame W GS wide' line:\n${errB}")
    endif()
    set(gsw "${CMAKE_MATCH_1}")
    # the path ran: the side bands' mode held the draws (wide) or not (4:3)
    rd_fixture_require(${name} "${outB}${errB}" side_band_card_draws)
    math(EXPR checked_${name} "${checked_${name}} + 1")
    execute_process(COMMAND "${PYTHON}" -I "${CMP}" "${dirA}" "${dirB}" --gs-width ${gsw}
                            --band-files SCENE.png --fog-from-a
                    RESULT_VARIABLE rcC OUTPUT_VARIABLE outC ERROR_VARIABLE errC)
    message(STATUS "rd_sideband_ab: ${name} (${s}, aspect ${aspect}, frame ${gsw} GS wide):\n"
                   "${outC}${errC}")
    if(NOT rcC EQUAL 0)
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
  if(checked_${name} EQUAL 0)
    message(SEND_ERROR "${name}: no setting checked it")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_sideband_ab: ${failures} checks failed (above)")
endif()
message(STATUS "rd_sideband_ab: ${nd} fixtures at 2 settings: the picture's band identical, no side "
               "pixel lost, the side bands' mode as each fixture declares")
