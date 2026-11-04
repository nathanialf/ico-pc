# rd_sideband_ab: the region rule's side bands (issue 29; vu_common.hlsli
# VU_RULE_CLIP_BESIDE, docs/port/VU1_PROGRAMS.md "The region rule") against
# the PS2's drop, on the wide dumps of the machine-local list
# tools/sideband_dumps.txt (a path and the dump's aspect a line; a 4:3 dump
# is the control: there the band is the frame).  For every dump, at its own
# aspect, two settings:
#   orig     the Original preset at 1x (--aspect A);
#   enh2x    Enhanced at 2x (--enhanced --resolution 2x --aspect A);
# rd_replay_tool's --pass-dump twice: A with ICO_RD_VU_BESIDE=0 (every VU
# draw keeps the PS2's drop, ICO_VU_NO_BESIDE) and B without, then
# tools/cmp_dirs_band.py A B --gs-width W --band-files SCENE.png
# --fog-from-a, W from the replay's "frame W GS wide" line: SCENE's centre
# 512 GS pixels (512 * scale texels) identical byte for byte, its side
# columns reported, and every side pixel B changes one A left at its fog
# colour (A's most common side colour); every other target reported, not
# held (the post effects read across the band's edge).  A missing list or
# dump is named and skipped; none here, or no device (77): SKIP.
# Both replays run under the PS2's region rule (ICO_RD_REGION_RULE=ps2: the
# side bands are its; the camera rule, the default, has none and is
# rd_region_camera_ab's).
# -DTOOL=<rd_replay_tool> -DPYTHON=<python3> -DCMP=<tools/cmp_dirs_band.py>
# -DLIST=<dump list> -DOUT=<a folder the test may write>
if(NOT EXISTS "${LIST}")
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(dumps "")
set(aspects "")
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  if(NOT l MATCHES "^([^ \t]+)[ \t]+([0-9.:]+)")
    message(STATUS "${l}: not 'path aspect', skipped")
    continue()
  endif()
  set(p "${CMAKE_MATCH_1}")
  set(a "${CMAKE_MATCH_2}")
  if(NOT IS_ABSOLUTE "${p}")
    set(p "${listDir}/${p}")
  endif()
  if(NOT EXISTS "${p}")
    message(STATUS "${p}: no such dump, skipped")
    continue()
  endif()
  list(APPEND dumps "${p}")
  list(APPEND aspects "${a}")
endforeach()
list(LENGTH dumps nd)
if(nd EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")

set(settings orig enh2x)
set(args_orig "")
set(args_enh2x --enhanced --resolution 2x)
set(failures 0)
math(EXPR last "${nd} - 1")
foreach(i RANGE ${last})
  list(GET dumps ${i} dump)
  list(GET aspects ${i} aspect)
  foreach(s IN LISTS settings)
    set(dirA "${OUT}/${i}-${s}-drop")
    set(dirB "${OUT}/${i}-${s}-beside")
    file(MAKE_DIRECTORY "${dirA}" "${dirB}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_BESIDE=0 ICO_RD_REGION_RULE=ps2
                            "${TOOL}" "${dump}" - --pass-dump "${dirA}" --aspect ${aspect}
                            ${args_${s}}
                    RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
    if(rcA EQUAL 77)
      message(STATUS "SKIP: no device: ${errA}")
      return()
    endif()
    if(NOT rcA EQUAL 0)
      message(FATAL_ERROR "${dump} (${s}): the drop's replay exit ${rcA}:\n${outA}\n${errA}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_REGION_RULE=ps2
                            "${TOOL}" "${dump}" - --pass-dump "${dirB}" --aspect ${aspect}
                            ${args_${s}}
                    RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
    if(NOT rcB EQUAL 0)
      message(FATAL_ERROR "${dump} (${s}): the side bands' replay exit ${rcB}:\n${outB}\n${errB}")
    endif()
    if(NOT "${outB}${errB}" MATCHES "frame ([0-9]+) GS wide")
      message(FATAL_ERROR "${dump} (${s}): no 'frame W GS wide' line:\n${errB}")
    endif()
    set(gsw "${CMAKE_MATCH_1}")
    execute_process(COMMAND "${PYTHON}" -I "${CMP}" "${dirA}" "${dirB}" --gs-width ${gsw}
                            --band-files SCENE.png --fog-from-a
                    RESULT_VARIABLE rcC OUTPUT_VARIABLE outC ERROR_VARIABLE errC)
    message(STATUS "rd_sideband_ab: ${dump} (${s}, aspect ${aspect}, frame ${gsw} GS wide):\n"
                   "${outC}${errC}")
    if(NOT rcC EQUAL 0)
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_sideband_ab: ${failures} comparisons failed (above)")
endif()
message(STATUS "rd_sideband_ab: ${nd} dumps at 2 settings: the picture's band identical, no side "
               "pixel lost")
