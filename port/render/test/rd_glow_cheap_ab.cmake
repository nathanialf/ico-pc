# rd_glow_cheap_ab: the glow's cheaper passes (docs/port/RENDER.md, "The
# glow's passes") against the passes drawn without them, byte for byte, with
# fewer glow passes at the phone's setting.  The two cuts:
#   - the glow's WORK2 fill (staticBlur.c fillWork2's sprite, a clear) is the
#     load op of the mask's pass (rd_fx_plan.c fxDraw and rd_blur_border.c
#     sceneRepeat end the open pass for SCENE's read through
#     rd__replay_end_pass_for, the clear carry), not a pass of its own;
#   - cleanUpFB's two sprites into SCENE stay in the fog's pass (rd_fx_plan.c
#     fxAttachDepth: the constant fold with the open pass's depth attached
#     and unused), not two passes of their own.
# Both are the same bytes by construction, so the bound is 0 levels on every
# target and the present: -DBOUND (default 0) is the largest level
# difference allowed per channel.  No switch turns the cuts alone off; what
# each rides on has one: ICO_RD_CLEAR_CARRY=0 (rd_pass_cut.c: a pending clear
# flushed as a pass of its own before a source's read: the fill's pass back,
# the same bytes) and ICO_RD_FX_PLAN=0 (the post sprites' plan, rd_fx_plan.c:
# cleanUpFB's sprites through the snapshot path, each a pass of its own on
# SCENE; the plan's other sprites change too, and its pictures are not the
# plan's bytes on a frame with the sun's fans: 1 to 3 levels on about 500
# pixels of SCENE on the game's frame and on its fixture alike, so that run
# gives the passes only).  For every fixture of the list
# (port/render/test/fixtures/lists/glow_cheap.txt: committed frames with no
# art, docs/port/TESTING_FIXTURES.md), rd_replay_tool under ICO_RD_BLEND=fixed
# and ICO_RD_FADES_FOLD=0 at three settings: a count run (--stats, both
# switches off: the glow's passes without the cuts), the before run
# (--pass-dump --stats, the clear carry off) and the run at the defaults:
#   phone   the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=moltenvk:
#           the tile class, its clear carry and its local fog) at 2364x1080,
#           Full height, Full pixel, the frame's own aspect, presented at
#           2364x1080, ICO_RD_VU_PREPASS=0;
#   orig    Original 4:3;
#   enh2x43 Enhanced 2x 4:3 presented at 1280x960.
# Every PNG of the before run must be the new one's bytes or within BOUND
# levels of it (port/render/test/png_level_diff.py); the glow's passes ("glow
# N passes") at most the count run's at orig and enh2x43 (the desktop
# profile: the clear carry is off there, and cleanUpFB joins the fog's pass
# only where that pass has the depth attached) and at least 2 fewer at the
# phone's setting (cleanUpFB's two passes; 3 where the fill had a pass of its
# own: a frame without the sun's fans, and at least one such run over the
# list).  The lines print each fixture's passes before and after and its
# largest level.
# The crack fill after lists 4-6 (rd_crack_fill.c, CHEAPPASSES; DIVERGENCES.md
# VD-20) is drawn after cleanUpFB's two sprites (rd__crack_fill_place: they
# write SCENE's alpha alone, so the fill after them writes RGB alone, the
# same bytes), which stay in the fog's pass, and it leaves the crack's depth,
# which cleanUpFB's Z-tested sprite and so the glow's mask (WORK2) read.  So
# at the phone's setting a run whose stats say the fill was drawn ("1 fills
# after lists 4-6") says it was drawn after the glow's alpha ("1 fills after
# the glow's alpha").
# The glow's mask folded into its reduction (rd_glow_fold.c, [dev] glow_fold;
# docs/port/RENDER_POST.md, "The glow's mask in its reduction").  The tool's
# --pass-dump reads every target back, so the fold is off there unless asked
# for: the runs above are made with ICO_RD_GLOW_FOLD=0 (the chain as drawn),
# and each fixture at each setting is run again with ICO_RD_GLOW_FOLD=1 into
# <name>-<setting>-fold:
#   - every PNG of the run without the fold (PRESENT.png too where the
#     setting presents) but WORK2.png the same bytes with it (WORK2 is what
#     the fold leaves undrawn: not compared);
#   - its stats' "glow: WORK2 fold on: N folded, M kept (why)": a folded run
#     has at least one glow pass fewer than the run without it (with the
#     sun's fans the mask alone is folded and WORK2 read where it keeps a
#     texel), a kept one as many, and only for the lamps' reason (their
#     meshes draw into WORK2 after the mask): any other reason fails;
#   - at least one run folded at each setting.
# Every run sets ICO_RD_GLOW_LIGHT=0 (the chain drawn whole, as the cuts and
# the WORK2 fold need it): rd_glow_light_ab holds the light plan
# (rd_glow_light.c, VD-30) against the chain drawn.
# The path ran: the run at the defaults shows the glow's passes the fixture
# declares (glow_passes) and the fold's run the folds (glow_fold_folded), at
# every setting (rd_fixture_require).  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures' folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<the
# list> -DPY=<python3> -DDIFF=<png_level_diff.py> -DOUT=<a folder the test
# may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
if("${BOUND}" STREQUAL "")
  set(BOUND 0)
endif()
if("${PY}" STREQUAL "" OR "${DIFF}" STREQUAL "")
  message(FATAL_ERROR "rd_glow_cheap_ab: -DPY and -DDIFF are needed")
endif()
rd_fixture_list("${LIST}" names)
list(LENGTH names ndumps)

set(settings phone orig enh2x43)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0)
set(args_orig --aspect 4:3)
set(env_orig "")
set(args_enh2x43 --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x43 "")
set(glowRe "glow ([0-9]+) passes")
set(lateRe "stats: crack fill on: [01] passes, [0-9]+ pixels filled \\([^)]*, 1 fills after lists 4-6,")
set(alphaRe ", 1 fills after the glow's alpha\\)")
set(foldRe "glow: WORK2 fold on: ([0-9]+) folded \\(([0-9]+) reading the sun's fans' WORK2\\), ([0-9]+) kept")
set(diffRe "png_level_diff: [0-9]+ x [0-9]+, [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, [0-9]+ more \\(largest ([0-9]+)\\), ([0-9]+) pixels differ")
set(before ICO_RD_CLEAR_CARRY=0)
set(countRun ICO_RD_FX_PLAN=0 ICO_RD_CLEAR_CARRY=0)
set(failures 0)
set(compared 0)
set(runs 0)
set(fewer 0)
set(fewer3 0)
set(folded 0)
set(foldedIn "")
set(foldRan "")
set(foldedRead 0)
set(kept 0)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)
  rd_fixture_list_props(${name})
  foreach(s IN LISTS settings)
    # the count run: the glow's passes without the cuts
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0 ICO_RD_GLOW_LIGHT=0
                            ICO_RD_GLOW_FOLD=0 ${countRun}
                            ${env_${s}} "${TOOL}" "${dump}" "${OUT}/${name}-${s}-count.png" --stats
                            ${args_${s}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE countText ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    string(REPLACE ";" "," countText "${countText}")
    if(NOT rc EQUAL 0 OR NOT countText MATCHES "${glowRe}")
      message(SEND_ERROR "${name} ${s} count run: exit ${rc}, or no glow pass count:\n${err}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(glowBase "${CMAKE_MATCH_1}")
    # the before run: the fill's pass back, the same bytes
    set(baseDir "${OUT}/${name}-${s}-before")
    file(REMOVE_RECURSE "${baseDir}")
    file(MAKE_DIRECTORY "${baseDir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0 ICO_RD_GLOW_LIGHT=0
                            ICO_RD_GLOW_FOLD=0 ${before}
                            ${env_${s}} "${TOOL}" "${dump}" "${baseDir}/out.png" --pass-dump
                            "${baseDir}" --stats ${args_${s}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE baseText ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(SEND_ERROR "${name} ${s} before: exit ${rc}:\n${err}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(dirNew "${OUT}/${name}-${s}")
    file(REMOVE_RECURSE "${dirNew}")
    file(MAKE_DIRECTORY "${dirNew}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0 ICO_RD_GLOW_LIGHT=0
                            ICO_RD_GLOW_FOLD=0
                            ${env_${s}} "${TOOL}" "${dump}" "${dirNew}/out.png" --pass-dump
                            "${dirNew}" --stats ${args_${s}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
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
    # the path ran: the glow drawn
    rd_fixture_require(${name} "${out}" glow_passes)
    # the passes the cuts are judged by: this run's, the crack fill drawn
    # (rd_crack_fill.c rd__crack_fill_place: after cleanUpFB's two sprites,
    # which stay in the fog's pass, its RGB alone)
    set(glowCut "${glowNew}")
    set(cutWhat "now")
    if(flat MATCHES "${lateRe}")
      set(cutWhat "with the crack fill")
      if(s STREQUAL "phone" AND NOT flat MATCHES "${alphaRe}")
        message(SEND_ERROR "${name} ${s}: the crack fill was drawn but not after the glow's "
                           "alpha (cleanUpFB):\n${out}")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    if(s STREQUAL "phone")
      math(EXPR most "${glowBase} - 2")
      if(glowCut GREATER most)
        message(SEND_ERROR "${name} ${s}: glow ${glowBase} passes before, ${glowCut} ${cutWhat}: "
                           "want at most ${most} (cleanUpFB in the fog's pass, the WORK2 fill in "
                           "the mask's)")
        math(EXPR failures "${failures} + 1")
      endif()
    elseif(glowCut GREATER glowBase)
      message(SEND_ERROR "${name} ${s}: glow ${glowBase} passes before, ${glowCut} ${cutWhat}: "
                         "want at most as many")
      math(EXPR failures "${failures} + 1")
    endif()
    if(glowCut LESS glowBase)
      math(EXPR fewer "${fewer} + 1")
    endif()
    math(EXPR three "${glowBase} - 3")
    if(s STREQUAL "phone" AND NOT glowCut GREATER three)
      math(EXPR fewer3 "${fewer3} + 1")
    endif()
    # the pictures
    file(GLOB pngs RELATIVE "${baseDir}" "${baseDir}/*.png")
    set(largest 0)
    set(differ 0)
    set(n 0)
    foreach(png IN LISTS pngs)
      if(png STREQUAL "out.png")
        continue()
      endif()
      if(NOT EXISTS "${dirNew}/${png}")
        message(SEND_ERROR "${name} ${s}: ${png} in the before run, not written now")
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
    message(STATUS "${name} ${s}: glow ${glowBase} -> ${glowNew} passes (${glowCut} ${cutWhat}); "
                   "${n} targets against ${baseDir}, largest difference ${largest} levels, "
                   "${differ} pixels differ")

    # the same with the glow's mask folded into its reduction
    set(dirFold "${OUT}/${name}-${s}-fold")
    file(REMOVE_RECURSE "${dirFold}")
    file(MAKE_DIRECTORY "${dirFold}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0 ICO_RD_GLOW_LIGHT=0
                            ICO_RD_GLOW_FOLD=1
                            ${env_${s}} "${TOOL}" "${dump}" "${dirFold}/out.png" --pass-dump
                            "${dirFold}" --stats ${args_${s}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE fout ERROR_VARIABLE ferr)
    if(NOT rc EQUAL 0)
      message(SEND_ERROR "${name} ${s} fold: exit ${rc}:\n${ferr}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    string(REPLACE ";" "," fflat "${fout}")
    if(NOT fflat MATCHES "${glowRe}")
      message(SEND_ERROR "${name} ${s} fold: no glow pass count in the stats:\n${fout}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(glowFold "${CMAKE_MATCH_1}")
    if(NOT fflat MATCHES "${foldRe}")
      message(SEND_ERROR "${name} ${s} fold: no glow fold line in the stats:\n${fout}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(nFolded "${CMAKE_MATCH_1}")
    set(nRead "${CMAKE_MATCH_2}")
    set(nKept "${CMAKE_MATCH_3}")
    # the path ran: the mask folded
    rd_fixture_require(${name} "${fout}" glow_fold_folded)
    list(APPEND foldRan "${s}")
    set(foldWhy "")
    if(fflat MATCHES "WORK2\\), [0-9]+ kept \\(([^)]*)\\)")
      set(foldWhy " (${CMAKE_MATCH_1})")
    endif()
    if(nFolded GREATER 0)
      math(EXPR folded "${folded} + 1")
      if(nRead GREATER 0)
        math(EXPR foldedRead "${foldedRead} + 1")
      endif()
      list(APPEND foldedIn "${s}")
      math(EXPR most "${glowNew} - 1")
      if(glowFold GREATER most)
        message(SEND_ERROR "${name} ${s} fold: glow ${glowNew} passes drawn, ${glowFold} folded: "
                           "want at most ${most}")
        math(EXPR failures "${failures} + 1")
      endif()
    else()
      math(EXPR kept "${kept} + 1")
      # the chain is kept only where the lamps draw into WORK2 after the mask
      # (the issue 163 rooms); any other reason is a refusal the fold was not
      # meant to have
      if(NOT foldWhy MATCHES "the lamps")
        message(SEND_ERROR "${name} ${s} fold: kept${foldWhy}: only the lamps' frames keep the "
                           "chain")
        math(EXPR failures "${failures} + 1")
      endif()
      if(NOT glowFold EQUAL glowNew)
        message(SEND_ERROR "${name} ${s} fold: kept${foldWhy}, but glow ${glowFold} passes "
                           "against ${glowNew} drawn")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    file(GLOB fpngs RELATIVE "${dirNew}" "${dirNew}/*.png")
    set(fn 0)
    set(fdiffer 0)
    foreach(png IN LISTS fpngs)
      if(png STREQUAL "out.png" OR png STREQUAL "WORK2.png")
        continue() # WORK2: the fold leaves it undrawn
      endif()
      if(NOT EXISTS "${dirFold}/${png}")
        message(SEND_ERROR "${name} ${s} fold: ${png} not written")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR fn "${fn} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirNew}/${png}"
                              "${dirFold}/${png}" RESULT_VARIABLE same)
      if(same EQUAL 0)
        continue()
      endif()
      math(EXPR fdiffer "${fdiffer} + 1")
      execute_process(COMMAND "${PY}" -I "${DIFF}" "${dirNew}/${png}" "${dirFold}/${png}"
                      OUTPUT_VARIABLE dout ERROR_VARIABLE derr)
      message(SEND_ERROR "${name} ${s} fold: ${png} differs from the chain drawn: ${dout}${derr}")
      math(EXPR failures "${failures} + 1")
    endforeach()
    message(STATUS "${name} ${s} fold: ${nFolded} folded (${nRead} reading the fans' WORK2), ${nKept} kept${foldWhy}; glow "
                   "${glowNew} -> ${glowFold} passes; ${fn} targets but WORK2 compared, "
                   "${fdiffer} differ")
  endforeach()
endforeach()
if(compared EQUAL 0 OR runs EQUAL 0)
  message(SEND_ERROR "rd_glow_cheap_ab: nothing was compared")
  math(EXPR failures "${failures} + 1")
endif()
if(fewer3 EQUAL 0)
  message(SEND_ERROR "rd_glow_cheap_ab: no run at the phone's setting with three glow passes "
                     "fewer (the WORK2 fill in the mask's pass: a frame without the sun's fans)")
  math(EXPR failures "${failures} + 1")
endif()
# list(FIND), not if(IN_LIST): a cmake -P script runs under the old policies
# (CMP0057), where IN_LIST is not an operator
foreach(s IN LISTS settings)
  list(FIND foldedIn "${s}" did)
  if(did EQUAL -1)
    message(SEND_ERROR "rd_glow_cheap_ab: no run folded the glow's mask at ${s}")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_glow_cheap_ab: ${failures} failures over ${ndumps} fixtures")
endif()
message(STATUS "rd_glow_cheap_ab: ${compared} targets within ${BOUND} levels in ${runs} runs over "
               "${ndumps} fixtures, ${fewer} runs with fewer glow passes (${fewer3} at the phone's "
               "setting with three fewer); the glow fold: ${folded} "
               "runs folded (${foldedRead} reading the sun's fans' WORK2), ${kept} kept, every target but "
               "WORK2 the same bytes")
