# rd_date_cut_ab: DATE's passes without the depth their draws never touch
# (port/render/rd_date_cut.c; docs/port/WORK_BUFFERS.md, "DATE's passes without
# the depth"), the cut off (ICO_RD_DATE_CUT=0) against on (=1), byte for byte.
# For every dump the list names (tools/date_cut_dumps.txt: a path, '*' globs
# allowed, and the passes the cut is expected to begin without the depth in a
# replay; a listed dump that is absent FAILS), rd_replay_tool's --pass-dump with
# --stats under the fixed-function blend (ICO_RD_BLEND=fixed), the store plan
# (ICO_RD_STORE_PLAN=1) and its poison (ICO_RD_STORE_POISON=1: a depth or
# stencil load the plan leaves undefined clears to garbage, so a depth the cut
# lost would show), DATE by the R8 snapshot (ICO_RD_DATE_PATH=snapshot) and the
# programs' own entries (ICO_RD_VU_PREPASS=0), at:
#   mali    the Mali-G52's (issue 179): 1x, 21:9, presented at 1612x720, the
#           fake Mali limits without the in-pass read (ICO_VK_FAKE_LIMITS=mali,
#           ICO_VK_FAKE_NO_DST=1);
#   adreno  the Adreno 740's (issue 123): Custom 3x at 3:2, Full height, Full
#           pixel, presented at 3048x2032, Qualcomm's driver faked
#           (ICO_VK_FAKE_ADRENO=1, ICO_VK_FAKE_NO_DST=1);
#   phone   the PowerVR phone's Resolution Auto (2364x1080, Full height, Full
#           pixel, aspect 2.189) with its in-pass read taken away
#           (ICO_VK_FAKE_NO_DST=1): a fractional scale, where the cut keeps
#           every depth (rd_date_cut_scale_ok; the Pixel's v3677 drew a
#           composite a level apart at four texels with the cut there before
#           the rule): 0 passes cut, the bytes the same;
# a dump drawn narrower than a setting's aspect cannot show it and is skipped
# there.  -DBACKEND=opengl (rd_date_cut_ab_gl): the mali setting alone on
# OpenGL (llvmpipe, DATE by the snapshot there; the Vulkan fakes left out).
# Each pair: every PNG the off run writes (SCENE, DISPLAY, PRESENT and every
# other named target) the on run's byte for byte; the same draws, passes and
# DATE snapshots, colour MB loaded and stored and MB copied (the stats'
# traffic line); the off run cuts nothing; the on run begins no pass again
# for a draw that needed the depth, cuts the passes the list expects (at
# least one where it takes two snapshots or more), and the depth and stencil
# MB loaded and stored each fall by at least 0.8 and at most 1.0 times the
# "at most" its stats name (4 bytes of depth and up to 1 of stencil a texel),
# and by something where it cut.  On Vulkan also --keys at the mali setting:
# the cut on asks for no pipeline the start-up set lacks that the cut off does
# not (no --aspect there: --present 1612x720 alone).  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# -DBACKEND=vulkan|opengl
# The lists name dumps of the build machine alone (the owner's rule there: a
# listed dump that is absent FAILS). A machine without that folder has none of
# them: the test is skipped as the older dump tests are.
if(NOT IS_DIRECTORY "/primary/dev/ico-dumps")
  message(STATUS "SKIP: no dump folder on this machine (/primary/dev/ico-dumps): the list's dumps are the build machine's")
  return()
endif()
set(dumps "")
set(failures 0)
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_date_cut_ab: no dump list ${LIST}")
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

set(common ICO_RD_BLEND=fixed ICO_RD_STORE_PLAN=1 ICO_RD_STORE_POISON=1
           ICO_RD_DATE_PATH=snapshot ICO_RD_VU_PREPASS=0)
set(args_mali --aspect 21:9 --present 1612x720)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1)
set(args_adreno --enhanced --resolution 3x --full-height --full-pixel --aspect 1.5
                --present 3048x2032)
set(env_adreno ICO_VK_FAKE_ADRENO=1 ICO_VK_FAKE_NO_DST=1)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
               --present 2364x1080)
set(env_phone ICO_VK_FAKE_NO_DST=1)
set(args_gl ${args_mali})
# --keys: no aspect (a dump drawn narrower than 21:9 cannot show it, and the
# keys do not depend on it)
set(args_keys --present 1612x720)
set(env_gl "")
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
else()
  set(settings mali adreno phone)
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(snapRe "stats: DATE snapshots ([0-9]+),")
set(cutRe "stats: DATE cut (on|off): ([0-9]+) passes begun without the depth \\(([0-9]+) draws, ([0-9]+) runs, ([0-9.]+) MB of depth and stencil each way at most\\), ([0-9]+) begun again with it")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored, depth and stencil ([0-9.]+)/([0-9.]+) MB loaded/stored, [0-9.]+ MB cleared, ([0-9.]+) MB copied")
set(keysRe "keys: [0-9]+ asked in [0-9]+ rounds, [0-9]+ in the set of [0-9]+, ([0-9]+) missing")

# a - b in hundredths of a MB (the stats print two decimals)
function(mb_hundredths out v)
  string(REGEX REPLACE "^([0-9]+)\\.([0-9][0-9])$" "\\1\\2" h "${v}")
  math(EXPR h "${h}")
  set(${out} "${h}" PARENT_SCOPE)
endfunction()

set(compared 0)
set(cutTotal 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(cut 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${cut}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}} ICO_RD_DATE_CUT=${cut}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND cut EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} cut ${cut}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      set(ok TRUE)
      foreach(re drawsRe snapRe cutRe trafficRe)
        if(NOT flat MATCHES "${${re}}")
          message(SEND_ERROR "${dump} ${s} cut ${cut}: no ${re} line:\n${out}")
          math(EXPR failures "${failures} + 1")
          set(ok FALSE)
        endif()
      endforeach()
      if(NOT ok)
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${cut} "${CMAKE_MATCH_1}")
      set(passes_${cut} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${snapRe}" m "${flat}")
      set(snaps_${cut} "${CMAKE_MATCH_1}")
      string(REGEX MATCH "${cutRe}" m "${flat}")
      set(word_${cut} "${CMAKE_MATCH_1}")
      set(cut_${cut} "${CMAKE_MATCH_2}")
      set(mb_${cut} "${CMAKE_MATCH_5}")
      set(again_${cut} "${CMAKE_MATCH_6}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(cl_${cut} "${CMAKE_MATCH_1}")
      set(cs_${cut} "${CMAKE_MATCH_2}")
      set(dl_${cut} "${CMAKE_MATCH_3}")
      set(ds_${cut} "${CMAKE_MATCH_4}")
      set(cp_${cut} "${CMAKE_MATCH_5}")
      message(STATUS "${dump} ${s} cut ${cut}: ${passes_${cut}} passes, ${draws_${cut}} draws, "
                     "${snaps_${cut}} DATE snapshots; ${cut_${cut}} passes without the depth "
                     "(${mb_${cut}} MB each way at most), ${again_${cut}} begun again; depth and "
                     "stencil ${dl_${cut}}/${ds_${cut}} MB loaded/stored")
    endforeach()
    if(skipped)
      continue()
    endif()
    # the pair
    if(NOT word_0 STREQUAL "off" OR NOT cut_0 EQUAL 0 OR NOT word_1 STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: the cut says ${word_0} (${cut_0} passes) off and "
                         "${word_1} on: off cutting nothing and on expected")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT draws_1 EQUAL draws_0 OR NOT passes_1 EQUAL passes_0 OR NOT snaps_1 EQUAL snaps_0)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws, ${passes_1} passes, ${snaps_1} "
                         "snapshots with the cut; ${draws_0}, ${passes_0} and ${snaps_0} without")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT cl_1 STREQUAL cl_0 OR NOT cs_1 STREQUAL cs_0 OR NOT cp_1 STREQUAL cp_0)
      message(SEND_ERROR "${dump} ${s}: colour ${cl_1}/${cs_1} MB, copies ${cp_1} MB with the "
                         "cut; ${cl_0}/${cs_0} and ${cp_0} without (the cut moves depth only)")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT again_1 EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: ${again_1} passes begun again with the depth (the scan "
                         "predicted a pass end that did not come)")
      math(EXPR failures "${failures} + 1")
    endif()
    set(want "${want_${dump}}")
    if(s STREQUAL "phone")
      # a fractional scale (2364x1080 for the frame: 2.79 x 2.41 on the
      # Pixel's): the cut keeps the depth (rd_date_cut.h rd_date_cut_scale_ok)
      set(want 0)
    endif()
    if(NOT want STREQUAL "" AND NOT cut_1 EQUAL want)
      message(SEND_ERROR "${dump} ${s}: ${cut_1} passes without the depth, ${want} expected")
      math(EXPR failures "${failures} + 1")
    elseif(want STREQUAL "" AND snaps_1 GREATER 1 AND cut_1 LESS 1 AND NOT s STREQUAL "phone")
      message(SEND_ERROR "${dump} ${s}: ${snaps_1} DATE snapshots and no pass without the depth")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR cutTotal "${cutTotal} + ${cut_1}")
    mb_hundredths(l0 "${dl_0}")
    mb_hundredths(l1 "${dl_1}")
    mb_hundredths(s0 "${ds_0}")
    mb_hundredths(s1 "${ds_1}")
    mb_hundredths(most "${mb_1}")
    math(EXPR fallL "${l0} - ${l1}")
    math(EXPR fallS "${s0} - ${s1}")
    math(EXPR least "${most} * 8 / 10 - 2")
    math(EXPR upto "${most} + 2")
    if(fallL LESS least OR fallL GREATER upto OR fallS LESS least OR fallS GREATER upto OR
       (cut_1 GREATER 0 AND (fallL LESS 1 OR fallS LESS 1)))
      message(SEND_ERROR "${dump} ${s}: depth and stencil ${dl_0}/${ds_0} -> ${dl_1}/${ds_1} MB "
                         "loaded/stored: the cut's ${cut_1} passes name up to ${mb_1} MB each way")
      math(EXPR failures "${failures} + 1")
    endif()
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written with the cut off, ${npngOn} on")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the cut on: it must be the same "
                           "bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
  # the start-up set: no key the cut asks for that the set lacks
  if(NOT "${BACKEND}" STREQUAL "opengl")
    foreach(cut 0 1)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_mali} ICO_RD_DATE_CUT=${cut}
                              "${TOOL}" "${dump}" - --keys ${args_keys}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${keysRe}")
        message(SEND_ERROR "${dump} --keys cut ${cut}: exit ${rc}, no keys line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(missing_${cut} 0)
        continue()
      endif()
      set(missing_${cut} "${CMAKE_MATCH_1}")
    endforeach()
    message(STATUS "${dump} --keys: ${missing_0} missing with the cut off, ${missing_1} on")
    if(missing_1 GREATER missing_0)
      message(SEND_ERROR "${dump}: the cut asks for ${missing_1} pipelines the start-up set "
                         "lacks, ${missing_0} without it")
      math(EXPR failures "${failures} + 1")
    endif()
  endif()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_date_cut_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_date_cut_ab: no target compared over ${ndumps} dumps")
endif()
if(cutTotal EQUAL 0)
  message(FATAL_ERROR "rd_date_cut_ab: no pass was begun without the depth over ${ndumps} dumps")
endif()
message(STATUS "rd_date_cut_ab: ${compared} targets the same bytes over ${ndumps} dumps; "
               "${cutTotal} passes begun without the depth")
