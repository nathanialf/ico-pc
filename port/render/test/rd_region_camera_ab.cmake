# rd_region_camera_ab: the camera rule ([dev] region_rule = camera, the
# default; vu_common.hlsli vu_drop_fate, docs/port/VU1_PROGRAMS.md "The
# region rule", docs/port/DIVERGENCES.md VD-29) against the PS2's region
# rule (ICO_RD_REGION_RULE=ps2) on the wide dumps of the machine-local list
# tools/sideband_dumps.txt (a path and its aspect a line; the 4:3 phone
# frame and issue 189's mist frame among them), at each dump's own aspect.
#   1. tools/region_camera_report.py at the Original preset 1x and Enhanced
#      2x: the two rules' pass dumps, every SCENE pixel the camera rule
#      changes inside the CPU oracle's mask (rd_replay_tool --region-mask:
#      the triangles the PS2's rule drops, or draws beside the 4:3 picture
#      only, that the camera rule draws; the back faces the card test drew
#      that it drops; the sprites out of the box), every target byte for
#      byte where the mask is empty; the report in OUT/report.md.
#   2. the same verdicts in every preset: --region-stats under the camera
#      rule at the Original preset 1x and Enhanced 1x, at the dump's aspect:
#      the frame line's triangles the camera's rule draws of the PS2's drops
#      (strip + drawn by the camera rule), those it keeps dropped (drop) and
#      those a tick rescues the same in both (the card test, the one
#      preset-dependent step of the PS2's rule, is not the camera's).
# A missing list or dump, or no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DPYTHON=<python3> -DREPORT=<tools/region_camera_report.py>
# -DLIST=<dump list> -DOUT=<a folder the test may write>
if(NOT EXISTS "${LIST}")
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

# 1. the pictures
execute_process(COMMAND "${PYTHON}" -I "${REPORT}" --tool "${TOOL}" --out "${OUT}/ab"
                        --list "${LIST}" --own-aspect --report "${OUT}/report.md"
                        --setting "orig=--present 1280x960"
                        --setting "enh2x=--enhanced --resolution 2x --present 1280x960"
                RESULT_VARIABLE rcR OUTPUT_VARIABLE outR ERROR_VARIABLE errR)
message(STATUS "rd_region_camera_ab:\n${outR}${errR}")
if(rcR EQUAL 77)
  message(STATUS "SKIP: no dump of ${LIST} or no device")
  return()
endif()
set(failures 0)
if(NOT rcR EQUAL 0)
  math(EXPR failures "${failures} + 1")
endif()

# 2. the verdicts in both presets
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(nd 0)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#" OR NOT l MATCHES "^([^ \t]+)[ \t]+([0-9.:]+)")
    continue()
  endif()
  set(p "${CMAKE_MATCH_1}")
  set(a "${CMAKE_MATCH_2}")
  if(NOT IS_ABSOLUTE "${p}")
    set(p "${listDir}/${p}")
  endif()
  if(NOT EXISTS "${p}")
    continue()
  endif()
  math(EXPR nd "${nd} + 1")
  set(counts "")
  foreach(preset orig enh)
    set(args --aspect ${a})
    if(preset STREQUAL "enh")
      list(APPEND args --enhanced)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_REGION_RULE=camera
                            "${TOOL}" "${p}" - ${args} --region-stats
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${p} (${preset}): --region-stats exited ${rc}:\n${err}")
    endif()
    string(REGEX MATCH "region: frame [^\n]*" frame "${out}")
    if(NOT frame MATCHES "strip ([0-9]+) drop ([0-9]+) \\([^;]*; rescued at a tick ([0-9]+);")
      message(FATAL_ERROR "${p} (${preset}): no region frame line:\n${out}")
    endif()
    set(strip "${CMAKE_MATCH_1}")
    set(drop "${CMAKE_MATCH_2}")
    set(rescued "${CMAKE_MATCH_3}")
    if(NOT frame MATCHES "drawn by the camera rule ([0-9]+) \\(")
      message(FATAL_ERROR "${p} (${preset}): no camera rule group:\n${frame}")
    endif()
    math(EXPR drawn "${strip} + ${CMAKE_MATCH_1}")
    list(APPEND counts "${drawn}/${drop}/${rescued}")
  endforeach()
  list(GET counts 0 co)
  list(GET counts 1 ce)
  if(NOT co STREQUAL ce)
    message(SEND_ERROR "${p} (aspect ${a}): the camera rule's verdicts differ by preset: "
                       "Original drawn/dropped/rescued ${co}, Enhanced ${ce}")
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "${p} (aspect ${a}): the camera rule's drawn/dropped/rescued ${co} in both "
                   "presets")
  endif()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_region_camera_ab: ${failures} checks failed (above; ${OUT}/report.md)")
endif()
message(STATUS "rd_region_camera_ab: every changed SCENE pixel inside a triangle the PS2 dropped; "
               "${nd} dumps with the same verdicts in both presets")
