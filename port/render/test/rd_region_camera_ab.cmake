# rd_region_camera_ab: the camera rule ([dev] region_rule = camera, the
# default; vu_common.hlsli vu_drop_fate, docs/port/VU1_PROGRAMS.md "The
# region rule", docs/port/DIVERGENCES.md VD-29) against the PS2's region
# rule (ICO_RD_REGION_RULE=ps2) on the committed fixtures of the list
# (port/render/test/fixtures/lists/wide.txt: a fixture name and the frame's
# aspect a line, then the line's own properties; the 4:3 frame among them),
# at each frame's own aspect.
#   1. tools/region_camera_report.py at the Original preset 1x and Enhanced
#      2x: the two rules' pass dumps, every SCENE pixel the camera rule
#      changes inside the CPU oracle's mask (rd_replay_tool --region-mask:
#      the triangles the PS2's rule drops, or draws beside the 4:3 picture
#      only, that the camera rule draws; the back faces the card test drew
#      that it drops; the sprites out of the box), every target byte for
#      byte where the mask is empty; the report in OUT/report.md.  The path
#      ran: the camera rule's replay (the report's camera.log, --stats) at
#      every setting shows the fixture's region_rule_draws ("region rule: the
#      camera's: N VU draws", the draws the camera rule's mode holds,
#      rd_replay_vu.c vuSource's cameraDrops).  The fixtures are made with
#      @scene drops (docs/port/TESTING_FIXTURES.md, "The placement rules"):
#      a floor behind the eye and a card beyond the GS window that face the
#      eye (drops the camera rule draws: the mask's red) and the same facing
#      away (kept dropped), so a fixture whose camera_drawn_tris is at least 1
#      must change SCENE at one setting at least, every changed pixel inside
#      the mask.
#   2. the same verdicts in every preset: --region-stats under the camera
#      rule at the Original preset 1x and Enhanced 1x, at the frame's aspect:
#      the frame line's triangles the camera's rule draws of the PS2's drops
#      (strip + drawn by the camera rule), those it keeps dropped (drop) and
#      those a tick rescues the same in both (the card test, the one
#      preset-dependent step of the PS2's rule, is not the camera's); and
#      each run shows the fixture's camera_drawn_tris (the triangles the
#      camera rule draws of the PS2's drops).
# A fixture no setting checked fails; no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DPYTHON=<python3> -DREPORT=<tools/region_camera_report.py>
# -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the fixtures folder>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<the fixture list>
# -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
get_filename_component(_rc_here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
include("${_rc_here}/rd_fixture_list_props.cmake")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
rd_fixture_list("${LIST}" names)
set(dumps "")
set(aspects "")
set(plist "# the expanded fixtures and their aspects (rd_region_camera_ab)\n")
foreach(n IN LISTS names)
  if(NOT RDFX_FIELDS_${n} MATCHES "^([0-9.:]+)")
    message(FATAL_ERROR "${LIST}: ${n}: no aspect after the name ('NAME ASPECT [QUANTITY OP VALUE]...')")
  endif()
  set(a "${CMAKE_MATCH_1}")
  rd_fixture_list_props(${n})
  rd_fixture_expand(${n} "${OUT}/fixtures" dump)
  list(APPEND dumps "${dump}")
  list(APPEND aspects "${a}")
  string(APPEND plist "${dump} ${a}\n")
endforeach()
file(WRITE "${OUT}/dumps.txt" "${plist}")
list(LENGTH dumps nd)
set(RDFX_FAILURES 0)

# 1. the pictures
execute_process(COMMAND "${PYTHON}" -I "${REPORT}" --tool "${TOOL}" --out "${OUT}/ab"
                        --list "${OUT}/dumps.txt" --own-aspect --report "${OUT}/report.md"
                        --setting "orig=--present 1280x960"
                        --setting "enh2x=--enhanced --resolution 2x --present 1280x960"
                RESULT_VARIABLE rcR OUTPUT_VARIABLE outR ERROR_VARIABLE errR)
message(STATUS "rd_region_camera_ab:\n${outR}${errR}")
if(rcR EQUAL 77)
  message(STATUS "SKIP: no device")
  return()
endif()
set(failures 0)
if(NOT rcR EQUAL 0)
  math(EXPR failures "${failures} + 1")
endif()
# the path ran: the camera rule's replay of each fixture at each setting (the report keeps its
# output in <out>/<folder>-<stem>-<setting>/camera.log: region_camera_report.py dump_name)
foreach(n IN LISTS names)
  set(checked_${n} 0)
  set(scene_${n} 0)
  foreach(s orig enh2x)
    if(outR MATCHES "(ok|FAIL) fixtures-${n}-${s}: SCENE ([0-9]+) pixels changed")
      math(EXPR scene_${n} "${scene_${n}} + ${CMAKE_MATCH_2}")
    endif()
    set(log "${OUT}/ab/fixtures-${n}-${s}/camera.log")
    if(NOT EXISTS "${log}")
      message(SEND_ERROR "${n} (${s}): no camera rule replay (${log})")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    file(READ "${log}" camText)
    rd_fixture_require(${n} "${camText}" region_rule_draws)
    math(EXPR checked_${n} "${checked_${n}} + 1")
  endforeach()
endforeach()

# 2. the verdicts in both presets
math(EXPR last "${nd} - 1")
foreach(i RANGE ${last})
  list(GET names ${i} n)
  list(GET dumps ${i} p)
  list(GET aspects ${i} a)
  set(counts "")
  foreach(preset orig enh)
    set(args --aspect ${a})
    if(preset STREQUAL "enh")
      list(APPEND args --enhanced)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_REGION_RULE=camera
                            "${TOOL}" "${p}" "${OUT}/${n}-${preset}-region.png" ${args}
                            --region-stats
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${n} (${preset}): --region-stats exited ${rc}:\n${err}")
    endif()
    string(REGEX MATCH "region: frame [^\n]*" frame "${out}")
    if(NOT frame MATCHES "strip ([0-9]+) drop ([0-9]+) \\([^;]*; rescued at a tick ([0-9]+);")
      message(FATAL_ERROR "${n} (${preset}): no region frame line:\n${out}")
    endif()
    set(strip "${CMAKE_MATCH_1}")
    set(drop "${CMAKE_MATCH_2}")
    set(rescued "${CMAKE_MATCH_3}")
    if(NOT frame MATCHES "drawn by the camera rule ([0-9]+) \\(")
      message(FATAL_ERROR "${n} (${preset}): no camera rule group:\n${frame}")
    endif()
    math(EXPR drawn "${strip} + ${CMAKE_MATCH_1}")
    list(APPEND counts "${drawn}/${drop}/${rescued}")
    rd_fixture_require(${n} "${out}" camera_drawn_tris)
    if(preset STREQUAL "orig")
      rd_fixture_quantity(camera_drawn_tris "${out}" camN)
    endif()
  endforeach()
  list(GET counts 0 co)
  list(GET counts 1 ce)
  if(NOT co STREQUAL ce)
    message(SEND_ERROR "${n} (aspect ${a}): the camera rule's verdicts differ by preset: "
                       "Original drawn/dropped/rescued ${co}, Enhanced ${ce}")
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "${n} (aspect ${a}): the camera rule's drawn/dropped/rescued ${co} in both "
                   "presets")
  endif()
  if(checked_${n} EQUAL 0)
    message(SEND_ERROR "${n}: no setting checked it")
    math(EXPR failures "${failures} + 1")
  endif()
  if(camN GREATER 0 AND scene_${n} EQUAL 0)
    message(SEND_ERROR "${n}: the camera rule draws ${camN} of the PS2's drops on the oracle, and "
                       "SCENE changed at no setting")
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "${n}: the camera rule draws ${camN} of the PS2's drops; ${scene_${n}} SCENE "
                   "pixels changed over the settings, inside the mask")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_region_camera_ab: ${failures} checks failed (above; ${OUT}/report.md)")
endif()
message(STATUS "rd_region_camera_ab: every changed SCENE pixel inside a triangle the PS2 dropped; "
               "${nd} fixtures with the same verdicts in both presets, the camera rule's mode on "
               "each")
