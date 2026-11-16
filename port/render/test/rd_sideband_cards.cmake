# rd_sideband_cards: issue 189 (lighter bars beside the 4:3 picture near
# stage 32's exit): the region rule's side bands leave a card dropped
# (vu_common.hlsli vu_rule_beside, docs/port/VU1_PROGRAMS.md "The region
# rule", step 7).  On the reporter's frame the only triangles the rule drops
# were three of the mist quads' (mesh wf_smoke1), cards, which the side bands
# drew beside the picture only before the fix: the sides 7 levels lighter,
# with an edge at the picture's.  The test replays the committed fixtures of
# its list (port/render/test/fixtures/lists/sideband_cards.txt): fx-cards-189,
# that frame made with @scene cards (docs/port/TESTING_FIXTURES.md, "The
# placement rules"), whose two cards (a band across the picture's middle
# with every corner beyond the GS window, one facing the eye, one away) are
# its only drops.  After a name the list gives the counts a run of the
# fixture shows (generated geometry: re-derived from a run, the rule from
# the code): ps2drops=N (every drop a card's under the PS2's rule),
# cameradrawn=D (the facing card's triangles the camera rule draws) and
# camerakept=K (the card facing away keeps its drop), D + K = N.
# At four settings, the Original preset at 4:3 1x, Enhanced 4:3 2x, Enhanced
# 16:10 1x and the reporter's own (Enhanced 16:10 at 1920x1200, full height,
# Full pixel).  The frame was recorded at 16:10, so it is 624 GS pixels wide
# at every setting (at 4:3 cropped to the picture's 512 at the present): the
# side bands are on in all four.  At each, rd_replay_tool --pass-dump twice:
#   A with ICO_RD_VU_BESIDE=0 (every VU draw keeps the PS2's drop) and B with
#   the side bands, every target and the present byte for byte the same
#   (the frame's drops are all cards, so the side bands add nothing);
#   B's --stats line "side bands: N draws with cards": N at least 1;
#   --region-stats (the CPU oracle): the frame's line with every drop a
#   card's ("drop N (behind-eye 0, out-of-box 0, card N") and nothing
#   beside ("beside 0 (behind-eye 0, out-of-box 0, card 0)");
# and on the 16:10 presents, with -DPYTHON, tools/band_seam.py on B's
# PRESENT.png: both edges of the picture within 1.5 levels.
# All of the above under the PS2's region rule (ICO_RD_REGION_RULE=ps2),
# whose side bands these are.  Then the camera rule (the default, [dev]
# region_rule = camera) at each setting: the facing card's triangles drawn
# over the whole frame (--region-stats: "drop K (behind-eye 0, out-of-box 0,
# card K" and "drawn by the camera rule D (behind-eye 0, out-of-box 0, card
# D)"; --stats: "region rule: the camera's: N VU draws" with N at least 1),
# and on the 16:10 presents band_seam.py within 1.5 levels.
# A fixture no setting checked fails; no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the fixtures folder>
# -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<the fixture list>
# -DOUT=<a folder the test may write> -DSEAM=<tools/band_seam.py> [-DPYTHON=<python3>]
include("${FIXTURE_HELPERS}")
file(REMOVE_RECURSE "${OUT}")
rd_fixture_list("${LIST}" names)

set(settings o43 e43x2 e1610 rep)
set(args_o43 --aspect 4:3 --present 640x480)
set(args_e43x2 --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(args_e1610 --enhanced --aspect 16:10 --present 1920x1200)
set(args_rep --enhanced --aspect 16:10 --resolution 1920x1200 --full-height --full-pixel
             --present 1920x1200)
# a present wider than 4:3, whose picture's edges band_seam.py measures
set(wide_o43 0)
set(wide_e43x2 0)
set(wide_e1610 1)
set(wide_rep 1)

set(failures 0)
foreach(n IN LISTS names)
  foreach(k ps2drops cameradrawn camerakept)
    if(NOT RDFX_FIELDS_${n} MATCHES "(^|[ \t])${k}=([0-9]+)")
      message(FATAL_ERROR "${LIST}: ${n}: no ${k}=N after the name")
    endif()
    set(${k} "${CMAKE_MATCH_2}")
  endforeach()
  math(EXPR sum "${cameradrawn} + ${camerakept}")
  if(NOT sum EQUAL ps2drops OR cameradrawn EQUAL 0 OR camerakept EQUAL 0)
    message(FATAL_ERROR "${LIST}: ${n}: cameradrawn ${cameradrawn} + camerakept ${camerakept} "
                        "must be ps2drops ${ps2drops}, each above 0 (a card each way)")
  endif()
  rd_fixture_expand(${n} "${OUT}/fixtures" dump)
  set(checked 0)
  foreach(s IN LISTS settings)
    set(dir "${OUT}/${n}/${s}")
    file(MAKE_DIRECTORY "${dir}/drop" "${dir}/bands")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_BESIDE=0 ICO_RD_REGION_RULE=ps2
                            "${TOOL}" "${dump}" "${dir}/drop-out.png"
                            ${args_${s}} --pass-dump "${dir}/drop"
                    RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
    if(rcA EQUAL 77)
      message(STATUS "SKIP: no device: ${errA}")
      return()
    endif()
    if(NOT rcA EQUAL 0)
      message(FATAL_ERROR "${n} ${s}: the replay without side bands exited ${rcA}:\n${outA}\n${errA}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_BESIDE=1 ICO_RD_REGION_RULE=ps2
                            "${TOOL}" "${dump}" "${dir}/bands-out.png"
                            ${args_${s}} --pass-dump "${dir}/bands" --stats
                    RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
    if(NOT rcB EQUAL 0)
      message(FATAL_ERROR "${n} ${s}: the replay with side bands exited ${rcB}:\n${outB}\n${errB}")
    endif()
    math(EXPR checked "${checked} + 1")
    # every target and the present byte for byte
    file(GLOB pngs RELATIVE "${dir}/drop" "${dir}/drop/*.png")
    list(LENGTH pngs np)
    if(np EQUAL 0)
      message(FATAL_ERROR "${n} ${s}: --pass-dump wrote no PNG")
    endif()
    set(differ "")
    foreach(p IN LISTS pngs)
      if(NOT EXISTS "${dir}/bands/${p}")
        list(APPEND differ "${p} (missing)")
        continue()
      endif()
      file(SHA256 "${dir}/drop/${p}" ha)
      file(SHA256 "${dir}/bands/${p}" hb)
      if(NOT ha STREQUAL hb)
        list(APPEND differ "${p}")
      endif()
    endforeach()
    if(differ)
      message(SEND_ERROR "${n} ${s}: the side bands changed ${differ} (a card drawn beside the "
                         "picture)")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "${n} ${s}: ${np} PNGs byte for byte with and without the side bands")
    endif()
    # the 10 s line's count, as --stats prints it
    if(NOT outB MATCHES "stats: side bands: ([0-9]+) draws with cards")
      message(SEND_ERROR "${n} ${s}: no side bands line in --stats:\n${outB}")
      math(EXPR failures "${failures} + 1")
    elseif(CMAKE_MATCH_1 EQUAL 0)
      message(SEND_ERROR "${n} ${s}: 0 draws with side bands and cards")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "${n} ${s}: ${CMAKE_MATCH_1} draws with side bands and cards")
    endif()
    # the CPU oracle: every drop a card's, none beside
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_REGION_RULE=ps2
                            "${TOOL}" "${dump}" "${dir}/region-out.png" ${args_${s}} --region-stats
                    RESULT_VARIABLE rcR OUTPUT_VARIABLE outR ERROR_VARIABLE errR)
    if(NOT rcR EQUAL 0)
      message(FATAL_ERROR "${n} ${s}: --region-stats exited ${rcR}:\n${outR}\n${errR}")
    endif()
    string(REGEX MATCH "region: frame [^\n]*" frameLine "${outR}")
    if(NOT frameLine MATCHES
       "drop ${ps2drops} \\(behind-eye 0, out-of-box 0, card ${ps2drops};"
       OR NOT frameLine MATCHES "beside 0 \\(behind-eye 0, out-of-box 0, card 0\\)")
      message(SEND_ERROR "${n} ${s}: the region frame line is not the cards' ${ps2drops} drops "
                         "and nothing beside:\n${frameLine}")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "${n} ${s}: ${frameLine}")
    endif()
    if(PYTHON AND wide_${s})
      execute_process(COMMAND "${PYTHON}" -I "${SEAM}" "${dir}/bands/PRESENT.png" --max 1.5
                      RESULT_VARIABLE rcS OUTPUT_VARIABLE outS ERROR_VARIABLE errS)
      string(STRIP "${outS}" outS)
      message(STATUS "${outS}")
      if(NOT rcS EQUAL 0)
        message(SEND_ERROR "${n} ${s}: the picture's edges show a seam (exit ${rcS}): "
                           "${outS}${errS}")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    # the camera rule: the facing card drawn over the whole frame, the other kept
    file(MAKE_DIRECTORY "${dir}/camera")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_REGION_RULE=camera
                            "${TOOL}" "${dump}" "${dir}/camera-out.png" ${args_${s}}
                            --pass-dump "${dir}/camera" --stats --region-stats
                    RESULT_VARIABLE rcC OUTPUT_VARIABLE outC ERROR_VARIABLE errC)
    if(NOT rcC EQUAL 0)
      message(FATAL_ERROR "${n} ${s}: the camera rule's replay exited ${rcC}:\n${outC}\n${errC}")
    endif()
    string(REGEX MATCH "region: frame [^\n]*" frameC "${outC}")
    if(NOT frameC MATCHES "drop ${camerakept} \\(behind-eye 0, out-of-box 0, card ${camerakept};"
       OR NOT frameC MATCHES
       "drawn by the camera rule ${cameradrawn} \\(behind-eye 0, out-of-box 0, card ${cameradrawn}\\)")
      message(SEND_ERROR "${n} ${s}: under the camera rule the region frame line is not the "
                         "facing card's ${cameradrawn} triangles drawn and the other's "
                         "${camerakept} kept:\n${frameC}")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "${n} ${s} (camera rule): ${frameC}")
    endif()
    if(NOT outC MATCHES "stats: region rule: the camera's: ([0-9]+) VU draws" OR CMAKE_MATCH_1 EQUAL 0)
      message(SEND_ERROR "${n} ${s}: no camera rule draws in --stats:\n${outC}")
      math(EXPR failures "${failures} + 1")
    endif()
    if(PYTHON AND wide_${s})
      execute_process(COMMAND "${PYTHON}" -I "${SEAM}" "${dir}/camera/PRESENT.png" --max 1.5
                      RESULT_VARIABLE rcS OUTPUT_VARIABLE outS ERROR_VARIABLE errS)
      string(STRIP "${outS}" outS)
      message(STATUS "camera rule: ${outS}")
      if(NOT rcS EQUAL 0)
        message(SEND_ERROR "${n} ${s}: under the camera rule the picture's edges show a seam "
                           "(exit ${rcS}): ${outS}${errS}")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
  endforeach()
  if(checked EQUAL 0)
    message(SEND_ERROR "${n}: no setting checked it")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "${failures} checks failed")
endif()
message(STATUS "rd_sideband_cards: the cards kept off the sides, drawn by the camera rule")
