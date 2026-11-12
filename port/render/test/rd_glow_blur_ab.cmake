# rd_glow_blur_ab: the glow's blur pairs in two passes (port/render/rd_glow_blur.c,
# ICO_RD_LAMPS=...+glowblur; identical to the eye, not byte-identical:
# docs/port/DIVERGENCES.md VD-33) against today's chain, both arms with the
# byte-identical items pad+owed+wholepad, on every dump of the lists (LIST
# and LIST2: tools/glow_light_dumps.txt and tools/lamps_dumps.txt,
# machine-local; a listed dump that is absent FAILS), rd_replay_tool
# --pass-dump --stats, ICO_RD_VU_PREPASS=0 ICO_RD_LATE_PASS=1, at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced 2364x1080, Full
#           height, Full pixel, presented at 2364x1080) under
#           ICO_VK_FAKE_LIMITS=moltenvk (the tile class's blur fold on);
#   enh43   Enhanced 2x at 4:3, presented at 1280x960, the same fakes;
#   enh3x   Enhanced 3x at 4:3, presented at 1920x1440;
#   enh4x   Enhanced 4x at 4:3, presented at 2560x1920;
#   mali    the phone's setting under the Mali fakes (no blur fold: the
#           chain's twenty passes against two);
#   orig    Original 4:3 at 1x (--aspect 4:3, lavapipe's own class): the glow
#           blur applies at every scale and preset (the owner's rule), so
#           Original's 1x is measured as the others;
#   enh1x   Enhanced 1x presented at 2364x1080 under the MoltenVK fake;
#   poison  the phone's setting, glowblur against glowblur under
#           ICO_RD_LAMPS_POISON=1 (WORK0 magenta before the down pass): every
#           PNG the same bytes (the down pass writes every texel of WORK0).
# With BACKEND opengl (rd_glow_blur_gl): the phone's setting on OpenGL alone.
# A dump whose chain the first setting (phone; gl on OpenGL) leaves as
# recorded (no glow, a dark plan, a refused form) is checked there (every
# PNG the same bytes) and not run at the others.
# Each measured setting, per dump, reads the tool's "stats: glow blur" line
# (this replay's chain in two passes and its pairs), "stats: N draws, M
# passes" and "blur pairs folded F", and asserts:
#   - the glowblur arm "on", today's "off", and today's no chain;
#   - where no chain was drawn in two passes: every PNG the same bytes;
#   - where one was (P pairs): passes = today's - (2 P - (F today - F
#     glowblur)) + 2 (the chain's passes as today draws them, the blur
#     fold's pairs one pass each, against the two; rd_glow_blur.c);
#     SHADOW0..2 and WORK2_PAD the same bytes (WORK2 is drawn before the
#     chain, but a later sprite of the frame may take SCENE into it); WORK3,
#     WORK0 (RGB) and
#     DISPLAY and PRESENT (RGB) at most w3Max, w3Max and dispMax levels
#     from today's (printed per dump as "glow blur measure: ...", with each
#     setting's maxima at the end);
#   - at the phone's setting, 2x, Original 1x and Enhanced 1x, the shimmer (WORK3's change under
#     --jitter 0.5,0.5, the lamps package's measure) with glowblur at most
#     shimNum/shimDen of today's plus shimPlus levels;
#   - at least one chain in two passes over the list at each measured setting.
# The bounds (the evidence's Python fit, glowblur/design.py and synth.py:
# WORK3 at most 14 and 18 levels on the title and issue 163's lamps, 18 to
# 20 on synthetic lamps at the phone's 424 x 128; the shimmer 1.15 times
# today's at most; at Original 1x WORK3 at most 16 and 16, a mean of 0.11
# and 0.36): WORK3 and WORK0 24 levels (20 and a margin of 4),
# DISPLAY and PRESENT 26 (the paste adds WORK3 at FIX 128 through the GS
# bilinear, the reduction and the present filter average it: at most
# WORK3's, plus 2 for their roundings); the shimmer 5/4 of today's + 4.
# No device: SKIP (77).
# -DPY=<python3> -DDIFF=<png_level_diff.py>
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DLIST2=<dump list> -DOUT=<a folder>
# -DBACKEND=vulkan|opengl
set(failures 0)
set(dumps "")
if("${PY}" STREQUAL "" OR "${DIFF}" STREQUAL "")
  message(FATAL_ERROR "rd_glow_blur_ab: -DPY and -DDIFF are needed")
endif()
foreach(lst IN ITEMS "${LIST}" "${LIST2}")
  if("${lst}" STREQUAL "" OR NOT EXISTS "${lst}")
    message(FATAL_ERROR "rd_glow_blur_ab: no dump list ${lst}")
  endif()
  get_filename_component(listDir "${lst}" DIRECTORY)
  file(STRINGS "${lst}" lines)
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
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)

set(w3Max 24)
set(dispMax 26)
set(shimNum 5)
set(shimDen 4)
set(shimPlus 4)

set(common ICO_RD_VU_PREPASS=0 ICO_RD_LATE_PASS=1)
set(armOff "ICO_RD_LAMPS=pad+owed+wholepad")
set(armOn "ICO_RD_LAMPS=pad+owed+wholepad+glowblur")
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(args_enh43 --enhanced --resolution 2x --aspect 1.3333 --present 1280x960)
set(args_enh3x --enhanced --resolution 3x --aspect 1.3333 --present 1920x1440)
set(args_enh4x --enhanced --resolution 4x --aspect 1.3333 --present 2560x1920)
set(args_mali ${args_phone})
set(args_orig --aspect 4:3)
set(args_enh1x --enhanced --resolution 1x --present 2364x1080)
set(args_poison ${args_phone})
set(args_gl ${args_phone})
set(fake_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(fake_enh43 ${fake_phone})
set(fake_enh3x ${fake_phone})
set(fake_enh4x ${fake_phone})
set(fake_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5)
set(fake_orig "")
set(fake_enh1x ${fake_phone})
set(fake_poison ${fake_phone})
set(fake_gl "")
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
  set(measured gl)
  set(shimSettings "")
else()
  set(settings phone enh43 enh3x enh4x mali orig enh1x poison)
  set(measured phone enh43 enh3x enh4x mali orig enh1x)
  set(shimSettings phone enh43 orig enh1x)
endif()

set(blurRe "stats: glow blur (on|off): ([0-9]+) chains in two passes \\(([0-9]+) pairs\\)")
set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(foldsRe "blur pairs folded ([0-9]+)")
set(diffRe "png_level_diff: ([0-9]+) x ([0-9]+), [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, [0-9]+ more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")

foreach(s IN LISTS settings)
  set(chains_${s} 0)
  foreach(t WORK3 WORK0 DISPLAY PRESENT)
    set(max_${s}_${t} 0)
    set(where_${s}_${t} "")
  endforeach()
endforeach()

# png_level_diff of a and b over channels ch: largest (0 when the same bytes)
# into ${out}_max, the pixels apart into ${out}_n, their total into ${out}_t
macro(level_diff out a b ch)
  set(${out}_max 0)
  set(${out}_n 0)
  set(${out}_t 1)
  set(${out}_ok TRUE)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${a}" "${b}" RESULT_VARIABLE _same)
  if(NOT _same EQUAL 0)
    execute_process(COMMAND "${PY}" -I "${DIFF}" --channels ${ch} "${a}" "${b}"
                    OUTPUT_VARIABLE _dout RESULT_VARIABLE _drc)
    if(_drc EQUAL 0 AND _dout MATCHES "${diffRe}")
      math(EXPR ${out}_t "${CMAKE_MATCH_1} * ${CMAKE_MATCH_2}")
      set(${out}_max "${CMAKE_MATCH_3}")
      set(${out}_n "${CMAKE_MATCH_4}")
    else()
      set(${out}_ok FALSE)
    endif()
  endif()
endmacro()

set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(hasChain TRUE)
  foreach(s IN LISTS settings)
    # a dump whose chain the first setting drew as recorded (a dark plan, no
    # glow, a refused form: the same bytes there) is not run at the others
    if(NOT hasChain)
      continue()
    endif()
    set(skipped FALSE)
    foreach(arm 0 1)
      set(env ${common} ${fake_${s}})
      if(s STREQUAL "poison")
        list(APPEND env "${armOn}")
        if(arm EQUAL 1)
          list(APPEND env ICO_RD_LAMPS_POISON=1)
        endif()
      elseif(arm EQUAL 1)
        list(APPEND env "${armOn}")
      else()
        list(APPEND env "${armOff}")
      endif()
      if("${BACKEND}" STREQUAL "opengl")
        list(APPEND env ICO_RHI_BACKEND=opengl)
      endif()
      set(dir "${OUT}/${index}-${s}-${arm}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND arm EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        break()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT rc EQUAL 0 OR NOT flat MATCHES "${blurRe}")
        message(SEND_ERROR "${dump} ${s} arm ${arm}: exit ${rc}, no glow blur line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        break()
      endif()
      string(REGEX MATCH "${blurRe}" m "${flat}")
      set(word_${arm} "${CMAKE_MATCH_1}")
      set(chain_${arm} "${CMAKE_MATCH_2}")
      set(pairs_${arm} "${CMAKE_MATCH_3}")
      set(passes_${arm} -1)
      set(folds_${arm} 0)
      if(flat MATCHES "${drawsRe}")
        set(passes_${arm} "${CMAKE_MATCH_2}")
      endif()
      if(flat MATCHES "${foldsRe}")
        set(folds_${arm} "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    if(skipped)
      continue()
    endif()
    set(d0 "${OUT}/${index}-${s}-0")
    set(d1 "${OUT}/${index}-${s}-1")
    # the words and the counts
    set(wantWord0 off)
    if(s STREQUAL "poison")
      set(wantWord0 on)
    endif()
    if(NOT word_0 STREQUAL wantWord0 OR NOT word_1 STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: glow blur ${word_0} and ${word_1}: ${wantWord0} and on "
                         "expected")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT s STREQUAL "poison" AND NOT chain_0 EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: ${chain_0} chains in two passes with glowblur off")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "poison" AND NOT chain_0 EQUAL chain_1)
      message(SEND_ERROR "${dump} poison: ${chain_0} chains in two passes, ${chain_1} poisoned")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR chains_${s} "${chains_${s}} + ${chain_1}")
    list(GET settings 0 firstSetting)
    if(s STREQUAL firstSetting AND chain_1 EQUAL 0)
      set(hasChain FALSE)
      message(STATUS "${dump}: no chain in two passes at ${s} (drawn as recorded): the other "
                     "settings not run")
    endif()
    file(GLOB pngs RELATIVE "${d0}" "${d0}/*.png")
    file(GLOB pngs1 RELATIVE "${d1}" "${d1}/*.png")
    list(LENGTH pngs n0)
    list(LENGTH pngs1 n1)
    if(n0 EQUAL 0 OR NOT n0 EQUAL n1)
      message(SEND_ERROR "${dump} ${s}: ${n0} PNGs in one arm, ${n1} in the other")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(exact FALSE)
    if(s STREQUAL "poison" OR chain_1 EQUAL 0)
      set(exact TRUE)
    endif()
    if(NOT exact)
      # the passes: the chain as today draws it against the two
      math(EXPR wantPasses "${passes_0} - (2 * ${pairs_1} - (${folds_0} - ${folds_1})) + 2")
      message(STATUS "${dump} ${s}: ${pairs_1} pairs in two passes; passes ${passes_0} today, "
                     "${passes_1} with glowblur (${wantPasses} expected); pairs folded ${folds_0} "
                     "and ${folds_1}")
      if(NOT passes_1 EQUAL wantPasses)
        message(SEND_ERROR "${dump} ${s}: ${passes_1} passes with glowblur, ${wantPasses} "
                           "expected (today ${passes_0}, ${pairs_1} pairs, pairs folded "
                           "${folds_0} and ${folds_1})")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    foreach(png IN LISTS pngs)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${d0}/${png}" "${d1}/${png}"
                      RESULT_VARIABLE same)
      if(exact OR png MATCHES "^(SHADOW[0-2]|WORK2_PAD)\\.png$")
        if(NOT same EQUAL 0)
          message(SEND_ERROR "${dump} ${s}: ${png} differs: it must be the same bytes")
          math(EXPR failures "${failures} + 1")
        endif()
        continue()
      endif()
      if(NOT png MATCHES "^(WORK3|WORK0|DISPLAY|PRESENT)\\.png$")
        continue()
      endif()
      string(REGEX REPLACE "\\.png$" "" t "${png}")
      set(bound ${w3Max})
      if(t STREQUAL "DISPLAY" OR t STREQUAL "PRESENT")
        set(bound ${dispMax})
      endif()
      level_diff(ld "${d0}/${png}" "${d1}/${png}" RGB)
      if(NOT ld_ok)
        message(SEND_ERROR "${dump} ${s}: ${png}: png_level_diff failed")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR pct10 "${ld_n} * 1000 / ${ld_t}")
      math(EXPR pctW "${pct10} / 10")
      math(EXPR pctT "${pct10} % 10")
      message(STATUS "glow blur measure: ${s} ${dump} ${png}: ${ld_n} pixels apart "
                     "(${pctW}.${pctT} %), largest ${ld_max} levels (the bound ${bound})")
      if(ld_max GREATER bound)
        message(SEND_ERROR "${dump} ${s}: ${png} ${ld_max} levels from today's: the bound is "
                           "${bound} (DIVERGENCES.md VD-33)")
        math(EXPR failures "${failures} + 1")
      endif()
      if(ld_max GREATER max_${s}_${t})
        set(max_${s}_${t} "${ld_max}")
        set(where_${s}_${t} "${dump}")
      endif()
    endforeach()
    # the shimmer: WORK3's change under half a SCENE texel's shift
    list(FIND shimSettings "${s}" isShim)
    if(isShim GREATER -1 AND NOT exact)
      set(shimText "")
      foreach(arm 0 1)
        set(jdir "${OUT}/${index}-${s}-${arm}-j")
        file(REMOVE_RECURSE "${jdir}")
        file(MAKE_DIRECTORY "${jdir}")
        set(jenv ${common} ${fake_${s}} "${armOff}")
        if(arm EQUAL 1)
          set(jenv ${common} ${fake_${s}} "${armOn}")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${jenv} "${TOOL}" "${dump}" -
                                --pass-dump "${jdir}" ${args_${s}} --jitter 0.5,0.5
                        RESULT_VARIABLE jrc OUTPUT_QUIET ERROR_QUIET)
        set(jMax_${arm} -1)
        if(jrc EQUAL 0)
          level_diff(jd "${OUT}/${index}-${s}-${arm}/WORK3.png" "${jdir}/WORK3.png" RGB)
          if(jd_ok)
            set(jMax_${arm} "${jd_max}")
            string(APPEND shimText " arm ${arm}: ${jd_n} texels change, largest ${jd_max};")
          endif()
        endif()
      endforeach()
      message(STATUS "glow blur shimmer: ${s} ${dump} WORK3 under half a SCENE texel's shift "
                     "(arm 0 today, arm 1 glowblur):${shimText}")
      if(jMax_0 LESS 0 OR jMax_1 LESS 0)
        message(SEND_ERROR "${dump} ${s}: the shimmer could not be measured")
        math(EXPR failures "${failures} + 1")
      else()
        math(EXPR shimAllowed "${jMax_0} * ${shimNum} / ${shimDen} + ${shimPlus}")
        if(jMax_1 GREATER shimAllowed)
          message(SEND_ERROR "${dump} ${s}: glowblur's WORK3 moves ${jMax_1} levels under the "
                             "shift, today's ${jMax_0}: at most ${shimNum}/${shimDen} of today's "
                             "+ ${shimPlus} (DIVERGENCES.md VD-33)")
          math(EXPR failures "${failures} + 1")
        endif()
      endif()
    endif()
  endforeach()
endforeach()
foreach(s IN LISTS measured)
  if(chains_${s} EQUAL 0)
    message(SEND_ERROR "rd_glow_blur_ab: ${s}: no chain in two passes over ${ndumps} dumps")
    math(EXPR failures "${failures} + 1")
  endif()
  foreach(t WORK3 WORK0 DISPLAY PRESENT)
    message(STATUS "glow blur maxima: ${s} ${t}: largest ${max_${s}_${t}} levels "
                   "(${where_${s}_${t}}); ${chains_${s}} chains in two passes")
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_glow_blur_ab: ${failures} failures over ${ndumps} dumps")
endif()
message(STATUS "rd_glow_blur_ab: ${ndumps} dumps: the glow's blur in two passes within VD-33's "
               "bounds; every scale, Original 1x included; the poison the same bytes")
