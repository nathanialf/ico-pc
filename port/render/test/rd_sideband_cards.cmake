# rd_sideband_cards: issue 189 (lighter bars beside the 4:3 picture near
# stage 32's exit): the region rule's side bands leave a card dropped
# (vu_common.hlsli vu_rule_beside, docs/port/VU1_PROGRAMS.md "The region
# rule", step 7).  On the reporter's frame the only triangles the rule drops
# are three of the mist quads' (mesh wf_smoke1, list 5: draws 5:11 and
# 5:22), cards, which the side bands drew beside the picture only before
# the fix: the sides 7 levels lighter, with an edge at the picture's.  At four settings, the
# Original preset at 4:3 1x, Enhanced 4:3 2x, Enhanced 16:10 1x and the
# reporter's own (Enhanced 16:10 at 1920x1200, full height, Full pixel).
# The dump was recorded at 16:10, so its frame is 624 GS pixels wide at
# every setting (at 4:3 cropped to the picture's 512 at the present): the
# side bands are on in all four, and with the cards drawn the mist beside the
# picture reached even the 4:3 present through the softening's work
# buffer (AA0), which reads across the picture's edge.  (The game itself
# draws a 512-wide frame at 4:3, where the side bands are off: the
# reporter saw no bars there.)  At each, rd_replay_tool --pass-dump twice:
#   A with ICO_RD_VU_BESIDE=0 (every VU draw keeps the PS2's drop) and B with
#   the side bands, every target and the present byte for byte the same
#   (this frame's drops are all cards, so the side bands add nothing);
#   B's --stats line "side bands: N draws with cards": N at least 1 (the
#   mist draws, whose cards the side bands now leave out);
#   --region-stats (the CPU oracle): the frame's line with every drop a
#   card's ("drop 3 (behind-eye 0, out-of-box 0, card 3") and nothing
#   beside ("beside 0 (behind-eye 0, out-of-box 0, card 0)");
# and on the 16:10 presents, with -DPYTHON, tools/band_seam.py on B's
# PRESENT.png: both edges of the picture within 1.5 levels (7.3 and 7.2
# with the cards drawn beside the picture; 0.1 with the drop kept, the
# image's own column-to-column step).
# A missing dump (machine-local) or no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DDUMP=<the 189 dump> -DOUT=<a folder the test may write>
# -DSEAM=<tools/band_seam.py> [-DPYTHON=<python3>]
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (machine-local)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")

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
foreach(s IN LISTS settings)
  set(dir "${OUT}/${s}")
  file(MAKE_DIRECTORY "${dir}/drop" "${dir}/bands")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_BESIDE=0 "${TOOL}" "${DUMP}" -
                          ${args_${s}} --pass-dump "${dir}/drop"
                  RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
  if(rcA EQUAL 77)
    message(STATUS "SKIP: no device: ${errA}")
    return()
  endif()
  if(NOT rcA EQUAL 0)
    message(FATAL_ERROR "${s}: the replay without side bands exited ${rcA}:\n${outA}\n${errA}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_BESIDE=1 "${TOOL}" "${DUMP}" -
                          ${args_${s}} --pass-dump "${dir}/bands" --stats
                  RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
  if(NOT rcB EQUAL 0)
    message(FATAL_ERROR "${s}: the replay with side bands exited ${rcB}:\n${outB}\n${errB}")
  endif()
  # every target and the present byte for byte
  file(GLOB pngs RELATIVE "${dir}/drop" "${dir}/drop/*.png")
  list(LENGTH pngs np)
  if(np EQUAL 0)
    message(FATAL_ERROR "${s}: --pass-dump wrote no PNG")
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
    message(SEND_ERROR "${s}: the side bands changed ${differ} (a card drawn beside the picture)")
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "${s}: ${np} PNGs byte for byte with and without the side bands")
  endif()
  # the 10 s line's count, as --stats prints it
  if(NOT outB MATCHES "stats: side bands: ([0-9]+) draws with cards")
    message(SEND_ERROR "${s}: no side bands line in --stats:\n${outB}")
    math(EXPR failures "${failures} + 1")
  else()
    set(cardDraws "${CMAKE_MATCH_1}")
    if(cardDraws EQUAL 0)
      message(SEND_ERROR "${s}: ${cardDraws} draws with side bands and cards")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "${s}: ${cardDraws} draws with side bands and cards")
    endif()
  endif()
  # the CPU oracle: every drop a card's, none beside
  execute_process(COMMAND "${TOOL}" "${DUMP}" - ${args_${s}} --region-stats
                  RESULT_VARIABLE rcR OUTPUT_VARIABLE outR ERROR_VARIABLE errR)
  if(NOT rcR EQUAL 0)
    message(FATAL_ERROR "${s}: --region-stats exited ${rcR}:\n${outR}\n${errR}")
  endif()
  string(REGEX MATCH "region: frame [^\n]*" frameLine "${outR}")
  if(NOT frameLine MATCHES "drop 3 \\(behind-eye 0, out-of-box 0, card 3;"
     OR NOT frameLine MATCHES "beside 0 \\(behind-eye 0, out-of-box 0, card 0\\)")
    message(SEND_ERROR "${s}: the region frame line is not the mist's three card drops and "
                       "nothing beside:\n${frameLine}")
    math(EXPR failures "${failures} + 1")
  else()
    message(STATUS "${s}: ${frameLine}")
  endif()
  if(PYTHON AND wide_${s})
    execute_process(COMMAND "${PYTHON}" -I "${SEAM}" "${dir}/bands/PRESENT.png" --max 1.5
                    RESULT_VARIABLE rcS OUTPUT_VARIABLE outS ERROR_VARIABLE errS)
    string(STRIP "${outS}" outS)
    message(STATUS "${outS}")
    if(NOT rcS EQUAL 0)
      message(SEND_ERROR "${s}: the picture's edges show a seam (exit ${rcS}): ${outS}${errS}")
      math(EXPR failures "${failures} + 1")
    endif()
  endif()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "${failures} checks failed")
endif()
