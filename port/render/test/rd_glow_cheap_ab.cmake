# rd_glow_cheap_ab: the glow's cheaper passes (docs/port/RENDER.md, "The
# glow's passes") against the pass dumps the build before them stored, level
# for level, with fewer glow passes at the phone's setting.  The two cuts:
#   - the glow's WORK2 fill (staticBlur.c fillWork2's sprite, a clear) is the
#     load op of the mask's pass (rd_fx_plan.c fxDraw and rd_blur_border.c
#     sceneRepeat end the open pass for SCENE's read through
#     rd__replay_end_pass_for, the clear carry), not a pass of its own;
#   - cleanUpFB's two sprites into SCENE stay in the fog's pass (rd_fx_plan.c
#     fxAttachDepth: the constant fold with the open pass's depth attached
#     and unused), not two passes of their own.
# Both are the same bytes by construction, so the bound is 0 levels on every
# target and the present: -DBOUND (default 0) is the largest level
# difference allowed per channel.  No switch turns them off, so the A/B is
# against stored pass dumps (a baseline refresh is the orchestrator's): for
# every dump the list names (tools/glow_cheap_dumps.txt, read as
# rd_pass_area_ab.cmake reads its list), rd_replay_tool's --pass-dump with
# --stats under ICO_RD_BLEND=fixed and ICO_RD_FADES_FOLD=0 (the gate's), at
# three settings, each against its baseline folder when it has one:
#   phone   the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=moltenvk:
#           the tile class, its clear carry and its local fog) at 2364x1080,
#           Full height, Full pixel, the dump's own aspect, presented at
#           2364x1080, ICO_RD_VU_PREPASS=0; baseline -DBASE_PHONE/<name>
#           and its log <name>.log (build-host/tmp/v098/evidence/glow/pd-phone,
#           made by the tool of main e2484dfdd);
#   orig    Original 4:3 (the gate's "orig"); baseline -DBASE_DESK/<name>-orig;
#   enh2x43 Enhanced 2x 4:3 presented at 1280x960 (the gate's "enh2x43");
#           baseline -DBASE_DESK/<name>-enh2x43 (build-host/tmp/v095/after/w3/pd).
# <name> is the dump's folder and name joined as gate-wave.sh joins them.
# Every PNG of the baseline folder must be the new one's bytes or within
# BOUND levels of it (port/render/test/png_level_diff.py); the glow's passes ("glow N
# passes") at most the baseline's at orig and enh2x43 (the desktop profile:
# the clear carry is off there, and cleanUpFB joins the fog's pass only where
# that pass has the depth attached) and at least 2 fewer at the phone's
# setting (cleanUpFB's two passes; 3 where the fill had a pass of its own).
# The lines print each dump's passes before and after and its largest
# level.  A missing list, dump or baseline is named and skipped; nothing
# compared, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DBASE_PHONE=<dir> -DBASE_DESK=<dir>
# -DPY=<python3> -DDIFF=<png_level_diff.py> -DOUT=<a folder the test may write>
if("${BOUND}" STREQUAL "")
  set(BOUND 0)
endif()
set(dumps "")
if(NOT "${LIST}" STREQUAL "" AND EXISTS "${LIST}")
  get_filename_component(listDir "${LIST}" DIRECTORY)
  file(STRINGS "${LIST}" lines)
  foreach(l IN LISTS lines)
    string(STRIP "${l}" l)
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    if(NOT IS_ABSOLUTE "${l}")
      set(l "${listDir}/${l}")
    endif()
    if(l MATCHES "[*]")
      file(GLOB found "${l}")
      list(SORT found)
      if(NOT found)
        message(STATUS "${l}: no dump matches, skipped")
      endif()
      list(APPEND dumps ${found})
    elseif(EXISTS "${l}")
      list(APPEND dumps "${l}")
    else()
      message(STATUS "${l}: no such dump, skipped")
    endif()
  endforeach()
else()
  message(STATUS "${LIST}: no dump list, skipped")
endif()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()

set(settings phone orig enh2x43)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0)
set(args_orig --aspect 4:3)
set(env_orig "")
set(args_enh2x43 --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x43 "")
set(glowRe "glow ([0-9]+) passes")
set(diffRe "png_level_diff: [0-9]+ x [0-9]+, [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, [0-9]+ more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")
set(failures 0)
set(compared 0)
set(runs 0)
set(fewer 0)
foreach(dump IN LISTS dumps)
  # gate-wave.sh's name: the dump's folder, '-', its name without .rddump,
  # less the folder prefixes it strips
  get_filename_component(dir "${dump}" DIRECTORY)
  get_filename_component(dirName "${dir}" NAME)
  get_filename_component(full "${dump}" NAME)
  string(REGEX REPLACE "[.]rddump$" "" base "${full}")
  set(name "${dirName}-${base}")
  string(REGEX REPLACE "^(ico-dumps|102|95|deja|keep)-" "" name "${name}")
  foreach(s IN LISTS settings)
    if(s STREQUAL "phone")
      set(baseDir "${BASE_PHONE}/${name}")
      set(baseLog "${BASE_PHONE}/${name}.log")
    else()
      set(baseDir "${BASE_DESK}/${name}-${s}")
      set(baseLog "${BASE_DESK}/${name}-${s}.log")
    endif()
    if(NOT IS_DIRECTORY "${baseDir}" OR NOT EXISTS "${baseLog}")
      message(STATUS "${name} ${s}: no baseline (${baseDir}), skipped")
      continue()
    endif()
    file(READ "${baseLog}" baseText)
    if(NOT baseText MATCHES "${glowRe}")
      message(STATUS "${name} ${s}: the baseline's log has no glow pass count, skipped")
      continue()
    endif()
    set(glowBase "${CMAKE_MATCH_1}")
    set(dirNew "${OUT}/${name}-${s}")
    file(REMOVE_RECURSE "${dirNew}")
    file(MAKE_DIRECTORY "${dirNew}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0
                            ${env_${s}} "${TOOL}" "${dump}" "${dirNew}/out.png" --pass-dump
                            "${dirNew}" --stats ${args_${s}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    if(NOT rc EQUAL 0)
      message(SEND_ERROR "${name} ${s}: exit ${rc}:\n${err}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR runs "${runs} + 1")
    string(REPLACE ";" "," flat "${out}")
    if(NOT flat MATCHES "${glowRe}")
      message(SEND_ERROR "${name} ${s}: no glow pass count in the stats:\n${out}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(glowNew "${CMAKE_MATCH_1}")
    if(s STREQUAL "phone")
      math(EXPR most "${glowBase} - 2")
      if(glowNew GREATER most)
        message(SEND_ERROR "${name} ${s}: glow ${glowBase} passes before, ${glowNew} now: want at "
                           "most ${most} (cleanUpFB in the fog's pass, the WORK2 fill in the "
                           "mask's)")
        math(EXPR failures "${failures} + 1")
      endif()
    elseif(glowNew GREATER glowBase)
      message(SEND_ERROR "${name} ${s}: glow ${glowBase} passes before, ${glowNew} now: want at "
                         "most as many")
      math(EXPR failures "${failures} + 1")
    endif()
    if(glowNew LESS glowBase)
      math(EXPR fewer "${fewer} + 1")
    endif()
    file(GLOB pngs RELATIVE "${baseDir}" "${baseDir}/*.png")
    set(largest 0)
    set(differ 0)
    set(n 0)
    foreach(png IN LISTS pngs)
      if(png STREQUAL "out.png")
        continue()
      endif()
      if(NOT EXISTS "${dirNew}/${png}")
        message(SEND_ERROR "${name} ${s}: ${png} in the baseline, not written now")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR n "${n} + 1")
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${baseDir}/${png}"
                              "${dirNew}/${png}" RESULT_VARIABLE same)
      if(same EQUAL 0)
        continue() # the same bytes (one encoder): 0 levels
      endif()
      execute_process(COMMAND "${PY}" -I "${DIFF}" "${baseDir}/${png}" "${dirNew}/${png}"
                      RESULT_VARIABLE drc OUTPUT_VARIABLE dout ERROR_VARIABLE derr)
      if(NOT drc EQUAL 0 OR NOT dout MATCHES "${diffRe}")
        message(SEND_ERROR "${name} ${s}: ${png}: ${dout}${derr}")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(lv "${CMAKE_MATCH_1}")
      set(px "${CMAKE_MATCH_2}")
      if(px GREATER 0 AND lv GREATER largest)
        set(largest "${lv}")
      endif()
      math(EXPR differ "${differ} + ${px}")
      if(px GREATER 0 AND lv GREATER BOUND)
        message(SEND_ERROR "${name} ${s}: ${png}: ${px} pixels differ, largest ${lv} levels: the "
                           "bound is ${BOUND}")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    message(STATUS "${name} ${s}: glow ${glowBase} -> ${glowNew} passes; ${n} targets, largest "
                   "difference ${largest} levels, ${differ} pixels differ")
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_glow_cheap_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} had a baseline to compare")
  return()
endif()
message(STATUS "rd_glow_cheap_ab: ${compared} targets within ${BOUND} levels in ${runs} runs over "
               "${ndumps} dumps, ${fewer} runs with fewer glow passes")
