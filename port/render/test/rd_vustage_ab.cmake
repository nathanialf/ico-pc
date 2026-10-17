# rd_vustage_ab: the per-triangle record against the old way, byte for byte
# (docs/port/RENDER.md, "The vertex and triangle pre-passes").  For every
# dump the list names (one path a line, relative to ROOT unless absolute,
# '#' comments), at the Original preset at 4:3 and 1x and at the Enhanced
# preset at 4:3 and 2x, rd_replay_tool's --pass-dump with --stats twice
# under ICO_RD_BLEND=fixed: with ICO_RD_VU_PREPASS=0 (every VU draw through
# its program's entry and vu_legacy_ps) and with the pre-passes (vu_main_vs,
# vu_particle_main_vs and vu_ps over the record); every named target's PNG
# must be equal byte for byte, the run with the pre-passes must report two
# passes (one where a dump draws no triangle) and no VU draw the old way.
# Each run's stats lines are printed as the release's structural record.  A
# missing list or dump is named and skipped; no dump read, or no device:
# SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DROOT=<repo root> -DOUT=<a folder the test may write>
if(NOT EXISTS "${LIST}")
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
file(STRINGS "${LIST}" lines)
set(dumps "")
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${ROOT}/${l}")
  endif()
  if(EXISTS "${l}")
    list(APPEND dumps "${l}")
  else()
    message(STATUS "${l}: no such dump, skipped")
  endif()
endforeach()
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()

set(settings "orig" "enh2x")
set(args_orig --aspect 4:3)
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x)
set(compared 0)
set(failures 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(dirOld "${OUT}/${index}-${s}-old")
    set(dirNew "${OUT}/${index}-${s}-new")
    file(REMOVE_RECURSE "${dirOld}" "${dirNew}")
    file(MAKE_DIRECTORY "${dirOld}" "${dirNew}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_VU_PREPASS=0
                            "${TOOL}" "${dump}" - --pass-dump "${dirOld}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOld OUTPUT_VARIABLE outOld ERROR_VARIABLE errOld)
    if(rcOld EQUAL 77)
      message(STATUS "SKIP: no device: ${errOld}")
      return()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed
                            "${TOOL}" "${dump}" - --pass-dump "${dirNew}" --stats ${args_${s}}
                    RESULT_VARIABLE rcNew OUTPUT_VARIABLE outNew ERROR_VARIABLE errNew)
    if(NOT rcOld EQUAL 0 OR NOT rcNew EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOld} (old way) and ${rcNew} (the record):\n"
                         "${errOld}\n${errNew}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    # counted as list items: a ";" in a line would split one
    string(REPLACE ";" "," flatNew "${outNew}")
    string(REGEX MATCHALL "[^\n]*: stats: [^\n]*" statsNew "${flatNew}")
    string(REPLACE ";" "\n" statsText "${statsNew}")
    message(STATUS "${dump} ${s} with the record:\n${statsText}")
    if(NOT outNew MATCHES "stats: VU pre-pass on: [12] passes, [0-9]+ vertices in [0-9]+ draws; 0 mesh and grid draws the old way")
      message(SEND_ERROR "${dump} ${s}: the pre-passes did not draw every VU command:\n${statsText}")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngs RELATIVE "${dirOld}" "${dirOld}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirNew}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written by the old way only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOld}/${png}"
                              "${dirNew}/${png}" RESULT_VARIABLE same)
      math(EXPR compared "${compared} + 1")
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs between the old way and the record")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vustage_ab: ${failures} failures over ${ndumps} dumps")
endif()
message(STATUS "rd_vustage_ab: ${compared} targets equal over ${ndumps} dumps at 2 settings")
