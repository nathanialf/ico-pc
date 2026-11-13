# rd_turn_fold_ab: the present's turn drawn by its own box blit
# (port/render/rd_turn_fold.c; docs/port/RENDER_POST.md "The turn in the
# present's blit") against the turn pass, on lavapipe through the headless
# twin (ICO_VK_FAKE_TRANSFORM=90, 180, 270: rd_read_presented reads the turned
# image back turned back, rd_prerotate.c).  For each dump and setting, and
# each quarter turn, run A (the turn pass: ICO_RD_TURN_FOLD unset, the build's
# RD_TURN_FOLD_DEFAULT 0) and run B (ICO_RD_TURN_FOLD=1): the PNGs byte for
# byte, B's self-test passed, and B either folded (its line) or said why the
# present kept the turn pass:
#   phone   the PowerVR phone's profile (ICO_VK_FAKE_LIMITS=moltenvk, Enhanced,
#           --resolution 2364x1080 --full-height --full-pixel --present
#           2364x1080: one texel a pixel): must fold unless the dump's
#           deferred text or overlay is drawn
#   win720  a desktop 16:9 window at the window's resolution (1280x720,
#           Full pixel): must fold likewise
#   plain   --present 640x480 at 1x (the box blit scaled): must keep the turn
#           pass ("not one texel a pixel")
#   overlay rd_pixel's dump with --overlay-test at the phone's setting: must
#           keep it ("the port's overlay or the deferred text drawn")
# At least one present must have folded over the run.  The dumps: DUMP
# (rd_pixel's, may be absent) and LIST's; where /primary/dev/ico-dumps exists a
# listed dump that is absent fails.  No device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DLIST=<dump list> -DOUT=<a folder the test may write>
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
set(dumps "")
if(DUMP AND EXISTS "${DUMP}")
  list(APPEND dumps "${DUMP}")
endif()
set(listed "")
if(LIST AND EXISTS "${LIST}")
  file(STRINGS "${LIST}" lines)
  foreach(l IN LISTS lines)
    string(STRIP "${l}" l)
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    if(EXISTS "${l}")
      list(APPEND listed "${l}")
    elseif(EXISTS "/primary/dev/ico-dumps")
      message(FATAL_ERROR "${LIST} names ${l}, which is not here")
    else()
      message(STATUS "${l}: not here (no dump folder): skipped")
    endif()
  endforeach()
endif()
list(APPEND dumps ${listed})
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump is here")
  return()
endif()

set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone "ICO_VK_FAKE_LIMITS=moltenvk")
set(size_phone 2364x1080)
set(want_phone fold)
set(args_win720 --enhanced --resolution 1280x720 --full-height --full-pixel --present 1280x720)
set(env_win720 "")
set(size_win720 1280x720)
set(want_win720 fold)
set(args_plain --present 640x480)
set(env_plain "")
set(size_plain 640x480)
set(want_plain "the box blit not one texel a pixel")
set(args_overlay ${args_phone} --overlay-test)
set(env_overlay "ICO_VK_FAKE_LIMITS=moltenvk")
set(size_overlay 2364x1080)
set(want_overlay "the port's overlay or the deferred text drawn")

set(first TRUE)
set(runs 0)
set(folded 0)
set(i 0)
foreach(dump IN LISTS dumps)
  if(dump STREQUAL "${DUMP}")
    set(configs phone win720 plain overlay)
  else()
    set(configs phone win720 plain)
  endif()
  foreach(cfg IN LISTS configs)
    string(REGEX MATCH "^([0-9]+)x([0-9]+)$" _ "${size_${cfg}}")
    set(pw ${CMAKE_MATCH_1})
    set(ph ${CMAKE_MATCH_2})
    foreach(deg 90 180 270)
      set(tag "d${i}-${cfg}-${deg}")
      if(deg EQUAL 180)
        set(tw ${pw})
        set(th ${ph})
      else()
        set(tw ${ph})
        set(th ${pw})
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=ICO_RD_TURN_FOLD
                              ICO_VK_FAKE_TRANSFORM=${deg} ${env_${cfg}}
                              "${TOOL}" "${dump}" "${OUT}/${tag}-a.png" ${args_${cfg}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(first AND rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      set(first FALSE)
      if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${dump} ${cfg} ${deg}: the turn pass's run exited ${rc}:\n${out}\n${err}")
      endif()
      if("${out}${err}" MATCHES "turn in the blit: the ")
        message(FATAL_ERROR "${dump} ${cfg} ${deg}: the run with the fold off folded:\n${err}")
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_TURN_FOLD=1
                              ICO_VK_FAKE_TRANSFORM=${deg} ${env_${cfg}}
                              "${TOOL}" "${dump}" "${OUT}/${tag}-b.png" ${args_${cfg}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${dump} ${cfg} ${deg}: the fold's run exited ${rc}:\n${out}\n${err}")
      endif()
      set(log "${out}${err}")
      if(NOT log MATCHES "turn in the blit: self-test passed \\(6 cases")
        message(FATAL_ERROR "${dump} ${cfg} ${deg}: no passed self-test:\n${err}")
      endif()
      set(foldLine "turn in the blit: the ${pw}x${ph} picture's box blit drawn turned ${deg} degrees into the ${tw}x${th} image")
      set(textLine "keeps the turn pass: the port's overlay or the deferred text drawn")
      if(want_${cfg} STREQUAL "fold")
        if(log MATCHES "${foldLine}")
          math(EXPR folded "${folded} + 1")
        elseif(NOT log MATCHES "${textLine}")
          message(FATAL_ERROR "${dump} ${cfg} ${deg}: neither folded nor kept for its text:\n${err}")
        endif()
      else()
        if(log MATCHES "turn in the blit: the ")
          message(FATAL_ERROR "${dump} ${cfg} ${deg}: folded where it must keep the turn pass:\n${err}")
        endif()
        if(NOT log MATCHES "keeps the turn pass: ${want_${cfg}}")
          message(FATAL_ERROR "${dump} ${cfg} ${deg}: no \"${want_${cfg}}\" refusal:\n${err}")
        endif()
      endif()
      file(SHA256 "${OUT}/${tag}-a.png" ha)
      file(SHA256 "${OUT}/${tag}-b.png" hb)
      if(NOT ha STREQUAL hb)
        message(FATAL_ERROR "${dump} ${cfg} ${deg}: the folded present differs from the turn "
                            "pass's (${OUT}/${tag}-b.png against ${OUT}/${tag}-a.png)")
      endif()
      math(EXPR runs "${runs} + 1")
    endforeach()
  endforeach()
  math(EXPR i "${i} + 1")
endforeach()
if(folded EQUAL 0)
  message(FATAL_ERROR "rd_turn_fold_ab: ${runs} presents compared, none folded")
endif()
message(STATUS "rd_turn_fold_ab: ${runs} turned presents byte for byte, ${folded} drawn by the "
               "box blit itself")
