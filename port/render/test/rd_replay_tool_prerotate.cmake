# rd_replay_tool_prerotate: the present into a swapchain made in the surface's
# own turn (port/render/rd_prerotate.c; docs/port/DISPLAY.md "The turned
# swapchain"), on lavapipe through its headless twin.  A headless device has
# no swapchain to turn, so under ICO_VK_FAKE_TRANSFORM=90, 180 or 270 the
# present draws its picture as always, turns it into a texture of the turned
# size in the swapchain path's own pass (blit_vs with DrawCB aux1.w), and
# rd_read_presented reads that texture back and turns it back on the CPU by
# the spec's table.  Each run's PNG must be the identity run's byte for byte
# (a turn read the wrong way round, off by a texel or filtered changes it),
# and the turned runs must say they drew the twin (`turned twin: ... turned
# 90 degrees into HxW`).  Runs, for rd_pixel's dump (DUMP) and each dump of
# LIST that is here:
#   plain     --present 640x480
#   wide      --present 1280x720 (the bars beside the box; the turned size
#             is not square)
#   overlay   --present 640x480 --overlay-test (the overlay's outline,
#             squares and popup text: the port's UI, where the touch
#             controls and the fps corner are drawn, in the same place)
#   crt       --present 640x480 --crt pvm (the CRT filter's own passes into
#             the output), rd_pixel's dump only
#   phone     --present 2400x1080 (the PowerVR phone's window, issue 188's log), the
#             list's dumps only
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DLIST=<dump list> -DOUT=<a folder the test may write>
# Exit 77 of the first run (no device) skips.
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

set(extra "")
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
    if(l MATCHES "[*]")
      file(GLOB found "${l}")
      list(SORT found)
      list(APPEND extra ${found})
    elseif(EXISTS "${l}")
      list(APPEND extra "${l}")
    else()
      message(STATUS "${l}: no such dump, skipped")
    endif()
  endforeach()
endif()

set(args_plain --present 640x480)
set(size_plain 640x480)
set(args_wide --present 1280x720)
set(size_wide 1280x720)
set(args_overlay --present 640x480 --overlay-test)
set(size_overlay 640x480)
set(args_crt --present 640x480 --crt pvm)
set(size_crt 640x480)
set(args_phone --present 2400x1080)
set(size_phone 2400x1080)

set(runs 0)
set(first TRUE)
set(i 0)
foreach(dump "${DUMP}" ${extra})
  if(dump STREQUAL "${DUMP}")
    set(configs plain wide overlay crt)
  else()
    set(configs plain phone)
  endif()
  foreach(cfg IN LISTS configs)
    set(tag "d${i}-${cfg}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=ICO_VK_FAKE_TRANSFORM
                            "${TOOL}" "${dump}" "${OUT}/${tag}-0.png" ${args_${cfg}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(first AND rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    set(first FALSE)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${dump} ${cfg}: the identity run exited ${rc}:\n${out}\n${err}")
    endif()
    if("${out}${err}" MATCHES "turned twin")
      message(FATAL_ERROR "${dump} ${cfg}: the identity run drew a turned twin:\n${err}")
    endif()
    file(SHA256 "${OUT}/${tag}-0.png" want)
    string(REGEX MATCH "^([0-9]+)x([0-9]+)$" _ "${size_${cfg}}")
    set(pw ${CMAKE_MATCH_1})
    set(ph ${CMAKE_MATCH_2})
    foreach(deg 90 180 270)
      if(deg EQUAL 180)
        set(tw ${pw})
        set(th ${ph})
      else()
        set(tw ${ph})
        set(th ${pw})
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_VK_FAKE_TRANSFORM=${deg}
                              "${TOOL}" "${dump}" "${OUT}/${tag}-${deg}.png" ${args_${cfg}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${dump} ${cfg}: ICO_VK_FAKE_TRANSFORM=${deg} exited ${rc}:\n${out}\n${err}")
      endif()
      if(NOT "${out}${err}" MATCHES "turned twin: the ${pw}x${ph} picture turned ${deg} degrees into ${tw}x${th}")
        message(FATAL_ERROR "${dump} ${cfg}: ICO_VK_FAKE_TRANSFORM=${deg} drew no ${tw}x${th} twin:\n${out}\n${err}")
      endif()
      file(SHA256 "${OUT}/${tag}-${deg}.png" got)
      if(NOT got STREQUAL want)
        message(FATAL_ERROR "${dump} ${cfg}: turned ${deg} and back is not the identity present "
                            "(${OUT}/${tag}-${deg}.png against ${OUT}/${tag}-0.png)")
      endif()
      math(EXPR runs "${runs} + 1")
    endforeach()
  endforeach()
  math(EXPR i "${i} + 1")
endforeach()
message(STATUS "rd_replay_tool_prerotate: ${runs} turned presents, each the identity present byte "
               "for byte")
