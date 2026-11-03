# rd_crack_late_ab: the crack fill drawn after lists 4-6 (port/render/rd_crack_fill.c;
# docs/port/RENDER_POST.md, "Crack filling above 1x"; docs/port/DIVERGENCES.md VD-20)
# against pass dumps of the build that drew it before the fog.  Each line of the list
# (tools/crack_late_dumps.txt: "<setting> <dump> <baseline folder>") is replayed with
# rd_replay_tool --pass-dump --stats under ICO_RD_BLEND=fixed and ICO_RD_FADES_FOLD=0
# (the baselines' own; the fades fold has its own A/B, rd_fades_fold_ab) at the setting:
#   enh2x43  Enhanced 2x at 4:3, presented at 1280x960 (the pass-dump gate's);
#   phone    the PowerVR phone's Resolution Auto (2364x1080, Full height, Full pixel,
#            aspect 2.189, presented at 2364x1080) under its profile's fakes (the local
#            fog, DATE's stencil, the store plan, the blur fold, the fx local read, no UINT
#            views, the VU planes at whole scales only, the shadow count's one draw, the
#            vertex pre-pass off, no dynamic state).
# The run must say "crack fill on: 1 passes" and "1 fills after lists 4-6".  Every
# target's PNG of the baseline is compared with the run's (png_level_diff.py): the
# pixels that differ at most MAX_SHARE parts per million of the target, no channel more
# than MAX_LEVEL levels apart (the measured bound, VD-20: the crack texels take their
# neighbour's colour as the fog and lists 4-6 left it; the downstream effects that read
# SCENE carry it on).  At the phone's setting the stats line's copies are one fewer than
# the baseline's (the depth no longer copied for the fill) and its passes no more.  The
# differing pixels and the largest difference are printed per target.  A line whose dump
# or baseline is missing is named and skipped; none compared, or no device: SKIP.
# -DTOOL=<rd_replay_tool> -DLIST=<the list> -DPY=<python3> -DDIFF=<png_level_diff.py>
# -DOUT=<a folder the test may write>
set(MAX_LEVEL 64)
set(MAX_SHARE 2000) # parts per million: 0.2 % of a target's pixels
set(runs "")
if(NOT "${LIST}" STREQUAL "" AND EXISTS "${LIST}")
  get_filename_component(listDir "${LIST}" DIRECTORY)
  file(STRINGS "${LIST}" lines)
  foreach(l IN LISTS lines)
    string(STRIP "${l}" l)
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    string(REGEX REPLACE "[ \t]+" ";" parts "${l}")
    list(LENGTH parts np)
    if(NOT np EQUAL 3)
      message(STATUS "${l}: not '<setting> <dump> <baseline>', skipped")
      continue()
    endif()
    list(GET parts 0 s)
    list(GET parts 1 d)
    list(GET parts 2 b)
    if(NOT IS_ABSOLUTE "${d}")
      set(d "${listDir}/${d}")
    endif()
    if(NOT IS_ABSOLUTE "${b}")
      set(b "${listDir}/${b}")
    endif()
    if(NOT EXISTS "${d}" OR NOT IS_DIRECTORY "${b}")
      message(STATUS "${d} or ${b}: missing, skipped")
      continue()
    endif()
    list(APPEND runs "${s}|${d}|${b}")
  endforeach()
else()
  message(STATUS "${LIST}: no dump list, skipped")
endif()
list(LENGTH runs nruns)
if(nruns EQUAL 0)
  message(STATUS "SKIP: no dump and baseline of ${LIST} is here")
  return()
endif()

set(args_enh2x43 --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x43 "")
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
               --present 2364x1080)
set(env_phone ICO_RD_VU_PREPASS=0 ICO_VK_FAKE_NO_DYNAMIC=1 ICO_RD_FOG_PATH=local
              ICO_RD_DATE_PATH=stencil ICO_RD_STORE_PLAN=1 ICO_RD_BLUR_FOLD=1
              ICO_RD_FX_DST_LOCAL=1 ICO_RD_UINT_VIEW=0 ICO_RD_VU_PLANES=0 ICO_RD_SHADOW_ONE=1)
# (the outputs' semicolons read as commas: CMake's list separator replaced)
set(lateRe "stats: crack fill on: 1 passes, [0-9]+ pixels filled \\([^)]*, 1 fills after lists 4-6,")
set(countRe "stats: [0-9]+ draws, ([0-9]+) passes, [^\n]* ([0-9]+) copies,")
set(diffRe "png_level_diff: ([0-9]+) x ([0-9]+), [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, [0-9]+ more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")
set(failures 0)
set(compared 0)
set(index 0)
set(worstLevel 0)
foreach(run IN LISTS runs)
  string(REPLACE "|" ";" r "${run}")
  list(GET r 0 s)
  list(GET r 1 dump)
  list(GET r 2 base)
  math(EXPR index "${index} + 1")
  set(dir "${OUT}/${index}-${s}")
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0
                          ${env_${s}} "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats
                          ${args_${s}}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(SEND_ERROR "${dump} ${s}: exit ${rc}:\n${err}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  string(REPLACE ";" "," flat "${out}")
  if(NOT flat MATCHES "${lateRe}")
    message(SEND_ERROR "${dump} ${s}: the fill was not drawn once after lists 4-6:\n${out}")
    math(EXPR failures "${failures} + 1")
  endif()
  if(s STREQUAL "phone" AND EXISTS "${base}.log")
    # the baseline's own run log (ab.sh writes <baseline>.log next to the folder)
    file(READ "${base}.log" baseLog)
    string(REPLACE ";" "," baseLog "${baseLog}")
    if(flat MATCHES "${countRe}")
      set(passes "${CMAKE_MATCH_1}")
      set(copies "${CMAKE_MATCH_2}")
      if(baseLog MATCHES "${countRe}")
        set(basePasses "${CMAKE_MATCH_1}")
        set(baseCopies "${CMAKE_MATCH_2}")
        math(EXPR wantCopies "${baseCopies} - 1")
        message(STATUS "${dump} ${s}: ${passes} passes and ${copies} copies, the baseline "
                       "${basePasses} and ${baseCopies}")
        if(NOT copies EQUAL wantCopies OR passes GREATER basePasses)
          message(SEND_ERROR "${dump} ${s}: ${passes} passes and ${copies} copies against the "
                             "baseline's ${basePasses} and ${baseCopies} (one copy fewer, no "
                             "pass more)")
          math(EXPR failures "${failures} + 1")
        endif()
      endif()
    endif()
  endif()
  file(GLOB pngs RELATIVE "${base}" "${base}/*.png")
  foreach(png IN LISTS pngs)
    if(NOT EXISTS "${dir}/${png}")
      message(SEND_ERROR "${dump} ${s}: ${png} in the baseline only")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR compared "${compared} + 1")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${base}/${png}" "${dir}/${png}"
                    RESULT_VARIABLE same)
    if(same EQUAL 0)
      continue()
    endif()
    execute_process(COMMAND "${PY}" -I "${DIFF}" "${base}/${png}" "${dir}/${png}"
                    RESULT_VARIABLE rcDiff OUTPUT_VARIABLE outDiff)
    string(STRIP "${outDiff}" outDiff)
    if(NOT rcDiff EQUAL 0 OR NOT outDiff MATCHES "${diffRe}")
      message(SEND_ERROR "${dump} ${s}: ${png} differs and cannot be measured: ${outDiff}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(w "${CMAKE_MATCH_1}")
    set(h "${CMAKE_MATCH_2}")
    set(largest "${CMAKE_MATCH_3}")
    set(pixels "${CMAKE_MATCH_4}")
    math(EXPR ppm "${pixels} * 1000000 / (${w} * ${h})")
    if(largest GREATER worstLevel)
      set(worstLevel "${largest}")
    endif()
    message(STATUS "  ${dump} ${s} ${png}: ${pixels} pixels differ (${ppm} per million), the "
                   "largest ${largest} levels")
    if(largest GREATER MAX_LEVEL OR ppm GREATER MAX_SHARE)
      message(SEND_ERROR "${dump} ${s}: ${png} has ${pixels} pixels apart (${ppm} per million, "
                         "${MAX_SHARE} the bound), the largest ${largest} levels (${MAX_LEVEL} "
                         "the bound)")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_crack_late_ab: ${failures} failures over ${nruns} runs")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no target of ${LIST} could be compared")
  return()
endif()
message(STATUS "rd_crack_late_ab: ${compared} targets within ${MAX_LEVEL} levels on at most "
               "${MAX_SHARE} pixels a million over ${nruns} runs (the largest ${worstLevel})")
