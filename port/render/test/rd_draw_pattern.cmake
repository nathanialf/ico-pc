# rd_draw_pattern: the draw pattern gate (docs/port/TESTING.md, "The draw pattern
# gate"; issue 179).  For one faked device profile it replays every fixture of
# LIST (test/fixtures/lists/draw_pattern.txt; docs/port/TESTING_FIXTURES.md: a
# listed fixture that is absent or does not expand to its pin FAILS) once with
# rd_replay_tool --stats --game-selftests (the game window's start-up self-tests,
# so a rule they turn on is in force as on the device) and compares the counts
# that say HOW the frame is drawn with TABLE (test/draw_pattern/<profile>.txt), a
# line a fixture, exact equality:
#   draws passes             "stats: N draws, M passes" (RdPerfRecord draws,
#                            renderPasses: every GPU draw and render pass)
#   vu_draws vu_gpu          "stats: VU merge: N draws in G GPU draws (IN merged
#   vu_merged vu_merged_into into GPU, A alone": the VU draws, the GPU draws they
#   vu_alone                 took, those merged and alone
#   fused kept kept_blend    "stats: VU fuse: ...": code 36's cut and kick pairs
#   kept_plans kept_passes   fused, kept apart and each reason, under one
#   kept_dst kept_planes     pipeline already
#   kept_build kept_off
#   fused_same
#   twice twice_tris         "stats: two-pass blend: ...": the draws drawn twice
#   colour_alone             by the two-pass blend, their VU triangles, those
#                            drawn once through its colour pass
#   pipelines                "stats: pipelines: P made this run"
# A change of any count fails with each count's old and new value and the
# sentence the docs give; a table that holds the word `ungenerated` fails with
# the command that writes it.  With -DWRITE=1 the script writes TABLE from this
# run instead (the regeneration, never run by ctest: the target
# rd_draw_pattern_tables).  The path ran: every run's counts read, the profile's
# fakes named in its log, the game self-tests' line, each fixture's declared
# prepass_vertices where the profile runs the VU pre-pass, and under the Mali
# profile draws drawn twice by the two-pass blend over the list.  No device: SKIP.
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<folder>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<list> -DPROFILE=<name>
# -DTABLE=<table file> -DTABLE_SHOWN=<its path in the repository> -DOUT=<folder>
# [-DWRITE=1]
include("${FIXTURE_HELPERS}")

# The profiles: the environment fakes and the tool's settings of each (the
# reporters' devices as the tests fake them; docs/port/TESTING.md, "Environment
# switches"), and texts of its log that say the fakes took.  Every profile but the
# Mali's (whose no destination read covers it) holds the fragment shader interlock off
# (ICO_VK_FAKE_NO_INTERLOCK=1): lavapipe has it from Mesa 25.2 (CI's runners) and not
# before (Mesa 25.0.7 here), and the blend's paths through it are another draw
# pattern; no reporter's phone has it (their logs' "destination read:
# interlock no": the Mali-G52, both Adreno 740s, the Pixel's PowerVR).
#   mali     issue 179's Mali-G52 (moto g24 power): Arm's phone GPU (the tile-based
#            phones' rule and device table rows), a Mali-G68's limits, a Vulkan 1.1
#            driver, no dual-source blending (the two-pass blend), no vertex
#            stores (the VU record by compute dispatches), no destination read, no
#            dynamic state; 1x, the phone's 1612x720 screen
#   powervr  the PowerVR phone as the tests fake it (rd_lamps_ab's phone,
#            rd_aura_paste_ab): the tile class's MoltenVK fakes, PowerVR's vendor
#            for the tile-based phones' rule, the VU pre-pass off; Resolution Auto
#            (Enhanced 2364x1080, Full height, Full pixel)
#   adreno   issue 123's and 215's Adreno on Qualcomm's own driver: Qualcomm's
#            vendor (the tile-based phones' rule), its pass row (VkRenderPass
#            objects), a Vulkan 1.1 driver without dynamic state (TESTING.md's
#            Adreno combination), the plain VU set as the device table's row
#            keeps it, the VU pre-pass off; Enhanced 2x at 1920x1080
#   desktop  lavapipe's own profile (a desktop GPU), the local reads kept by the
#            desktop's probe (ICO_RD_LOCAL_PROBE_SABOTAGE=fast: its timing would
#            otherwise decide); Enhanced 2x at 1280x960
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_VK_FAKE_API=1.1
    ICO_VK_FAKE_NO_DUAL=1 ICO_VK_FAKE_NO_VERTEX_STORES=1 ICO_RD_VU_RECORD=compute
    ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DYNAMIC=1)
set(args_mali --resolution 1x --present 1612x720)
set(said_mali "ICO_VK_FAKE_LIMITS=mali: the device's limits are clamped"
    "(ICO_VK_FAKE_VENDOR)" "ICO_VK_FAKE_API=1.1: the device is used as a Vulkan 1.1"
    "no dualSrcBlend (ICO_VK_FAKE_NO_DUAL)" "(ICO_VK_FAKE_NO_VERTEX_STORES)"
    "ICO_VK_FAKE_NO_DST" "ICO_VK_FAKE_NO_DYNAMIC=1")
set(prepass_mali TRUE)
set(env_powervr ICO_VK_FAKE_LIMITS=moltenvk ICO_VK_FAKE_VENDOR=0x1010 ICO_RD_VU_PREPASS=0
    ICO_VK_FAKE_NO_INTERLOCK=1)
set(args_powervr --enhanced --resolution 2364x1080 --full-height --full-pixel --present
    2364x1080)
set(said_powervr "ICO_VK_FAKE_LIMITS=moltenvk: the device is reported as MoltenVK"
    "(ICO_VK_FAKE_VENDOR)" "(ICO_VK_FAKE_NO_INTERLOCK)" "stats: VU pre-pass off")
set(prepass_powervr FALSE)
set(env_adreno ICO_VK_FAKE_VENDOR=0x5143 ICO_VK_FAKE_PASS_ROW=qualcomm ICO_VK_FAKE_API=1.1
    ICO_VK_FAKE_NO_DYNAMIC=1 ICO_RD_VU_PLAIN=row ICO_VK_FAKE_NO_PREPASS=1
    ICO_VK_FAKE_NO_INTERLOCK=1)
set(args_adreno --enhanced --resolution 2x --present 1920x1080)
# (the pass row's own line is not said at API 1.1, where the passes are VkRenderPass
# objects before the row is asked: vk_device.c; the stats line says them)
set(said_adreno "(ICO_VK_FAKE_VENDOR)" "as VkRenderPass objects"
    "ICO_VK_FAKE_API=1.1: the device is used as a Vulkan 1.1" "ICO_VK_FAKE_NO_DYNAMIC=1"
    "(ICO_VK_FAKE_NO_INTERLOCK)" "ICO_RD_VU_PLAIN=row: the plain set's drops"
    "stats: VU pre-pass off")
set(prepass_adreno FALSE)
set(env_desktop ICO_RD_LOCAL_PROBE_SABOTAGE=fast ICO_VK_FAKE_NO_INTERLOCK=1)
set(args_desktop --enhanced --resolution 2x --present 1280x960)
set(said_desktop "(ICO_VK_FAKE_NO_INTERLOCK)")
set(prepass_desktop TRUE)

if(NOT DEFINED env_${PROFILE})
  message(FATAL_ERROR "rd_draw_pattern: no profile '${PROFILE}' (mali, powervr, adreno, desktop)")
endif()
if(NOT DEFINED TABLE_SHOWN OR TABLE_SHOWN STREQUAL "")
  set(TABLE_SHOWN "${TABLE}")
endif()

# the counts, in the table's order, and the line each comes from
set(keys draws passes vu_draws vu_gpu vu_merged vu_merged_into vu_alone fused kept kept_blend
    kept_plans kept_passes kept_dst kept_planes kept_build kept_off fused_same twice twice_tris
    colour_alone pipelines)
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes, ")
set(mergeRe "stats: VU merge: ([0-9]+) draws in ([0-9]+) GPU draws \\(([0-9]+) merged into ([0-9]+), ([0-9]+) alone, merge (on|off)\\)")
set(fuseRe "stats: VU fuse: ([0-9]+) pairs fused, ([0-9]+) kept apart \\(blend form ([0-9]+), plans differ ([0-9]+), passes ([0-9]+), dst/DATE ([0-9]+), no planes ([0-9]+), no merge build ([0-9]+), ")
set(fuse2Re "stats: VU fuse: [^\n]*, fuse off ([0-9]+)\\), ([0-9]+) under one pipeline already")
set(twiceRe "stats: two-pass blend: ([0-9]+) draws drawn twice \\(([0-9]+) VU triangles\\), ([0-9]+) through the colour pass alone")
set(pipesRe "stats: pipelines: ([0-9]+) made this run")
set(selfRe "game self-tests: blend (passed|failed), VU pre-pass (on|off), fog (passed|failed)")
set(deviceRe "device table: ([^\n]*\\(device [^)\n]*\\))")

set(regen "cmake --build <build dir> --target rd_draw_pattern_tables (docs/port/TESTING.md, \"The draw pattern gate\")")
set(changed "the draw pattern of profile ${PROFILE} changed: if it is intended, update ${TABLE_SHOWN} in the same commit and name the change in the release notes' device checklist")

# the table (the check only)
set(tableUngenerated FALSE)
set(tableNames "")
set(tableDevice "")
if(NOT WRITE)
  if(NOT EXISTS "${TABLE}")
    message(FATAL_ERROR "${TABLE_SHOWN}: no table for profile ${PROFILE}: write it with ${regen}")
  endif()
  file(STRINGS "${TABLE}" tlines)
  foreach(l IN LISTS tlines)
    string(STRIP "${l}" l)
    if(l MATCHES "^# device: (.*)$")
      set(tableDevice "${CMAKE_MATCH_1}")
      continue()
    endif()
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    if(l STREQUAL "ungenerated")
      set(tableUngenerated TRUE)
      continue()
    endif()
    separate_arguments(w UNIX_COMMAND "${l}")
    list(POP_FRONT w name)
    list(APPEND tableNames "${name}")
    foreach(kv IN LISTS w)
      if(NOT kv MATCHES "^([a-z_]+)=([0-9]+)$")
        message(FATAL_ERROR "${TABLE_SHOWN}: '${kv}' on the line of ${name} is not COUNT=N")
      endif()
      set(want_${name}_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endforeach()
  endforeach()
  if(tableUngenerated)
    message(FATAL_ERROR "${TABLE_SHOWN}: the table of profile ${PROFILE} is marked ungenerated: "
                        "nothing to compare with.  Write it with ${regen}, check the counts it "
                        "wrote and commit it")
  endif()
endif()

rd_fixture_list("${LIST}" names)
set(failures 0)
set(diffs 0)
set(sumTwice 0)
set(sumVu 0)
set(runDevice "")
set(rows "")
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${PROFILE}} "${TOOL}" "${dump}" /dev/null
                          --stats --game-selftests ${args_${PROFILE}}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(SEND_ERROR "${name} (${PROFILE}): exit ${rc}:\n${out}\n${err}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  # the tool's lines with their ";" taken out (a ";" would split a list item)
  string(REPLACE ";" "," flat "${out}\n${err}")
  set(missing "")
  foreach(s IN LISTS said_${PROFILE})
    string(FIND "${flat}" "${s}" at)
    if(at EQUAL -1)
      list(APPEND missing "'${s}'")
    endif()
  endforeach()
  if(NOT flat MATCHES "${selfRe}")
    list(APPEND missing "the game self-tests' line")
  else()
    set(self "blend ${CMAKE_MATCH_1}, VU pre-pass ${CMAKE_MATCH_2}, fog ${CMAKE_MATCH_3}")
  endif()
  if(missing)
    string(REPLACE ";" ", " missing "${missing}")
    message(SEND_ERROR "${name} (${PROFILE}): the profile did not take (not in its log: "
                       "${missing}):\n${out}\n${err}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(runDevice STREQUAL "" AND flat MATCHES "${deviceRe}")
    set(runDevice "${CMAKE_MATCH_1}")
  endif()
  set(ok TRUE)
  if(flat MATCHES "${drawsRe}")
    set(got_draws "${CMAKE_MATCH_1}")
    set(got_passes "${CMAKE_MATCH_2}")
  else()
    set(ok FALSE)
  endif()
  if(flat MATCHES "${mergeRe}")
    set(got_vu_draws "${CMAKE_MATCH_1}")
    set(got_vu_gpu "${CMAKE_MATCH_2}")
    set(got_vu_merged "${CMAKE_MATCH_3}")
    set(got_vu_merged_into "${CMAKE_MATCH_4}")
    set(got_vu_alone "${CMAKE_MATCH_5}")
  else()
    set(ok FALSE)
  endif()
  if(flat MATCHES "${fuseRe}")
    set(got_fused "${CMAKE_MATCH_1}")
    set(got_kept "${CMAKE_MATCH_2}")
    set(got_kept_blend "${CMAKE_MATCH_3}")
    set(got_kept_plans "${CMAKE_MATCH_4}")
    set(got_kept_passes "${CMAKE_MATCH_5}")
    set(got_kept_dst "${CMAKE_MATCH_6}")
    set(got_kept_planes "${CMAKE_MATCH_7}")
    set(got_kept_build "${CMAKE_MATCH_8}")
  else()
    set(ok FALSE)
  endif()
  if(flat MATCHES "${fuse2Re}")
    set(got_kept_off "${CMAKE_MATCH_1}")
    set(got_fused_same "${CMAKE_MATCH_2}")
  else()
    set(ok FALSE)
  endif()
  if(flat MATCHES "${twiceRe}")
    set(got_twice "${CMAKE_MATCH_1}")
    set(got_twice_tris "${CMAKE_MATCH_2}")
    set(got_colour_alone "${CMAKE_MATCH_3}")
  else()
    set(ok FALSE)
  endif()
  if(flat MATCHES "${pipesRe}")
    set(got_pipelines "${CMAKE_MATCH_1}")
  else()
    set(ok FALSE)
  endif()
  if(NOT ok)
    message(SEND_ERROR "${name} (${PROFILE}): a count's stats line is missing (draws, VU merge, "
                       "VU fuse, two-pass blend, pipelines):\n${out}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  # the path ran: the fixture's declared pre-pass vertices where the profile
  # runs the VU pre-pass
  if(prepass_${PROFILE})
    rd_fixture_require(${name} "${out}" prepass_vertices)
  endif()
  math(EXPR sumTwice "${sumTwice} + ${got_twice}")
  math(EXPR sumVu "${sumVu} + ${got_vu_draws}")
  set(row "${name}")
  foreach(k IN LISTS keys)
    string(APPEND row " ${k}=${got_${k}}")
  endforeach()
  list(APPEND rows "${row}")
  message(STATUS "${row} (game self-tests: ${self})")
  if(NOT WRITE)
    list(FIND tableNames "${name}" at)
    if(at EQUAL -1)
      message(SEND_ERROR "${name}: not in ${TABLE_SHOWN}; this run:\n  ${row}")
      math(EXPR diffs "${diffs} + 1")
      continue()
    endif()
    set(lines "")
    foreach(k IN LISTS keys)
      if(NOT DEFINED want_${name}_${k})
        string(APPEND lines "\n  ${name} ${k}: not in the table, this run ${got_${k}}")
      elseif(NOT want_${name}_${k} STREQUAL got_${k})
        string(APPEND lines "\n  ${name} ${k}: table ${want_${name}_${k}}, this run ${got_${k}}")
      endif()
    endforeach()
    if(NOT lines STREQUAL "")
      message(SEND_ERROR "${name} (${PROFILE}): the counts differ from ${TABLE_SHOWN}:${lines}")
      math(EXPR diffs "${diffs} + 1")
    endif()
  endif()
endforeach()
if(NOT WRITE)
  foreach(name IN LISTS tableNames)
    list(FIND names "${name}" at)
    if(at EQUAL -1)
      message(SEND_ERROR "${TABLE_SHOWN} holds ${name}, which ${LIST} does not name")
      math(EXPR diffs "${diffs} + 1")
    endif()
  endforeach()
endif()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures EQUAL 0 AND sumVu EQUAL 0)
  message(SEND_ERROR "${PROFILE}: no VU draw in any fixture: the replays drew nothing the gate "
                     "measures")
  math(EXPR failures "${failures} + 1")
endif()
if(failures EQUAL 0 AND PROFILE STREQUAL "mali" AND sumTwice EQUAL 0)
  message(SEND_ERROR "mali: no draw drawn twice by the two-pass blend over the list: the profile "
                     "without dual-source blending did not take the two-pass blend")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_draw_pattern_${PROFILE}: ${failures} failures (the replays did not "
                      "run as the profile asks; nothing compared or written)")
endif()
if(WRITE)
  set(text "# The draw pattern of profile ${PROFILE} (rd_draw_pattern_${PROFILE},\n")
  string(APPEND text "# port/render/test/rd_draw_pattern.cmake; docs/port/TESTING.md, \"The draw pattern gate\"):\n")
  string(APPEND text "# a line a fixture of port/render/test/fixtures/lists/draw_pattern.txt, the counts\n")
  string(APPEND text "# rd_replay_tool --stats --game-selftests printed under the profile's fakes.  Written by\n")
  string(APPEND text "# the target rd_draw_pattern_tables; a change is committed with the change that made\n")
  string(APPEND text "# it and named in the release's device checklist.\n")
  string(APPEND text "# device: ${runDevice}\n")
  foreach(row IN LISTS rows)
    string(APPEND text "${row}\n")
  endforeach()
  file(WRITE "${TABLE}" "${text}")
  list(LENGTH rows nrows)
  message(STATUS "rd_draw_pattern_${PROFILE}: wrote ${TABLE} (${nrows} fixtures, ${runDevice})")
  return()
endif()
if(diffs GREATER 0)
  set(devNote "")
  if(NOT tableDevice STREQUAL runDevice)
    set(devNote " (the table was written on ${tableDevice}, this run is on ${runDevice})")
  endif()
  message(FATAL_ERROR "${changed}${devNote}.  ${diffs} fixtures differ; the table is written by "
                      "${regen}")
endif()
list(LENGTH names nnames)
message(STATUS "rd_draw_pattern_${PROFILE}: ${nnames} fixtures, every count the table's")
