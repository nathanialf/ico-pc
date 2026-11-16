# rd_crack_late_ab: the crack fill drawn after lists 4-6 (port/render/rd_crack_fill.c;
# docs/port/RENDER_POST.md, "Crack filling above 1x"; docs/port/DIVERGENCES.md VD-20)
# on committed fixtures, against the pictures the release before (v0.11.6) drew of
# them.  Each line of the list (port/render/test/fixtures/lists/crack_late.txt:
# "<fixture> <setting>[,<setting>...]" and the line's own properties,
# rd_fixture_list_props.cmake; a listed fixture that is absent or does not expand to
# its pinned SHA-256 FAILS) is expanded into OUT and replayed at each setting with
# rd_replay_tool --pass-dump --stats under ICO_RD_BLEND=fixed, ICO_RD_FADES_FOLD=0,
# ICO_RD_GLOW_LIGHT=0, ICO_RD_CRACK_SWAP=0 and ICO_RD_CULL=off (the environment the
# goldens were made under: the fades fold, the light plan, the swap and the culler
# have their own A/Bs, rd_fades_fold_ab, rd_glow_light_ab, rd_crack_work_ab,
# rd_cull_ab; the registration adds ICO_RD_SHADOW_FINE=0, rd_shadowres.cmake) at:
#   enh2x43  Enhanced 2x at 4:3, presented at 1280x960 (the pass-dump gate's);
#   phone    the PowerVR phone's Resolution Auto (2364x1080, Full height, Full pixel,
#            aspect 2.189, presented at 2364x1080) under its profile's fakes (the local
#            fog, DATE's stencil, the store plan, the blur fold, the fx local read, no UINT
#            views, the VU planes at whole scales only, the shadow count's one draw, the
#            vertex pre-pass off, no dynamic state).
# The run must say "crack fill on: P passes" (P 0 where the fill was the first draw of the
# aura's paste pass, else 1) and "1 fills after lists 4-6", and the path ran: the
# generated geometry decides where the cracks are, so the fixture's declared
# crack_pixels (>=1) is checked on the run (rd_fixture_require).  Every PNG the run
# writes must be the golden's byte for byte (-DGOLDENS, port/render/test/fixtures/
# lists/crack_late_goldens.txt: "<fixture> <setting> <png> <sha256>", the SHA-256 of
# every PNG v0.11.6's rd_replay_tool wrote with the same environment and arguments on
# the expanded fixture); a PNG without a golden, a golden no run wrote and a (fixture,
# setting) without goldens fail.  The bytes are compared only where the run's
# device line matches the list's "# driver: <regex>" line (the lavapipe build and
# CPU the goldens hold for: CI run 43805687280's arm64 llvmpipe, 128 bits, drew
# other bytes); elsewhere the set of PNGs, the fill's line, the path and the
# start-of-list-7 replay's bytes (the same job's) are checked.  These replace the old build's pass dumps outside the
# repository (the fill before the fog, compared within VD-20's measured bound, which
# stays a measurement of the game's frames in DIVERGENCES.md): owner's decision 3, the
# fixture's own picture.  Every run is replayed once more with ICO_RD_CRACK_PLACE=7 (the
# fill at the start of list 7, its place before; rd_crack_fill.c rd__crack_fill_place
# moves it after the glow's sprites of SCENE's alpha alone, or into the aura's paste
# pass): every PNG must be that replay's bytes, and the run's passes no more than its;
# at the phone's setting a run whose stats place the fill later ("1 fills in the aura's
# paste pass" or "1 fills after the glow's alpha") has at least one pass fewer.  A
# fixture no setting replayed fails.  No device: SKIP (77).
# -DMAKE_GOLDENS=<file> (by hand, with TOOL the release before's rd_replay_tool): the
# goldens written to that file instead of checked, nothing else asserted.
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the fixtures
# folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a fixture list>
# -DGOLDENS=<the goldens> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(failures 0)
rd_fixture_list("${LIST}" names)
set(runs "")
foreach(name IN LISTS names)
  rd_fixture_list_props(${name})
  string(REGEX REPLACE "[ \t]+" ";" words "${RDFX_FIELDS_${name}}")
  set(settings "")
  foreach(w IN LISTS words)
    if(w MATCHES "^(enh2x43|phone)(,(enh2x43|phone))*$")
      string(REPLACE "," ";" settings "${w}")
    endif()
  endforeach()
  if(settings STREQUAL "")
    message(FATAL_ERROR "${LIST}: ${name} names no setting (enh2x43, phone)")
  endif()
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  set(name_${d} "${name}")
  foreach(s IN LISTS settings)
    list(APPEND runs "${s}|${d}")
  endforeach()
endforeach()
list(LENGTH runs nruns)
list(LENGTH names nnames)

# the goldens: "<fixture> <setting> <png> <sha256>"
set(goldenKeys "")
set(goldenDriverRe "")
set(goldenOtherDriver 0)
if(NOT MAKE_GOLDENS)
  if("${GOLDENS}" STREQUAL "" OR NOT EXISTS "${GOLDENS}")
    message(FATAL_ERROR "rd_crack_late_ab: no golden list ${GOLDENS}")
  endif()
  file(STRINGS "${GOLDENS}" glines)
  foreach(l IN LISTS glines)
    string(STRIP "${l}" l)
    if(l MATCHES "^# driver: (.+)$")
      set(goldenDriverRe "${CMAKE_MATCH_1}")
      continue()
    endif()
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    if(NOT l MATCHES "^([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+\\.png)[ \t]+([0-9a-f]+)$")
      message(FATAL_ERROR "${GOLDENS}: not '<fixture> <setting> <png> <sha256>': ${l}")
    endif()
    set(golden_${CMAKE_MATCH_1}__${CMAKE_MATCH_2}__${CMAKE_MATCH_3} "${CMAKE_MATCH_4}")
    list(APPEND goldenKeys "${CMAKE_MATCH_1}__${CMAKE_MATCH_2}__${CMAKE_MATCH_3}")
  endforeach()
else()
  file(WRITE "${MAKE_GOLDENS}" "")
endif()
set(goldenUsed "")

set(args_enh2x43 --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(env_enh2x43 "")
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
               --present 2364x1080)
set(env_phone ICO_RD_VU_PREPASS=0 ICO_VK_FAKE_NO_DYNAMIC=1 ICO_RD_FOG_PATH=local
              ICO_RD_DATE_PATH=stencil ICO_RD_STORE_PLAN=1 ICO_RD_BLUR_FOLD=1
              ICO_RD_FX_DST_LOCAL=1 ICO_RD_UINT_VIEW=0 ICO_RD_VU_PLANES=0 ICO_RD_SHADOW_ONE=1)
set(common ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0 ICO_RD_GLOW_LIGHT=0 ICO_RD_CRACK_SWAP=0
           ICO_RD_CULL=off)
# (the outputs' semicolons read as commas: CMake's list separator replaced)
set(lateRe "stats: crack fill on: [01] passes, [0-9]+ pixels filled \\([^)]*, 1 fills after lists 4-6,")
set(placedRe ", 1 fills in the aura's paste pass,|, 1 fills after the glow's alpha\\)")
set(countRe "stats: [0-9]+ draws, ([0-9]+) passes")
set(compared 0)
set(index 0)
foreach(run IN LISTS runs)
  string(REPLACE "|" ";" r "${run}")
  list(GET r 0 s)
  list(GET r 1 dump)
  set(n "${name_${dump}}")
  math(EXPR index "${index} + 1")
  set(dir "${OUT}/${index}-${s}")
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}} "${TOOL}" "${dump}"
                          "${dir}-out.png" --pass-dump "${dir}" --stats ${args_${s}}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(SEND_ERROR "${n} ${s}: exit ${rc}:\n${err}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  set(replayed_${n} TRUE)
  file(GLOB pngs RELATIVE "${dir}" "${dir}/*.png")
  list(SORT pngs)
  if(MAKE_GOLDENS)
    foreach(png IN LISTS pngs)
      file(SHA256 "${dir}/${png}" h)
      file(APPEND "${MAKE_GOLDENS}" "${n} ${s} ${png} ${h}\n")
    endforeach()
    list(LENGTH pngs npng)
    message(STATUS "${n} ${s}: ${npng} goldens written")
    continue()
  endif()
  string(REPLACE ";" "," flat "${out}")
  if(NOT flat MATCHES "${lateRe}")
    message(SEND_ERROR "${n} ${s}: the fill was not drawn once after lists 4-6:\n${out}")
    math(EXPR failures "${failures} + 1")
  endif()
  # the path ran: the fill drew on the fixture's generated geometry
  rd_fixture_require(${n} "${out}" crack_pixels)
  if(flat MATCHES "${lateRe}")
    # the same replay with the fill at the start of list 7: the same bytes
    set(pin "${dir}-pin")
    file(REMOVE_RECURSE "${pin}")
    file(MAKE_DIRECTORY "${pin}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}} ICO_RD_CRACK_PLACE=7
                            "${TOOL}" "${dump}" "${pin}-out.png" --pass-dump "${pin}" --stats
                            ${args_${s}}
                    RESULT_VARIABLE rcPin OUTPUT_VARIABLE outPin ERROR_VARIABLE errPin)
    string(REPLACE ";" "," flatPin "${outPin}")
    if(NOT rcPin EQUAL 0 OR NOT flatPin MATCHES "${countRe}" OR NOT flat MATCHES "${countRe}")
      message(SEND_ERROR "${n} ${s}: the replay with the fill at the start of list 7: exit "
                         "${rcPin}, no pass count:\n${errPin}")
      math(EXPR failures "${failures} + 1")
    else()
      string(REGEX MATCH "${countRe}" m "${flat}")
      set(runPasses "${CMAKE_MATCH_1}")
      string(REGEX MATCH "${countRe}" m "${flatPin}")
      set(pinPasses "${CMAKE_MATCH_1}")
      set(placed FALSE)
      if(flat MATCHES "${placedRe}")
        set(placed TRUE)
      endif()
      file(GLOB pinPngs RELATIVE "${pin}" "${pin}/*.png")
      set(pinDiffer 0)
      foreach(png IN LISTS pinPngs)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${pin}/${png}"
                                "${dir}/${png}" RESULT_VARIABLE samePin)
        if(NOT samePin EQUAL 0)
          math(EXPR pinDiffer "${pinDiffer} + 1")
          message(SEND_ERROR "${n} ${s}: ${png} differs from the replay with the fill at the "
                             "start of list 7")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
      message(STATUS "${n} ${s}: ${runPasses} passes, ${pinPasses} with the fill at the start "
                     "of list 7 (placed later: ${placed}); ${pinDiffer} PNGs differ from it")
      if(runPasses GREATER pinPasses OR
         (placed AND s STREQUAL "phone" AND NOT runPasses LESS pinPasses))
        message(SEND_ERROR "${n} ${s}: ${runPasses} passes against ${pinPasses} with the fill "
                           "at the start of list 7 (placed later: ${placed})")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
  endif()
  # every PNG the release before's picture of the fixture, byte for byte (on the
  # goldens' driver)
  set(bytes TRUE)
  if(NOT goldenDriverRe STREQUAL "" AND NOT err MATCHES "${goldenDriverRe}")
    set(bytes FALSE)
    math(EXPR goldenOtherDriver "${goldenOtherDriver} + 1")
  endif()
  set(nGolden 0)
  foreach(png IN LISTS pngs)
    set(k "${n}__${s}__${png}")
    if(NOT DEFINED golden_${k})
      message(SEND_ERROR "${n} ${s}: ${png} has no golden in ${GOLDENS}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    list(APPEND goldenUsed "${k}")
    math(EXPR compared "${compared} + 1")
    math(EXPR nGolden "${nGolden} + 1")
    if(NOT bytes)
      continue()
    endif()
    file(SHA256 "${dir}/${png}" h)
    if(NOT h STREQUAL "${golden_${k}}")
      message(SEND_ERROR "${n} ${s}: ${png} is not v0.11.6's picture of the fixture (SHA-256 ${h}, "
                         "the golden ${golden_${k}})")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
  if(bytes)
    message(STATUS "${n} ${s}: ${nGolden} PNGs against v0.11.6's")
  else()
    string(REGEX MATCH "rhi_vk: device table: [^\n]*" dev "${err}")
    message(STATUS "${n} ${s}: ${nGolden} PNGs written; their bytes not compared: the device is "
                   "not the goldens' (${goldenDriverRe}): ${dev}")
  endif()
endforeach()
foreach(name IN LISTS names)
  if(NOT replayed_${name})
    message(SEND_ERROR "${name}: no setting replayed it: a fixture this test cannot measure is "
                       "no test")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(MAKE_GOLDENS)
  if(failures GREATER 0)
    message(FATAL_ERROR "rd_crack_late_ab: ${failures} failures making the goldens")
  endif()
  message(STATUS "rd_crack_late_ab: goldens of ${nruns} runs written to ${MAKE_GOLDENS}")
  return()
endif()
# every golden matched: one no run wrote is a drifted list
foreach(k IN LISTS goldenKeys)
  list(FIND goldenUsed "${k}" at)
  if(at LESS 0)
    message(SEND_ERROR "${GOLDENS}: the golden ${k} was written by no run")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_crack_late_ab: ${failures} failures over ${nruns} runs of ${nnames} "
                      "fixtures")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_crack_late_ab: no target compared over ${nruns} runs")
endif()
message(STATUS "rd_crack_late_ab: ${compared} targets v0.11.6's bytes over ${nruns} runs of "
               "${nnames} fixtures, the fill drawn once after lists 4-6 in each")
