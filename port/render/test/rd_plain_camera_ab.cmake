# rd_plain_camera_ab: the camera rule for static meshes and grids on a device that keeps the VU
# plain set (port/render/rd_plain_camera.h, rd_replay_vu.c vuDropMode; issue 123's right wall,
# issue 163's skinned draws; docs/port/DIVERGENCES.md VD-21 and VD-29), on the committed fixtures
# of the list (port/render/test/fixtures/lists/plain_camera.txt: a fixture name and the frame's
# aspect a line, then the line's own properties), at each frame's own aspect, with lavapipe put
# on the plain set as the device table's row and Turnip keep it (ICO_RD_VU_PLAIN=row) and the VU
# pre-pass off, as on the issue 123 tablet (ICO_VK_FAKE_NO_PREPASS=1: every VU draw through the
# plain program entries).
#   1. tools/region_camera_report.py at the Original preset 1x and Enhanced 2x: the camera rule
#      against region_rule = ps2 on that device, every SCENE pixel the camera rule changes inside
#      the CPU oracle's mask and every target byte for byte where the mask is empty (the draws
#      that sample a render target left out, --feedback-off, as rd_region_camera_ab).  The
#      oracle (port/render/tools/rd_replay_region.c) puts no skinned triangle in the mask on the
#      device's plain set (rd__plain_keeps_drops), so a skinned draw that changed a pixel fails
#      here: the skinned draws are today's.
#   2. the path ran, from the camera rule's replay of each fixture at each setting
#      (<out>/ab/fixtures-<name>-<setting>/camera.log): the start line "rd: region rule: the
#      camera decides for static meshes on this device's plain set; skinned draws keep the PS2's
#      drops (issue 163)...", the stand-in's line, the fixture's region_rule_draws, and --stats'
#      "stats: plain set: N static and grid draws by the camera rule, K skinned draws kept the
#      PS2's drops" with N >= 1 (the @scene drops are static meshes), N at most the camera rule's
#      draws (RdPerfRecord.vuCameraDraws counts the particles too) and K >= 1 (every fixture
#      draws skinned commands in view); --region-stats under the camera rule shows the fixture's
#      camera_drawn_tris, and a fixture whose oracle draws one must change SCENE at a setting.
#   3. region_rule = ps2 on that device is today's: each fixture at both settings replayed
#      under ICO_RD_VU_PLAIN=row ICO_RD_REGION_RULE=ps2 and under ICO_RD_VU_PLAIN=1
#      ICO_RD_VU_BESIDE=0 ICO_RD_REGION_RULE=ps2 (the plain set with every drop kept and no side
#      band: the flags v0.11.7 gave the row's draws), every target byte for byte; the first's
#      start line names the PS2's rule with no side bands and its --stats count no draw by the
#      camera rule and K >= 1 skinned draws kept.
# A fixture no setting checked fails; no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DPYTHON=<python3> -DREPORT=<tools/region_camera_report.py>
# -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the fixtures folder>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<the fixture list>
# -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
get_filename_component(_pc_here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
include("${_pc_here}/rd_fixture_list_props.cmake")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
set(row ICO_RD_VU_PLAIN=row ICO_VK_FAKE_NO_PREPASS=1)
set(startRe "rd: region rule: the camera decides for static meshes on this device's plain set; skinned draws keep the PS2's drops \\(issue 163\\); \\[dev\\] region_rule = ps2 keeps the PS2's drops on every draw")
set(standInRe "rd: VU shaders: ICO_RD_VU_PLAIN=row: the plain set's drops as on a device that keeps it")
set(ps2Re "rd: region rule: the PS2's \\(\\[dev\\] region_rule = ps2: a triangle the VU drops stays dropped, with no side bands on this device's plain set \\(issue 163\\)\\)")
set(plainRe "stats: plain set: ([0-9]+) static and grid draws by the camera rule, ([0-9]+) skinned draws kept the PS2's drops")
rd_fixture_list("${LIST}" names)
set(dumps "")
set(aspects "")
set(plist "# the expanded fixtures and their aspects (rd_plain_camera_ab)\n")
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
set(failures 0)

# 1. the pictures: the camera rule against the PS2's on the device's plain set
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${row} "${PYTHON}" -I "${REPORT}" --tool "${TOOL}"
                        --out "${OUT}/ab" --list "${OUT}/dumps.txt" --own-aspect
                        --report "${OUT}/report.md" --feedback-off
                        --setting "orig=--present 1280x960"
                        --setting "enh2x=--enhanced --resolution 2x --present 1280x960"
                RESULT_VARIABLE rcR OUTPUT_VARIABLE outR ERROR_VARIABLE errR)
message(STATUS "rd_plain_camera_ab:\n${outR}${errR}")
if(rcR EQUAL 77)
  message(STATUS "SKIP: no device")
  return()
endif()
if(NOT rcR EQUAL 0)
  math(EXPR failures "${failures} + 1")
endif()

# 2. the path ran
set(settingArgs_orig --present 1280x960)
set(settingArgs_enh2x --enhanced --resolution 2x --present 1280x960)
math(EXPR last "${nd} - 1")
foreach(i RANGE ${last})
  list(GET names ${i} n)
  list(GET dumps ${i} p)
  list(GET aspects ${i} a)
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
    rd_fixture_quantity(region_rule_draws "${camText}" camDraws)
    if(NOT camText MATCHES "${startRe}" OR NOT camText MATCHES "${standInRe}")
      message(SEND_ERROR "${n} (${s}): the camera rule's replay is not on the device's plain set "
                         "(its start lines)")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT camText MATCHES "${plainRe}")
      message(SEND_ERROR "${n} (${s}): no plain set stats line")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(nCam "${CMAKE_MATCH_1}")
    set(nKept "${CMAKE_MATCH_2}")
    if(nCam LESS 1 OR nKept LESS 1 OR "${camDraws}" STREQUAL "" OR nCam GREATER camDraws)
      message(SEND_ERROR "${n} (${s}): ${nCam} static and grid draws by the camera rule (>= 1, "
                         "at most the camera rule's ${camDraws} draws), ${nKept} skinned draws "
                         "kept (>= 1)")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "${n} (${s}): ${nCam} static and grid draws by the camera rule of its "
                     "${camDraws}, ${nKept} skinned draws kept the PS2's drops")
    endif()
    math(EXPR checked_${n} "${checked_${n}} + 1")
  endforeach()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${row} ICO_RD_REGION_RULE=camera
                          "${TOOL}" "${p}" "${OUT}/${n}-region.png" --aspect ${a} --region-stats
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${n}: --region-stats exited ${rc}:\n${err}")
  endif()
  rd_fixture_require(${n} "${out}" camera_drawn_tris)
  rd_fixture_quantity(camera_drawn_tris "${out}" camN)
  if("${camN}" STREQUAL "")
    set(camN 0)
  endif()
  if(camN GREATER 0 AND scene_${n} EQUAL 0)
    message(SEND_ERROR "${n}: the camera rule draws ${camN} of the PS2's drops on the oracle, and "
                       "SCENE changed at no setting")
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "${n}: the camera rule draws ${camN} of the PS2's drops on the device's plain "
                   "set; ${scene_${n}} SCENE pixels changed over the settings, inside the mask")
  endif()

  # 3. region_rule = ps2 on the device's plain set: today's bytes
  foreach(s orig enh2x)
    set(dRow "${OUT}/ps2/${n}-${s}-row")
    set(dOld "${OUT}/ps2/${n}-${s}-old")
    file(REMOVE_RECURSE "${dRow}" "${dOld}")
    file(MAKE_DIRECTORY "${dRow}" "${dOld}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${row} ICO_RD_REGION_RULE=ps2 "${TOOL}"
                            "${p}" /dev/null --aspect ${a} ${settingArgs_${s}} --pass-dump "${dRow}"
                            --stats
                    RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_PLAIN=1 ICO_RD_VU_BESIDE=0
                            ICO_VK_FAKE_NO_PREPASS=1 ICO_RD_REGION_RULE=ps2 "${TOOL}" "${p}"
                            /dev/null --aspect ${a} ${settingArgs_${s}} --pass-dump "${dOld}"
                    RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
    if(rcA EQUAL 77 OR rcB EQUAL 77)
      message(STATUS "SKIP: no device")
      return()
    endif()
    if(NOT rcA EQUAL 0 OR NOT rcB EQUAL 0)
      message(SEND_ERROR "${n} (${s}, region_rule = ps2): exit ${rcA} (the row) and ${rcB} "
                         "(today's flags):\n${errA}\n${errB}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(textA "${outA}\n${errA}")
    if(NOT textA MATCHES "${ps2Re}" OR NOT textA MATCHES "${standInRe}")
      message(SEND_ERROR "${n} (${s}): the row's region_rule = ps2 replay's start lines")
      math(EXPR failures "${failures} + 1")
    endif()
    set(psCam "?")
    set(psKept "0")
    if(textA MATCHES "${plainRe}")
      set(psCam "${CMAKE_MATCH_1}")
      set(psKept "${CMAKE_MATCH_2}")
    endif()
    if(NOT psCam STREQUAL "0" OR psKept LESS 1)
      message(SEND_ERROR "${n} (${s}): region_rule = ps2 on the row: ${psCam} draws by the camera "
                         "rule (0 wanted), ${psKept} skinned draws kept (>= 1 wanted)")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngs RELATIVE "${dRow}" "${dRow}/*.png")
    file(GLOB pngsOld RELATIVE "${dOld}" "${dOld}/*.png")
    list(LENGTH pngs np)
    list(LENGTH pngsOld npOld)
    if(np EQUAL 0 OR NOT np EQUAL npOld)
      message(SEND_ERROR "${n} (${s}): ${np} targets under the row, ${npOld} under today's flags")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(diff "")
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dOld}/${png}")
        list(APPEND diff "${png} (absent under today's flags)")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dRow}/${png}" "${dOld}/${png}"
                      RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        list(APPEND diff "${png}")
      endif()
    endforeach()
    if(diff)
      message(SEND_ERROR "${n} (${s}): region_rule = ps2 on the row differs from today's flags: "
                         "${diff}")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "${n} (${s}): region_rule = ps2 on the row: ${np} targets byte for byte "
                     "today's")
      math(EXPR checked_${n} "${checked_${n}} + 1")
    endif()
  endforeach()
  if(checked_${n} EQUAL 0)
    message(SEND_ERROR "${n}: no setting checked it")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_plain_camera_ab: ${failures} checks failed (above; ${OUT}/report.md)")
endif()
message(STATUS "rd_plain_camera_ab: on the device's plain set every changed SCENE pixel inside a "
               "static triangle the PS2 dropped, the skinned draws unchanged, region_rule = ps2 "
               "today's bytes; ${nd} fixtures")
