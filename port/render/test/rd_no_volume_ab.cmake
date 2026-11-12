# rd_no_volume_ab: a shadow window with no volume drawn as before
# (ICO_RD_NO_VOLUME=0) against SCENE's pass kept open and the count and levels
# written as zero clears (=1; port/render/rd_no_volume.c, docs/port/RENDER_POST.md
# "A shadow window with no volume"), byte for byte.  For every dump the list
# names (tools/no_volume_dumps.txt: a path, '*' globs allowed, and the windows a
# replay takes at the phone setting, 1 or 0; a listed dump that is absent
# FAILS), rd_replay_tool's --pass-dump with --stats under the store plan
# (ICO_RD_STORE_PLAN=1) and the programs' own entries (ICO_RD_VU_PREPASS=0), at:
#   phone     the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=
#             moltenvk ICO_RD_LATE_PASS=1: DATE through the stencil bit, the
#             count in one draw, the late passes as the phone has them):
#             Enhanced, 2364x1080, Full height, Full pixel, presented 2364x1080;
#   desktop   lavapipe as it is, 1x, presented 1280x720: DATE by the R8
#             snapshot (no window: no local read), so not taken (DATE);
#   dstencil  the same with DATE through the stencil bit
#             (ICO_RD_DATE_PATH=stencil): taken at 1x (the count target at
#             scale 1, its own zero);
#   mali      the Mali-G52's (ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1:
#             DATE by the R8 snapshot), presented 1612x720: not taken (DATE);
#   gl        -DBACKEND=opengl (rd_no_volume_ab_gl): 1x on OpenGL, presented
#             1280x720: not taken (DATE by the snapshot);
# a dump a setting cannot show ("cannot show") is skipped there.  Each pair:
#   - every PNG the =0 run writes (SCENE, SHADOW0-2, DISPLAY, PRESENT, every
#     other target) the =1 run's byte for byte, the same number;
#   - =0 says "no volume off: 0 kept"; a window with volumes is counted in
#     both runs and never taken;
#   - where the =1 run takes none: the same passes and the same traffic, to
#     the last printed figure;
#   - where it takes K (a dump listed with 1 at phone and dstencil, 0 on the
#     others), from rd_replay.c and rd_no_volume.c: the pass count falls by
#     K at phone (the count's pass, the three levels' and list 3's pass on
#     SCENE not begun, four zero clears begun: the reduced count's and the
#     three levels') and by 2K at dstencil (the shadow reset's own stencil
#     clear pass not begun either: the volumes' merge is the tile-based
#     classes' only); the colour stored falls by the kept pass's colour
#     (W x H x 4: list 0's store of SCENE) and the colour loaded by that and
#     the levels' (list 3's load of SCENE, and each level's pass loaded its
#     target; the zero clears load nothing), at phone the cleared bytes rise
#     by the levels' (the reduced count's CLEAR stands for the boxed count's;
#     at dstencil the reset's pass cleared the stencil too);
#     at phone the depth and stencil loaded and stored fall by W x H x 5
#     (ICO_VK_FAKE_LIMITS=moltenvk is the Apple class: the stencil is loaded
#     and stored with the depth, rd_stencil_selftest.c); each printed MB
#     figure is rounded to 0.01, so a difference is taken within 0.02 MB of
#     the bytes;
# and over the dumps the phone setting takes at least one window.  No device:
# SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# -DBACKEND=vulkan|opengl
set(dumps "")
set(failures 0)
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_no_volume_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  set(want "")
  if(l MATCHES "^([^ \t]+)[ \t]+([0-9]+)$")
    set(l "${CMAKE_MATCH_1}")
    set(want "${CMAKE_MATCH_2}")
  else()
    message(SEND_ERROR "${l}: a dump line is a path and the windows taken at the phone setting")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${listDir}/${l}")
  endif()
  if(l MATCHES "[*]")
    file(GLOB found "${l}")
    list(SORT found)
    if(NOT found)
      message(SEND_ERROR "${l}: no dump matches (a listed dump must exist)")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(d IN LISTS found)
      list(APPEND dumps "${d}")
      set(want_${d} "${want}")
    endforeach()
  elseif(EXISTS "${l}")
    list(APPEND dumps "${l}")
    set(want_${l} "${want}")
  else()
    message(SEND_ERROR "${l}: no such dump (a listed dump must exist)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)

set(common ICO_RD_STORE_PLAN=1 ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1)
set(args_desktop --present 1280x720)
set(env_desktop "")
set(args_dstencil --present 1280x720)
set(env_dstencil ICO_RD_DATE_PATH=stencil)
set(args_mali --present 1612x720)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1)
set(args_gl --present 1280x720)
set(env_gl "")
# the settings that take a window (want applies), the passes each window
# takes off, and the depth and stencil bytes a texel the kept pass does not
# store and load again (0: not asserted: at dstencil the reset's own pass
# moved the depth too), and whether the cleared bytes rise by the levels'
# exactly (at dstencil the reset's pass cleared the stencil as well)
set(takes_phone TRUE)
set(drop_phone 1)
set(depthB_phone 5)
set(cleared_phone TRUE)
set(takes_dstencil TRUE)
set(drop_dstencil 2)
set(depthB_dstencil 0)
set(cleared_dstencil FALSE)
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
else()
  set(settings phone desktop dstencil mali)
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
# (CMake keeps nine groups a match: each line in two regexes)
set(trafficRe "stats: traffic: colour [0-9.]+ MB loaded [0-9.]+ MB stored, depth and stencil [0-9.]+/[0-9.]+ MB loaded/stored, [0-9.]+ MB cleared")
set(colourRe "stats: traffic: colour ([0-9]+)\\.([0-9][0-9]) MB loaded ([0-9]+)\\.([0-9][0-9]) MB stored")
set(depthRe "MB stored, depth and stencil ([0-9]+)\\.([0-9][0-9])/([0-9]+)\\.([0-9][0-9]) MB loaded/stored, ([0-9]+)\\.([0-9][0-9]) MB cleared")
set(nvRe "stats: no volume (on|off): ([0-9]+) kept \\(([0-9]+) with the pass open over ([0-9]+)x([0-9]+), depth (yes|no)[;,] [0-9]+ zero clears, ([0-9]+) level texels[;,] [0-9]+ draws not drawn\\), [0-9]+ with volumes, [0-9]+ not taken")
set(nvTailRe "stats: no volume [onf]+: [^\n]* draws not drawn\\), ([0-9]+) with volumes, ([0-9]+) not taken")

# hundredths of a MB from the integer and fraction a figure printed
function(nv_hundredths out i f)
  if(f MATCHES "^0([0-9])$")
    set(f "${CMAKE_MATCH_1}")
  endif()
  math(EXPR v "${i} * 100 + ${f}")
  set(${out} "${v}" PARENT_SCOPE)
endfunction()

# fails unless off - on (hundredths) lies within the rounding of bytes
# (2 printed figures, each within half a hundredth: [floor - 1, floor + 2])
function(nv_delta what off on bytes)
  math(EXPR got "${off} - ${on}")
  math(EXPR lo "${bytes} * 100 / 1048576 - 1")
  math(EXPR hi "${bytes} * 100 / 1048576 + 2")
  if(got LESS lo OR got GREATER hi)
    message(SEND_ERROR "${what}: off less on ${got} hundredths of a MB, ${bytes} bytes "
                       "(${lo}..${hi}) expected")
    set(nvFail TRUE PARENT_SCOPE)
  endif()
endfunction()

set(compared 0)
set(phoneKept 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(nv 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${nv}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}}
                              ICO_RD_NO_VOLUME=${nv}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND nv EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (the setting cannot show it)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} nv ${nv}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${drawsRe}" OR NOT flat MATCHES "${trafficRe}" OR
         NOT flat MATCHES "${nvRe}")
        message(SEND_ERROR "${dump} ${s} nv ${nv}: no draws, traffic or no volume line:\n${out}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(passes_${nv} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(traffic_${nv} "${m}")
      string(REGEX MATCH "${colourRe}" m "${flat}")
      nv_hundredths(cl_${nv} "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
      nv_hundredths(cs_${nv} "${CMAKE_MATCH_3}" "${CMAKE_MATCH_4}")
      string(REGEX MATCH "${depthRe}" m "${flat}")
      set(d1 "${CMAKE_MATCH_1}")
      set(d2 "${CMAKE_MATCH_2}")
      set(d3 "${CMAKE_MATCH_3}")
      set(d4 "${CMAKE_MATCH_4}")
      set(d5 "${CMAKE_MATCH_5}")
      set(d6 "${CMAKE_MATCH_6}")
      nv_hundredths(dl_${nv} "${d1}" "${d2}")
      nv_hundredths(ds_${nv} "${d3}" "${d4}")
      nv_hundredths(cr_${nv} "${d5}" "${d6}")
      string(REGEX MATCH "${nvRe}" m "${flat}")
      set(word_${nv} "${CMAKE_MATCH_1}")
      set(kept_${nv} "${CMAKE_MATCH_2}")
      set(open_${nv} "${CMAKE_MATCH_3}")
      set(pw_${nv} "${CMAKE_MATCH_4}")
      set(ph_${nv} "${CMAKE_MATCH_5}")
      set(levels_${nv} "${CMAKE_MATCH_7}")
      string(REGEX MATCH "${nvTailRe}" m "${flat}")
      set(with_${nv} "${CMAKE_MATCH_1}")
      set(refused_${nv} "${CMAKE_MATCH_2}")
      message(STATUS "${dump} ${s} nv ${nv}: ${passes_${nv}} passes; no volume ${word_${nv}}: "
                     "${kept_${nv}} kept (${open_${nv}} open over ${pw_${nv}}x${ph_${nv}}, "
                     "${levels_${nv}} level texels), ${with_${nv}} with volumes, "
                     "${refused_${nv}} not taken; ${traffic_${nv}}")
    endforeach()
    if(skipped)
      continue()
    endif()
    set(listed "${want_${dump}}")
    set(want "${listed}")
    if(NOT takes_${s})
      set(want 0)
    endif()
    if(NOT word_0 STREQUAL "off" OR NOT kept_0 EQUAL 0 OR NOT kept_1 EQUAL want OR
       NOT with_0 EQUAL with_1)
      message(SEND_ERROR "${dump} ${s}: =0 says ${word_0} with ${kept_0} kept, =1 ${kept_1} "
                         "kept (${want} expected), ${with_0} and ${with_1} with volumes")
      math(EXPR failures "${failures} + 1")
    endif()
    if(listed EQUAL 0 AND (with_1 EQUAL 0 OR NOT refused_1 EQUAL 0))
      message(SEND_ERROR "${dump} ${s}: listed with volumes, ${with_1} counted, ${refused_1} "
                         "not taken")
      math(EXPR failures "${failures} + 1")
    endif()
    if(listed GREATER 0 AND kept_1 EQUAL 0 AND refused_1 EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: listed without a volume, neither taken nor refused")
      math(EXPR failures "${failures} + 1")
    endif()
    if(kept_1 EQUAL 0)
      if(NOT passes_0 EQUAL passes_1 OR NOT traffic_0 STREQUAL traffic_1)
        message(SEND_ERROR "${dump} ${s}: nothing taken, but ${passes_1} passes against "
                           "${passes_0}, or the traffic moved:\n  ${traffic_0}\n  ${traffic_1}")
        math(EXPR failures "${failures} + 1")
      endif()
    else()
      if(s STREQUAL "phone")
        math(EXPR phoneKept "${phoneKept} + ${kept_1}")
      endif()
      set(nvFail FALSE)
      math(EXPR wantPasses "${passes_0} - ${kept_1} * ${drop_${s}}")
      if(NOT passes_1 EQUAL wantPasses OR NOT open_1 EQUAL kept_1)
        message(SEND_ERROR "${dump} ${s}: ${passes_1} passes taken (${passes_0} drawn as "
                           "before: ${wantPasses} expected), ${open_1} of ${kept_1} with the "
                           "pass open")
        set(nvFail TRUE)
      endif()
      math(EXPR colour "${kept_1} * ${pw_1} * ${ph_1} * 4")
      math(EXPR levels "${levels_1} * 4")
      math(EXPR loaded "${colour} + ${levels}")
      nv_delta("${dump} ${s}: colour stored" "${cs_0}" "${cs_1}" "${colour}")
      nv_delta("${dump} ${s}: colour loaded" "${cl_0}" "${cl_1}" "${loaded}")
      if(cleared_${s})
        nv_delta("${dump} ${s}: cleared" "${cr_1}" "${cr_0}" "${levels}")
      endif()
      if(depthB_${s} GREATER 0)
        math(EXPR depth "${kept_1} * ${pw_1} * ${ph_1} * ${depthB_${s}}")
        nv_delta("${dump} ${s}: depth and stencil loaded" "${dl_0}" "${dl_1}" "${depth}")
        nv_delta("${dump} ${s}: depth and stencil stored" "${ds_0}" "${ds_1}" "${depth}")
      elseif(dl_1 GREATER dl_0 OR ds_1 GREATER ds_0)
        message(SEND_ERROR "${dump} ${s}: more depth and stencil moved with the window taken")
        set(nvFail TRUE)
      endif()
      if(nvFail)
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written drawn as before, ${npngOn} "
                         "with the window taken")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the window taken: it must be the "
                           "same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(NOT "${BACKEND}" STREQUAL "opengl" AND phoneKept EQUAL 0)
  message(SEND_ERROR "rd_no_volume_ab: the phone setting took no window over ${ndumps} dumps")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_no_volume_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_no_volume_ab: no target compared over ${ndumps} dumps")
endif()
message(STATUS "rd_no_volume_ab: ${compared} targets the same bytes over ${ndumps} dumps; the "
               "phone setting took ${phoneKept} windows")
