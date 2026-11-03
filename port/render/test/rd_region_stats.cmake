# rd_region_stats: rd_replay_tool --region-stats (port/render/tools/rd_replay_region.c,
# docs/port/VU1_PROGRAMS.md "The region rule") on the first two dumps of the
# machine-local list tools/occlusion_corpus.txt that are here (read as
# rd_occlusion_tool.cmake reads it), each at two settings: Original 4:3 1x
# and Enhanced 4:3 2x (tools/occlusion_corpus.py's orig and enh2x43), with
# a present.
#   1. --pass-dump PLAIN --region-out SPEC: the frame's region line
#      ("<dump>: region: frame N, ... drawing nothing: K draws: S") with S
#      "none" or L:I items of lists 0-6, K the items, SPEC holding S, and a
#      line a draw and a list in the shape the tool documents;
#   2. --pass-dump NOP --nop @SPEC: every PNG of PLAIN equal to NOP's byte
#      for byte (the draws the CPU oracle says draw nothing draw no pixel:
#      the stats' verdicts agree with the shaders' there);
#   3. at the orig setting with --no-device the same frame line and spec
#      (the stats need no device; the record-only renderer takes no
#      display options, so the enh2x43 state is not its).
# A missing list or dump, or no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
if(NOT EXISTS "${LIST}")
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(dumps "")
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${listDir}/${l}")
  endif()
  if(l MATCHES "[*]")
    file(GLOB matched "${l}")
    list(SORT matched)
    list(APPEND dumps ${matched})
  elseif(EXISTS "${l}")
    list(APPEND dumps "${l}")
  endif()
  list(LENGTH dumps nd)
  if(nd GREATER_EQUAL 2)
    break()
  endif()
endforeach()
list(LENGTH dumps nd)
if(nd EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()
if(nd GREATER 2)
  list(SUBLIST dumps 0 2 dumps)
endif()
file(REMOVE_RECURSE "${OUT}")

set(settings_orig --aspect 4:3 --present 640x480)
set(settings_enh2x43 --enhanced --aspect 4:3 --resolution 2x --present 1280x960)
set(frameRe "region: frame [0-9]+, [0-9]+x[0-9]+, (extended|out of the box), free camera (on|off), [0-9]+ draws with the ticks' cameras; lists 0-6: ([0-9]+) draws under the region rule \\([0-9]+ VU draws not\\), tris ([0-9]+): exact [0-9]+ clip [0-9]+ strip [0-9]+ drop ([0-9]+) \\(behind-eye [0-9]+, out-of-box [0-9]+, card [0-9]+; rescued at a tick [0-9]+\\), beside [0-9]+ \\(behind-eye [0-9]+, out-of-box [0-9]+, card [0-9]+\\); hidden [0-9]+; det - in [0-9]+ draws; drawing nothing: ([0-9]+) draws: ([^\n]+)")
set(drawRe "region: [0-6]:[0-9]+ (mesh|skinned|grid) [^ ]+ tris [0-9]+: exact [0-9]+ clip [0-9]+ strip [0-9]+ drop [0-9]+ \\(behind-eye [0-9]+, out-of-box [0-9]+, card [0-9]+; rescued at a tick [0-9]+\\), beside [0-9]+ \\(behind-eye [0-9]+, out-of-box [0-9]+, card [0-9]+\\) det [-+][^\n]*; beside [0-9]+, dropped back [0-9]+")
set(n 0)
foreach(dump IN LISTS dumps)
  math(EXPR n "${n} + 1")
  foreach(setting orig enh2x43)
    set(dir "${OUT}/${n}-${setting}")
    file(MAKE_DIRECTORY "${dir}/plain" "${dir}/nop")
    set(spec "${dir}/nothing.txt")
    execute_process(COMMAND "${TOOL}" "${dump}" - --pass-dump "${dir}/plain" ${settings_${setting}}
                            --region-out "${spec}"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${dump} (${setting}): --region-stats exit ${rc}:\n${out}\n${err}")
    endif()
    if(NOT out MATCHES "${frameRe}")
      message(FATAL_ERROR "${dump} (${setting}): no region frame line:\n${out}")
    endif()
    set(draws "${CMAKE_MATCH_3}")
    set(tris "${CMAKE_MATCH_4}")
    set(drop "${CMAKE_MATCH_5}")
    set(k "${CMAKE_MATCH_6}")
    set(s "${CMAKE_MATCH_7}")
    if(draws EQUAL 0 OR tris EQUAL 0)
      message(FATAL_ERROR "${dump} (${setting}): no triangle under the region rule:\n${out}")
    endif()
    if(NOT out MATCHES "${drawRe}")
      message(FATAL_ERROR "${dump} (${setting}): no region line of a draw:\n${out}")
    endif()
    if(NOT out MATCHES "region: list [0-6]: [0-9]+ draws, tris [0-9]+")
      message(FATAL_ERROR "${dump} (${setting}): no region line of a list:\n${out}")
    endif()
    if(s STREQUAL "none")
      if(NOT k EQUAL 0)
        message(FATAL_ERROR "${dump} (${setting}): ${k} draws drawing nothing, spec none")
      endif()
    else()
      if(NOT s MATCHES "^[0-6]:[0-9]+(,[0-6]:[0-9]+)*$")
        message(FATAL_ERROR "${dump} (${setting}): not a --nop spec: ${s}")
      endif()
      string(REPLACE "," ";" items "${s}")
      list(LENGTH items ni)
      if(NOT ni EQUAL k)
        message(FATAL_ERROR "${dump} (${setting}): ${k} draws drawing nothing, ${ni} in the spec")
      endif()
    endif()
    file(READ "${spec}" written)
    string(STRIP "${written}" written)
    if(NOT written STREQUAL s)
      message(FATAL_ERROR "${dump} (${setting}): --region-out wrote '${written}', the line says '${s}'")
    endif()
    # the stats without a device (the record-only renderer: the Original
    # preset at 1x, so only the orig setting's state): the same line
    if(setting STREQUAL "orig")
      execute_process(COMMAND "${TOOL}" "${dump}" - --no-device ${settings_${setting}}
                              --region-out "${dir}/nothing-nodevice.txt"
                      RESULT_VARIABLE rcNd OUTPUT_VARIABLE outNd ERROR_VARIABLE errNd)
      if(NOT rcNd EQUAL 0 OR NOT outNd MATCHES "${frameRe}")
        message(FATAL_ERROR "${dump} (${setting}): --no-device --region-out exit ${rcNd}:\n${errNd}")
      endif()
      if(NOT CMAKE_MATCH_7 STREQUAL s)
        message(FATAL_ERROR "${dump} (${setting}): without a device the spec is "
                            "'${CMAKE_MATCH_7}', with one '${s}'")
      endif()
    endif()
    # the draws that draw nothing left out: the same pictures
    execute_process(COMMAND "${TOOL}" "${dump}" - --pass-dump "${dir}/nop" ${settings_${setting}}
                            --nop "@${spec}"
                    RESULT_VARIABLE rcNop OUTPUT_VARIABLE outNop ERROR_VARIABLE errNop)
    if(NOT rcNop EQUAL 0)
      message(FATAL_ERROR "${dump} (${setting}): --nop @${spec} exit ${rcNop}:\n${errNop}")
    endif()
    file(GLOB pngs RELATIVE "${dir}/plain" "${dir}/plain/*.png")
    list(LENGTH pngs npng)
    if(npng LESS 2)
      message(FATAL_ERROR "${dump} (${setting}): ${npng} pass dumps written")
    endif()
    foreach(png IN LISTS pngs)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir}/plain/${png}"
                              "${dir}/nop/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(FATAL_ERROR "${dump} (${setting}): ${png} differs with the draws the stats say "
                            "draw nothing left out (${s})")
      endif()
    endforeach()
    message(STATUS "rd_region_stats: ${dump} (${setting}): ${draws} draws, ${tris} triangles, "
                   "${drop} dropped, ${k} draws drawing nothing; ${npng} pass dumps equal "
                   "without them")
  endforeach()
endforeach()
