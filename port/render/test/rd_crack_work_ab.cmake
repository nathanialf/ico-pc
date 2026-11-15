# rd_crack_work_ab: the crack fill's swap and early leave (port/render/rd_crack_fill.c
# FILLSWAP and CRACKTAP; docs/port/RENDER_POST.md, "The crack fill's snapshot by
# the swap"): both off (ICO_RD_CRACK_SWAP=0 ICO_RD_CRACK_TAP2=0) against both on
# (=1).  For every fixture the list names (port/render/test/fixtures/lists/
# crack_work.txt: fixture names of the repository, docs/port/TESTING_FIXTURES.md;
# a listed fixture that is absent or does not expand to its pinned SHA-256
# FAILS; after each name swapN and tapN, one replay's swapped snapshots and
# early-leave draws at the phone's setting, re-derived from a run of the
# fixture, and the line's own properties, rd_fixture_list_props.cmake),
# expanded into OUT, rd_replay_tool's --pass-dump with --stats and
# ICO_RD_VU_PREPASS=0, at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced, 2364x1080, Full height,
#           Full pixel, presented at 2364x1080; scale about 2.8 x 2.4, reach 2x2)
#           under the tile class's fakes (ICO_VK_FAKE_LIMITS=moltenvk) and the
#           late pass the phone takes by default (ICO_RD_LATE_PASS=1: the fake
#           leaves RhiLimits.tiler false on lavapipe, so the profile alone
#           would give list 11's join only and no hold): the fill in the
#           aura's paste pass, or held by the late pass;
#   desk21  a desktop window of 1344x1008 at 4:3 (scale 2.1 for a frame 640 GS
#           pixels wide, 2.625 for 512; reach 2x2 or 2x2), lavapipe's own profile
#           (the fill in a pass of its own);
#   desk3x  Enhanced 3x at 4:3 (reach 2x2), the same profile;
#   desk4x  Enhanced 4x at 4:3 (reach 2x2), the same profile;
#   gl      -DBACKEND=opengl (rd_crack_work_gl): the phone's setting on OpenGL
#           (llvmpipe, no fakes: the fill in a pass of its own);
# a fixture drawn narrower than a setting's aspect cannot show it and is
# skipped there.  Each pair: every PNG the off run writes (SCENE, DISPLAY,
# PRESENT and every other named target) the on run's byte for byte; the same
# draws and passes; the off run's stats say the swap off with nothing swapped
# and the early leave off with no draw; the on run's swap self-test passed where
# the fill drew (the off run's too: it runs either way); its colour MB copied and
# loaded each fewer than the off run's by the MB its stats name for the swapped
# snapshots (within 0.02, two decimals' rounding), stored the same, depth and
# stencil the same; the swapped snapshots and the early-leave draws at most the
# off run's copied snapshots and fill draws; the list's swapN and tapN at the
# phone's setting.  The path ran: the generated geometry decides where the
# cracks are, so on the on run of every pair the fixture's declared crack_pixels
# (>=1: the fill drew at that setting) is checked (rd_fixture_require), every
# fixture swaps or leaves early at least once over its settings (a held fill
# keeps its copy at the phone's setting and swaps at the desktop's), and a
# fixture the phone's setting (OpenGL's) did not replay fails.  At least one snapshot swapped and one early-leave draw over the list.
# No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write> -DBACKEND=vulkan|opengl
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(failures 0)
rd_fixture_list("${LIST}" names)
set(dumps "")
foreach(name IN LISTS names)
  rd_fixture_list_props(${name})
  set(wantSwap "")
  set(wantTap "")
  string(REGEX REPLACE "[ \t]+" ";" words "${RDFX_FIELDS_${name}}")
  foreach(w IN LISTS words)
    if(w MATCHES "^swap([0-9]+)$")
      set(wantSwap "${CMAKE_MATCH_1}")
    elseif(w MATCHES "^tap([0-9]+)$")
      set(wantTap "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(wantSwap STREQUAL "" OR wantTap STREQUAL "")
    message(FATAL_ERROR "${LIST}: ${name} without swapN and tapN (the phone's setting's swapped "
                        "snapshots and early-leave draws, re-derived from a run of the fixture)")
  endif()
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
  set(wantSwap_${d} "${wantSwap}")
  set(wantTap_${d} "${wantTap}")
endforeach()
list(LENGTH dumps ndumps)

set(common ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1)
set(args_desk21 --enhanced --resolution 1344x1008 --aspect 1.3333 --present 1344x1008)
set(env_desk21 "")
set(args_desk3x --enhanced --resolution 3x --aspect 1.3333 --present 1920x1440)
set(env_desk3x "")
set(args_desk4x --enhanced --resolution 4x --aspect 1.3333 --present 2560x1920)
set(env_desk4x "")
set(args_gl ${args_phone})
set(env_gl "")
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
else()
  set(settings phone desk21 desk3x desk4x)
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(crackRe "stats: crack fill (on|off): ([0-9]+) passes, ([0-9]+) pixels filled")
set(swapRe "stats: crack fill swap (on|off) \\(self-test (passed|failed|not run)\\): ([0-9]+) snapshots by the swap \\(([0-9.]+) MB of colour not copied and as many not loaded\\), ([0-9]+) copied")
set(tapRe "early leave (on|off): ([0-9]+) draws")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored, depth and stencil ([0-9.]+)/([0-9.]+) MB loaded/stored, [0-9.]+ MB cleared, ([0-9.]+) MB copied")

# v in hundredths of a MB (the stats print two decimals)
function(mb_hundredths out v)
  string(REGEX REPLACE "^([0-9]+)\\.([0-9][0-9])$" "\\1\\2" h "${v}")
  math(EXPR h "${h}")
  set(${out} "${h}" PARENT_SCOPE)
endfunction()

set(compared 0)
set(swapTotal 0)
set(tapTotal 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(w 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${w}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}}
                              ICO_RD_CRACK_SWAP=${w} ICO_RD_CRACK_TAP2=${w}
                              "${TOOL}" "${dump}" "${dir}-out.png" --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND w EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} swap and early leave ${w}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      set(out_${w} "${out}")
      string(REPLACE ";" "," flat "${out}")
      set(ok TRUE)
      foreach(re drawsRe crackRe swapRe tapRe trafficRe)
        if(NOT flat MATCHES "${${re}}")
          message(SEND_ERROR "${dump} ${s} swap and early leave ${w}: no ${re} line:\n${out}")
          math(EXPR failures "${failures} + 1")
          set(ok FALSE)
        endif()
      endforeach()
      if(NOT ok)
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${w} "${CMAKE_MATCH_1}")
      set(passes_${w} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${crackRe}" m "${flat}")
      set(fillOn_${w} "${CMAKE_MATCH_1}")
      set(fillPasses_${w} "${CMAKE_MATCH_2}")
      set(pixels_${w} "${CMAKE_MATCH_3}")
      string(REGEX MATCH "${swapRe}" m "${flat}")
      set(swapWord_${w} "${CMAKE_MATCH_1}")
      set(test_${w} "${CMAKE_MATCH_2}")
      set(swaps_${w} "${CMAKE_MATCH_3}")
      set(swapMb_${w} "${CMAKE_MATCH_4}")
      set(copied_${w} "${CMAKE_MATCH_5}")
      string(REGEX MATCH "${tapRe}" m "${flat}")
      set(tapWord_${w} "${CMAKE_MATCH_1}")
      set(taps_${w} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(cl_${w} "${CMAKE_MATCH_1}")
      set(cs_${w} "${CMAKE_MATCH_2}")
      set(dl_${w} "${CMAKE_MATCH_3}")
      set(ds_${w} "${CMAKE_MATCH_4}")
      set(cp_${w} "${CMAKE_MATCH_5}")
      message(STATUS "${dump} ${s} swap and early leave ${w}: ${passes_${w}} passes, "
                     "${draws_${w}} draws, ${fillPasses_${w}} crack fill passes, "
                     "${pixels_${w}} pixels filled; swap ${swapWord_${w}} (self-test "
                     "${test_${w}}): ${swaps_${w}} swapped (${swapMb_${w}} MB), "
                     "${copied_${w}} copied; early leave ${tapWord_${w}}: ${taps_${w}} draws; "
                     "colour ${cl_${w}}/${cs_${w}} MB loaded/stored, depth and stencil "
                     "${dl_${w}}/${ds_${w}} MB, ${cp_${w}} MB copied")
    endforeach()
    if(skipped)
      continue()
    endif()
    # the pair: the off run swaps and leaves early nothing
    if(NOT swapWord_0 STREQUAL "off" OR NOT swaps_0 EQUAL 0 OR NOT tapWord_0 STREQUAL "off" OR
       NOT taps_0 EQUAL 0 OR NOT tapWord_1 STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: the swap says ${swapWord_0} (${swaps_0}) and the early "
                         "leave ${tapWord_0} (${taps_0}) off, ${swapWord_1} and ${tapWord_1} on")
      math(EXPR failures "${failures} + 1")
    endif()
    # where the fill drew, the self-test ran and passed in both runs, and the
    # on run says the swap on
    math(EXPR fills0 "${swaps_0} + ${copied_0}")
    if(fills0 GREATER 0 AND (NOT test_0 STREQUAL "passed" OR NOT test_1 STREQUAL "passed" OR
                             NOT swapWord_1 STREQUAL "on"))
      message(SEND_ERROR "${dump} ${s}: the swap's self-test ${test_0} and ${test_1}, the swap "
                         "${swapWord_1} on: passed and on expected (lavapipe and llvmpipe keep "
                         "UNORM8 exactly)")
      math(EXPR failures "${failures} + 1")
    endif()
    # the same draws, passes and fill; as many snapshots in all
    math(EXPR fills1 "${swaps_1} + ${copied_1}")
    if(NOT draws_1 EQUAL draws_0 OR NOT passes_1 EQUAL passes_0 OR
       NOT fillPasses_1 EQUAL fillPasses_0 OR NOT pixels_1 EQUAL pixels_0 OR
       NOT fills1 EQUAL fills0)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws, ${passes_1} passes, ${fillPasses_1} fill "
                         "passes, ${pixels_1} pixels, ${fills1} snapshots on; ${draws_0}, "
                         "${passes_0}, ${fillPasses_0}, ${pixels_0}, ${fills0} off")
      math(EXPR failures "${failures} + 1")
    endif()
    # the early leave at most once a fill
    if(taps_1 GREATER fills1)
      message(SEND_ERROR "${dump} ${s}: ${taps_1} early-leave draws for ${fills1} fills")
      math(EXPR failures "${failures} + 1")
    endif()
    # the bytes: copies and colour loads fewer by the swapped snapshots' MB
    mb_hundredths(cl0 "${cl_0}")
    mb_hundredths(cl1 "${cl_1}")
    mb_hundredths(cp0 "${cp_0}")
    mb_hundredths(cp1 "${cp_1}")
    mb_hundredths(fall "${swapMb_1}")
    math(EXPR dL "${cl0} - ${cl1} - ${fall}")
    math(EXPR dC "${cp0} - ${cp1} - ${fall}")
    if(dL LESS -2 OR dL GREATER 2 OR dC LESS -2 OR dC GREATER 2 OR NOT cs_1 STREQUAL cs_0 OR
       NOT dl_1 STREQUAL dl_0 OR NOT ds_1 STREQUAL ds_0)
      message(SEND_ERROR "${dump} ${s}: colour ${cl_0}/${cs_0} -> ${cl_1}/${cs_1} MB loaded/stored, "
                         "copies ${cp_0} -> ${cp_1} MB, depth and stencil ${dl_0}/${ds_0} -> "
                         "${dl_1}/${ds_1}: the swapped snapshots name ${swapMb_1} MB fewer copied "
                         "and loaded, the rest the same")
      math(EXPR failures "${failures} + 1")
    endif()
    # the path ran: the fill drew at this setting on the fixture's generated
    # geometry (its declared crack_pixels)
    set(n "${name_${dump}}")
    rd_fixture_require(${n} "${out_1}" crack_pixels)
    if(s STREQUAL "phone" OR s STREQUAL "gl")
      set(required_${dump} TRUE)
    endif()
    math(EXPR work "${swaps_1} + ${taps_1}")
    if(work GREATER 0)
      set(worked_${dump} TRUE)
    endif()
    if(s STREQUAL "phone")
      foreach(k Swap Tap)
        set(want "${want${k}_${dump}}")
        if(k STREQUAL "Swap")
          set(got "${swaps_1}")
        else()
          set(got "${taps_1}")
        endif()
        if(NOT got EQUAL want)
          message(SEND_ERROR "${n} ${s}: ${got} ${k} on, ${want} expected (the list's field)")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
    endif()
    math(EXPR swapTotal "${swapTotal} + ${swaps_1}")
    math(EXPR tapTotal "${tapTotal} + ${taps_1}")
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written off, ${npngOn} on")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the swap and the early leave on: "
                           "it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
foreach(dump IN LISTS dumps)
  if(NOT required_${dump})
    message(SEND_ERROR "${name_${dump}}: never replayed at the phone's setting (OpenGL: its one "
                       "setting): a fixture this test cannot measure is no test")
    math(EXPR failures "${failures} + 1")
  elseif(NOT worked_${dump})
    message(SEND_ERROR "${name_${dump}}: no snapshot swapped and no early-leave draw at any "
                       "setting: the swap and the early leave did not run on this fixture")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_crack_work_ab: ${failures} failures over ${ndumps} fixtures")
endif()
if(compared EQUAL 0 OR swapTotal EQUAL 0 OR tapTotal EQUAL 0)
  message(FATAL_ERROR "rd_crack_work_ab: ${compared} targets compared, ${swapTotal} snapshots "
                      "swapped, ${tapTotal} early-leave draws over ${ndumps} fixtures: one of each "
                      "at least expected")
endif()
message(STATUS "rd_crack_work_ab: ${compared} targets the same bytes over ${ndumps} fixtures; "
               "${swapTotal} snapshots swapped, ${tapTotal} early-leave draws")
