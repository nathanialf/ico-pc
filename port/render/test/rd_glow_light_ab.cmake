# rd_glow_light_ab: the glow's light plan (port/render/rd_glow_light.c;
# docs/port/RENDER_POST.md, "The glow where it adds light") against the chain
# drawn whole (ICO_RD_GLOW_LIGHT=0), on every glow dump of the list
# (tools/glow_light_dumps.txt, machine-local, read as rd_glow_cheap_ab.cmake
# reads its list, except that a listed dump that is absent FAILS: the owner's
# rule for v0.11.5).  For every dump, rd_replay_tool --pass-dump --stats
# twice (the chain whole, then the plan) at three settings:
#   phone   the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=moltenvk,
#           ICO_RD_VU_PREPASS=0) at 2364x1080, Full height, Full pixel,
#           presented at 2364x1080;
#   orig    Original 4:3 at 1x (the plan applies at every scale);
#   enh1x   Enhanced 1x at the phone's profile (moltenvk), presented at 2364x1080;
#   mali1x  Enhanced 1x under a Mali device's identity (ICO_VK_FAKE_VENDOR=0x13B5,
#           ICO_VK_FAKE_LIMITS=mali: issue 179's phone runs at 1x), presented at
#           1600x720;
#   enh2x43 Enhanced 2x 4:3 presented at 1280x960.
# Every PNG of the whole run against the plan's (png_level_diff.py):
#   - RGB 0 levels on every target and the present (--channels RGB);
#   - the alpha 0 levels on DISPLAY, PRESENT and every target but WORK1,
#     WORK2, WORK3, WORK3_HELD and SCENE, and on those too in a frame whose
#     whole run's WORK2 holds no alpha 192 (no fill kept by the mask: no
#     marked background, --count-alpha 192), the only place the plan may
#     change an alpha (rd_glow_light.c's header);
#   - at every row, 1x included, the same rules (RGB 0 levels, the alpha only
#     where the fill was kept).
# A fourth row, phonedef, the phone's setting with every other package's
# switch at its default (above: the late pass's join, VD-31, within its bound).
# The stats ("glow N passes", "glow: light plan on: <plan>; N passes in the
# chain"): a dark run's chain begins at most one pass (WORK1's zero draw over
# the first blur pass's rect, its strip and the sun's shrink: a fresh
# renderer's WORK1 is not known to be zero; WORK2, WORK2_PAD, WORK3 and WORK0
# are lazy clears, WORK0's drawn as the load of the aura's pass where the aura
# runs) and its glow passes are fewer than the whole run's (cleanUpFB's
# sprites stay: they are SCENE's, in the fog's pass on the phone's profile), a
# lamps run at least one fewer than the whole run, any other at most as many; at least one dark run at the phone's setting and at
# Enhanced 2x (the phone's own frame v3677 among them).  No device: SKIP (77).
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
      message(SEND_ERROR "${l}: listed but absent (the list names only dumps present)")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
else()
  message(FATAL_ERROR "rd_glow_light_ab: no dump list ${LIST}")
endif()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)

set(settings phone orig enh2x43 phonedef enh1x mali1x origdef)
# The plan alone: every run but phonedef pins the other packages' switches
# whose work depends on which pass is open or which passes the frame has, off
# in both runs.  Without the glow's chain (the dark plan) another pass is open
# at list 11, so the late pass (rd_late_pass.c, VD-31) joins list 11's draws
# that use no depth into it in one run and not in the other: its own accepted
# difference (up to 3 levels on menu text) would show here as the plan's.
# Measured on v1881 at the phone's setting (2026-11-12, the int tool): 105
# SCENE pixels 1-2 levels apart with the defaults, every one inside list 11's
# text sprites, none with ICO_RD_LATE_PASS=0.  The aura's paste, the crack
# fill's swap, the present from SCENE, the shadow box and the packed stencil
# are pinned for the same reason (each plans by the passes around it).
set(pins ICO_RD_LATE_PASS=0 ICO_RD_AURA_PASTE=0 ICO_RD_CRACK_SWAP=0 ICO_RD_PRESENT_SCENE=0
         ICO_RD_SHADOW_BOX=0 ICO_RD_STENCIL_PACKED=0)
# Every run, the defaults' ones too, sets ICO_RD_LAMPS=noglowblur: the glow
# blur in two passes (rd_glow_blur.c, VD-33: not byte-identical) takes lamps
# and whole plans but not the chain drawn whole (ICO_RD_GLOW_LIGHT=0), so a
# build with RD_GLOW_BLUR_DEFAULT 1 would put its difference between the
# arms; the lamps' other parts stay at the class's default
# phonedef: the phone's setting with every switch at its default, the
# interaction itself pinned: SCENE, DISPLAY and PRESENT within VD-31's bound
# (at most 3 levels on at most 800 pixels), every other target's RGB the same
set(args_phonedef ${args_phone})
set(env_phonedef ${env_phone})
# origdef: Original 4:3 at 1x at the defaults, the same bound
set(args_origdef ${args_orig})
set(env_origdef ${env_orig})
set(defMaxLevel 3)
set(defMaxPixels 800)
set(defTargets SCENE.png DISPLAY.png PRESENT.png)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0)
set(args_orig --aspect 4:3)
set(env_orig "")
set(args_enh1x --enhanced --resolution 1x --present 2364x1080)
set(env_enh1x ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0)
set(args_mali1x --enhanced --resolution 1x --present 1600x720)
set(env_mali1x ICO_VK_FAKE_VENDOR=0x13B5 ICO_VK_FAKE_LIMITS=mali ICO_RD_VU_PREPASS=0)
set(args_enh2x43 --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x43 "")
set(glowRe "glow ([0-9]+) passes")
set(planRe "glow: light plan (on|off): ([a-z]+)")
set(chainRe "; ([0-9]+) passes in the chain,")
set(diffRe "png_level_diff: [0-9]+ x [0-9]+, [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, [0-9]+ more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")
set(keptRe "png_level_diff: [0-9]+ x [0-9]+, ([0-9]+) pixels with alpha 192")
set(alphaFree WORK1.png WORK2.png WORK3.png WORK3_HELD.png SCENE.png)
set(runs 0)
set(dark_phone 0)
set(dark_enh2x43 0)
set(dark_orig 0)
set(dark_enh1x 0)
set(dark_mali1x 0)
set(dark_phonedef 0)
set(dark_origdef 0)
set(lamps 0)
set(whole 0)

# png_level_diff over channels _ch of _a and _b: _lv the largest level, _px
# the pixels that differ (-1 when unread)
function(level_diff _a _b _ch _lv _px)
  execute_process(COMMAND "${PY}" -I "${DIFF}" --channels ${_ch} "${_a}" "${_b}"
                  RESULT_VARIABLE drc OUTPUT_VARIABLE dout ERROR_VARIABLE derr)
  if(NOT drc EQUAL 0 OR NOT dout MATCHES "${diffRe}")
    message(STATUS "${_b}: ${dout}${derr}")
    set(${_lv} -1 PARENT_SCOPE)
    set(${_px} -1 PARENT_SCOPE)
    return()
  endif()
  set(${_lv} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(${_px} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

foreach(dump IN LISTS dumps)
  get_filename_component(dir "${dump}" DIRECTORY)
  get_filename_component(dirName "${dir}" NAME)
  get_filename_component(full "${dump}" NAME)
  string(REGEX REPLACE "[.](rddump|zip)$" "" base "${full}")
  set(name "${dirName}-${base}")
  foreach(s IN LISTS settings)
    set(dirW "${OUT}/${name}-${s}-whole")
    set(dirP "${OUT}/${name}-${s}-plan")
    file(REMOVE_RECURSE "${dirW}" "${dirP}")
    file(MAKE_DIRECTORY "${dirW}" "${dirP}")
    set(pin ${pins})
    if(s MATCHES "def$")
      set(pin "")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_GLOW_LIGHT=0 ICO_RD_LAMPS=noglowblur ${pin} ${env_${s}} "${TOOL}"
                            "${dump}" "${dirW}/out.png" --pass-dump "${dirW}" --stats
                            ${args_${s}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE wout ERROR_VARIABLE werr)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${werr}")
      return()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_LAMPS=noglowblur ${pin} ${env_${s}} "${TOOL}" "${dump}"
                            "${dirP}/out.png" --pass-dump "${dirP}" --stats ${args_${s}}
                    RESULT_VARIABLE prc OUTPUT_VARIABLE pout ERROR_VARIABLE perr)
    if(NOT rc EQUAL 0 OR NOT prc EQUAL 0)
      message(SEND_ERROR "${name} ${s}: exit ${rc} / ${prc}:\n${werr}\n${perr}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR runs "${runs} + 1")
    string(REPLACE ";" "," wflat "${wout}")
    string(REPLACE ";" "," pflat "${pout}")
    if(NOT wflat MATCHES "${glowRe}")
      message(SEND_ERROR "${name} ${s}: no glow pass count in the whole run's stats")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(glowW "${CMAKE_MATCH_1}")
    if(NOT pflat MATCHES "${glowRe}")
      message(SEND_ERROR "${name} ${s}: no glow pass count in the plan's stats")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(glowP "${CMAKE_MATCH_1}")
    if(NOT pflat MATCHES "${planRe}")
      message(SEND_ERROR "${name} ${s}: no light plan line in the stats:\n${pout}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(plan "${CMAKE_MATCH_2}")
    set(chainP 0)
    if(pflat MATCHES "${chainRe}")
      set(chainP "${CMAKE_MATCH_1}")
    endif()
    # the passes
    if(plan STREQUAL "dark")
      math(EXPR dark_${s} "${dark_${s}} + 1")
      if(chainP GREATER 1 OR NOT glowP LESS glowW)
        message(SEND_ERROR "${name} ${s}: dark, ${chainP} passes in the chain (want at most 1), "
                           "${glowP} glow passes against ${glowW} whole (want fewer)")
        math(EXPR failures "${failures} + 1")
      endif()
    elseif(plan STREQUAL "lamps")
      math(EXPR lamps "${lamps} + 1")
      math(EXPR most "${glowW} - 1")
      if(glowP GREATER most)
        message(SEND_ERROR "${name} ${s}: lamps, ${glowP} glow passes against ${glowW} whole "
                           "(want at least one fewer)")
        math(EXPR failures "${failures} + 1")
      endif()
    else()
      if(plan STREQUAL "whole")
        math(EXPR whole "${whole} + 1")
      endif()
      if(glowP GREATER glowW)
        message(SEND_ERROR "${name} ${s}: ${plan}, ${glowP} glow passes against ${glowW} whole")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    # a fill the mask kept: the alphas the plan may change
    set(kept 0)
    if(EXISTS "${dirW}/WORK2.png")
      execute_process(COMMAND "${PY}" -I "${DIFF}" --count-alpha 192 "${dirW}/WORK2.png"
                      OUTPUT_VARIABLE kout)
      if(kout MATCHES "${keptRe}")
        set(kept "${CMAKE_MATCH_1}")
      endif()
    endif()
    file(GLOB pngs RELATIVE "${dirW}" "${dirW}/*.png")
    set(n 0)
    set(alphaDiffer 0)
    foreach(png IN LISTS pngs)
      if(png STREQUAL "out.png")
        continue()
      endif()
      if(NOT EXISTS "${dirP}/${png}")
        message(SEND_ERROR "${name} ${s}: ${png} not written by the plan's run")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR n "${n} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirW}/${png}"
                              "${dirP}/${png}" RESULT_VARIABLE same)
      if(same EQUAL 0)
        continue()
      endif()
      level_diff("${dirW}/${png}" "${dirP}/${png}" RGB lv px)
      list(FIND defTargets "${png}" isDef)
      if(s MATCHES "def$" AND NOT isDef EQUAL -1 AND px GREATER -1 AND
         NOT px GREATER defMaxPixels AND NOT lv GREATER defMaxLevel)
        if(px GREATER 0)
          message(STATUS "${name} ${s}: ${png}: RGB ${px} pixels within ${lv} levels (the late "
                         "pass's join, VD-31)")
        endif()
        set(px 0)
      endif()
      if(NOT px EQUAL 0)
        message(SEND_ERROR "${name} ${s}: ${png}: RGB ${px} pixels differ, largest ${lv} levels")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      level_diff("${dirW}/${png}" "${dirP}/${png}" A lv px)
      if(px EQUAL 0)
        continue()
      endif()
      list(FIND alphaFree "${png}" free)
      if(free EQUAL -1 OR kept EQUAL 0)
        message(SEND_ERROR "${name} ${s}: ${png}: alpha ${px} pixels differ (largest ${lv}); "
                           "WORK2 kept ${kept} fill pixels")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR alphaDiffer "${alphaDiffer} + 1")
    endforeach()
    message(STATUS "${name} ${s}: plan ${plan}; glow ${glowW} -> ${glowP} passes (${chainP} in "
                   "the chain); ${n} targets, "
                   "RGB the same, ${alphaDiffer} with the alpha changed (WORK2 kept ${kept} fill "
                   "pixels)")
  endforeach()
endforeach()
if(runs GREATER 0)
  foreach(s phone enh2x43 orig enh1x mali1x)
    if(dark_${s} EQUAL 0)
      message(SEND_ERROR "rd_glow_light_ab: no dark run at ${s}")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_glow_light_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(runs EQUAL 0)
  message(STATUS "SKIP: nothing ran")
  return()
endif()
message(STATUS "rd_glow_light_ab: ${runs} runs over ${ndumps} dumps: dark ${dark_phone} at the "
               "phone's setting and ${dark_enh2x43} at Enhanced 2x, lamps ${lamps}, whole "
               "${whole}; RGB the same everywhere")
