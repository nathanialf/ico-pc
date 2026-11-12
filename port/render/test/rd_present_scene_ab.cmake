# rd_present_scene_ab: the present from SCENE (port/render/rd_present_scene.c;
# docs/port/RENDER_POST.md "The present from SCENE (off by default until
# measured)") against the reduction drawn into DISPLAY.  For every dump the
# list names (tools/present_scene_dumps.txt; a listed dump that is absent
# FAILS), rd_replay_tool --pass-dump --stats with ICO_RD_PRESENT_SCENE=0 (the
# reduction drawn, v0.11.5's default) and =1 at three settings:
#   phone   the PowerVR phone's Resolution Auto on lavapipe (2364x1080, full
#           height, Full pixel, ICO_VK_FAKE_LIMITS=moltenvk,
#           ICO_RD_VU_PREPASS=0): the reduction not drawn ("1 reductions not
#           drawn") on every dump the tool can show there, its pass and its
#           DISPLAY store gone ("N passes" fewer, "MB stored" less);
#   desk    a 16:9 desktop window (1920x1080, full height, Full pixel);
#   enh2x   Enhanced 2x at 4:3 into 1280x960: kept (the PS2's quarter pixel
#           is half a texel there, "not one SCENE texel a DISPLAY texel");
# every target's PNG but PRESENT byte for byte (DISPLAY drawn on demand from
# SCENE for its read: the same bytes the reduction stores), PRESENT within
# the bound (two levels a channel: one from g's rounding before the filter,
# one where the sampler weighs the moved coordinate's taps differently;
# png_level_diff.py, its counts printed).  On the first dump, at the phone's
# setting, the readers that keep the reduction, each with its reason and
# every target and PRESENT byte for byte: the CRT filter (--crt consumer; it
# locks the scene to 1x, so kept as scale 1 or as the filter),
# FSR 1.0 (--fsr1), the display layer under a temporal upscaler
# (--upscale-test 1.5), Downsampling (--downsample); and the mirror, which
# takes it (PRESENT within the bound).  The off runs must say "present from
# SCENE off".  The crack fill's swap (rd_crack_fill.c) as often in the on run
# as in the off run, and at the phone's setting some dump with the reduction
# not drawn and the swap taken in one replay (the display layer's switch on
# in every run, its default: it keeps nothing without a temporal upscaler);
# the first dump presented 31 times (--time 30) prints the 10 s line with a
# rate above 0.  No device: SKIP (77); no list: SKIP.
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DPY=<python3>
# -DDIFF=<png_level_diff.py> -DOUT=<a folder the test may write>
set(dumps "")
set(failures 0)
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
    if(EXISTS "${l}")
      list(APPEND dumps "${l}")
    else()
      message(SEND_ERROR "${l}: a listed dump is absent (the list names only dumps present)")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
else()
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(FATAL_ERROR "no dump of ${LIST} is here")
endif()
file(MAKE_DIRECTORY "${OUT}/tmp")

set(settings phone desk enh2x)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_desk "")
set(args_desk --enhanced --resolution 1920x1080 --full-height --full-pixel --present 1920x1080)
set(env_enh2x "")
set(args_enh2x --enhanced --resolution 2x --present 1280x960)
set(statRe "stats: present from SCENE (on|off): ([0-9]+) reductions not drawn, ([0-9]+) presents from SCENE, ([0-9]+) kept( \\(([^)]*)\\))?")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored[^\n]* ([0-9]+) passes")
set(swapRe "stats: crack fill swap (on|off) \\(self-test (passed|failed|not run)\\): ([0-9]+) snapshots by the swap[^\n]*, ([0-9]+) copied")
set(lineRe "rd: present from SCENE: ([0-9.]+) a replay \\(([0-9.]+) presents from SCENE\\), ([0-9.]+) through DISPLAY")
set(diffRe "png_level_diff: [0-9]+ x [0-9]+, ([0-9]+) channels: [0-9]+ equal, ([0-9]+) one level apart, ([0-9]+) more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")

# one run: the tool's exit, its output in out_<var>
function(runOne var dump dir onoff env)
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "TMPDIR=${OUT}/tmp" ICO_RD_PRESENT_SCENE=${onoff}
                          ${env} "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${ARGN}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(rc_${var} "${rc}" PARENT_SCOPE)
  set(out_${var} "${out}" PARENT_SCOPE)
  set(err_${var} "${err}" PARENT_SCOPE)
endfunction()

# the two runs' PNGs: every one but PRESENT byte for byte (same: PRESENT
# too); PRESENT's largest difference in largest_<tag>
function(compareDirs tag dirOff dirOn same)
  set(bad 0)
  file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
  list(LENGTH pngs n)
  if(n EQUAL 0)
    message(SEND_ERROR "${tag}: no PNG written")
    set(bad 1)
  endif()
  set(largest 0)
  foreach(png IN LISTS pngs)
    if(NOT EXISTS "${dirOn}/${png}")
      message(SEND_ERROR "${tag}: ${png} missing from the on run")
      math(EXPR bad "${bad} + 1")
      continue()
    endif()
    if(png STREQUAL "PRESENT.png" AND NOT same)
      execute_process(COMMAND "${PY}" -I "${DIFF}" "${dirOff}/${png}" "${dirOn}/${png}"
                      RESULT_VARIABLE rcD OUTPUT_VARIABLE outD)
      if(NOT rcD EQUAL 0 OR NOT outD MATCHES "${diffRe}")
        message(SEND_ERROR "${tag}: PRESENT not compared: ${outD}")
        math(EXPR bad "${bad} + 1")
        continue()
      endif()
      set(largest "${CMAKE_MATCH_4}")
      message(STATUS "${tag}: PRESENT: ${CMAKE_MATCH_5} pixels differ, ${CMAKE_MATCH_2} channels a level apart, ${CMAKE_MATCH_3} more, largest ${CMAKE_MATCH_4}")
      if(CMAKE_MATCH_4 GREATER 2)
        message(SEND_ERROR "${tag}: PRESENT ${CMAKE_MATCH_4} levels from the reduction's (2 the bound)")
        math(EXPR bad "${bad} + 1")
      endif()
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}" "${dirOn}/${png}"
                    RESULT_VARIABLE rcC)
    if(NOT rcC EQUAL 0)
      message(SEND_ERROR "${tag}: ${png} differs (byte for byte expected)")
      math(EXPR bad "${bad} + 1")
    endif()
  endforeach()
  set(bad_${tag} "${bad}" PARENT_SCOPE)
endfunction()

set(taken 0)
set(phoneTaken 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(tag "${index}-${s}")
    runOne(off "${dump}" "${OUT}/${tag}-off" 0 "${env_${s}}" ${args_${s}})
    if(rc_off EQUAL 77)
      message(STATUS "SKIP: no device: ${err_off}")
      return()
    endif()
    if(NOT rc_off EQUAL 0 AND "${err_off}" MATCHES "cannot show")
      message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
      continue()
    endif()
    runOne(on "${dump}" "${OUT}/${tag}-on" 1 "${env_${s}}" ${args_${s}})
    if(NOT rc_off EQUAL 0 OR NOT rc_on EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rc_off} (off) and ${rc_on} (on):\n${err_off}\n${err_on}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    if(NOT out_off MATCHES "stats: present from SCENE off: 0 reductions not drawn")
      message(SEND_ERROR "${dump} ${s}: the off run is not off with none skipped")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT out_on MATCHES "${statRe}")
      message(SEND_ERROR "${dump} ${s}: no present from SCENE stats line:\n${out_on}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(skipped "${CMAKE_MATCH_2}")
    set(keptWhy "${CMAKE_MATCH_6}")
    string(REGEX MATCH "${trafficRe}" t "${out_off}")
    set(storedOff "${CMAKE_MATCH_2}")
    set(passesOff "${CMAKE_MATCH_3}")
    string(REGEX MATCH "${trafficRe}" t "${out_on}")
    set(storedOn "${CMAKE_MATCH_2}")
    set(passesOn "${CMAKE_MATCH_3}")
    message(STATUS "${dump} ${s}: ${skipped} reductions not drawn (kept: ${keptWhy}); passes ${passesOff} -> ${passesOn}, colour stored ${storedOff} -> ${storedOn} MB")
    if(s STREQUAL "enh2x" AND NOT skipped EQUAL 0)
      message(SEND_ERROR "${dump} enh2x: the reduction not drawn where its sample blends two columns")
      math(EXPR failures "${failures} + 1")
    endif()
    if(skipped EQUAL 1)
      math(EXPR taken "${taken} + 1")
      if(NOT passesOn LESS passesOff OR NOT storedOn LESS storedOff)
        message(SEND_ERROR "${dump} ${s}: the reduction not drawn but no pass or store fewer (${passesOff} -> ${passesOn} passes, ${storedOff} -> ${storedOn} MB)")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    # the crack fill's swap: the present from SCENE takes no refusal from it,
    # so the on run swaps as often as the off run (the display layer's switch
    # on in both, its default: the layer itself needs a temporal upscaler)
    set(swapsOff 0)
    set(swapsOn 0)
    if(out_off MATCHES "${swapRe}")
      set(swapsOff "${CMAKE_MATCH_3}")
    endif()
    if(out_on MATCHES "${swapRe}")
      set(swapsOn "${CMAKE_MATCH_3}")
    endif()
    message(STATUS "${dump} ${s}: crack fill swaps ${swapsOff} off, ${swapsOn} on")
    if(NOT swapsOn EQUAL swapsOff)
      message(SEND_ERROR "${dump} ${s}: the crack fill swapped ${swapsOff} times off and ${swapsOn} on (the same expected)")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "phone" AND skipped EQUAL 1 AND swapsOn GREATER 0)
      math(EXPR phoneTaken "${phoneTaken} + 1")
    endif()
    compareDirs("${tag}" "${OUT}/${tag}-off" "${OUT}/${tag}-on" FALSE)
    math(EXPR failures "${failures} + ${bad_${tag}}")
    # the readers, on the first dump at the phone's setting
    if(index EQUAL 1 AND s STREQUAL "phone")
      set(readers crt fsr1 layer downsample mirror)
      set(rargs_crt --crt consumer)
      set(rwhy_crt "*") # the CRT filter locks the scene to 1x: kept as scale 1 or as the filter
      set(rargs_fsr1 --fsr1)
      set(rwhy_fsr1 "an upscaler")
      set(rargs_layer --upscale-test 1.5)
      set(rwhy_layer "an upscaler")
      set(rargs_downsample --downsample)
      set(rwhy_downsample "Downsampling")
      set(rargs_mirror --mirror)
      set(rwhy_mirror "")
      foreach(rd IN LISTS readers)
        set(rtag "${tag}-${rd}")
        runOne(off "${dump}" "${OUT}/${rtag}-off" 0 "${env_phone}" ${args_phone} ${rargs_${rd}})
        runOne(on "${dump}" "${OUT}/${rtag}-on" 1 "${env_phone}" ${args_phone} ${rargs_${rd}})
        if(NOT rc_off EQUAL 0 OR NOT rc_on EQUAL 0)
          message(SEND_ERROR "${dump} ${rd}: exit ${rc_off} (off) and ${rc_on} (on):\n${err_off}\n${err_on}")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        if(NOT out_on MATCHES "${statRe}")
          message(SEND_ERROR "${dump} ${rd}: no present from SCENE stats line")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        set(rskipped "${CMAKE_MATCH_2}")
        set(rwhy "${CMAKE_MATCH_6}") # group 5 is " (why)", 6 the why alone
        if(rwhy_${rd} STREQUAL "")
          message(STATUS "${dump} ${rd}: ${rskipped} reductions not drawn")
          if(NOT rskipped EQUAL 1)
            message(SEND_ERROR "${dump} ${rd}: the reduction drawn (${rwhy})")
            math(EXPR failures "${failures} + 1")
          endif()
          compareDirs("${rtag}" "${OUT}/${rtag}-off" "${OUT}/${rtag}-on" FALSE)
        else()
          message(STATUS "${dump} ${rd}: kept (${rwhy})")
          if(NOT rskipped EQUAL 0 OR (NOT rwhy_${rd} STREQUAL "*" AND NOT rwhy STREQUAL "${rwhy_${rd}}"))
            message(SEND_ERROR "${dump} ${rd}: ${rskipped} reductions not drawn, kept for '${rwhy}' ('${rwhy_${rd}}' expected)")
            math(EXPR failures "${failures} + 1")
          endif()
          compareDirs("${rtag}" "${OUT}/${rtag}-off" "${OUT}/${rtag}-on" TRUE)
        endif()
        math(EXPR failures "${failures} + ${bad_${rtag}}")
      endforeach()
    endif()
  endforeach()
endforeach()
# the phone's shape: the reduction not drawn and the crack fill's swap taken
# in one replay (the display layer's switch on), on some dump
if(phoneTaken EQUAL 0)
  message(SEND_ERROR "no dump at the phone's setting had the reduction not drawn with the crack fill's swap taken")
  math(EXPR failures "${failures} + 1")
endif()
# the 10 s line: the first dump presented 31 times at the phone's setting
# (about 1.4 s of lavapipe a replay: past 10 s), the line's rate above 0
list(GET dumps 0 first)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "TMPDIR=${OUT}/tmp" ICO_RD_PRESENT_SCENE=1
                        ${env_phone} "${TOOL}" "${first}" "${OUT}/ten.png" --stats --time 30
                        ${args_phone}
                RESULT_VARIABLE rcT OUTPUT_VARIABLE outT ERROR_VARIABLE errT)
if(NOT rcT EQUAL 0 OR NOT "${outT}${errT}" MATCHES "${lineRe}")
  message(SEND_ERROR "the 10 s line: exit ${rcT}, no 'present from SCENE: N a replay' line:\n${errT}")
  math(EXPR failures "${failures} + 1")
else()
  message(STATUS "the 10 s line: ${CMAKE_MATCH_1} a replay, ${CMAKE_MATCH_2} presents from SCENE, ${CMAKE_MATCH_3} through DISPLAY")
  if(NOT CMAKE_MATCH_1 GREATER 0 OR NOT CMAKE_MATCH_2 GREATER 0)
    message(SEND_ERROR "the 10 s line says ${CMAKE_MATCH_1} a replay (${CMAKE_MATCH_2} presents from SCENE): above 0 expected")
    math(EXPR failures "${failures} + 1")
  endif()
endif()
if(taken EQUAL 0)
  message(SEND_ERROR "no dump's reduction was left undrawn at any setting (the gate saw nothing)")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_present_scene_ab: ${failures} failures")
endif()
message(STATUS "rd_present_scene_ab: ${ndumps} dumps, ${taken} runs with the reduction not drawn")
