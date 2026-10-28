# rd_vubatch_ab: the pre-pass's batches against one draw an entry, byte for
# byte (docs/port/VU1_PROGRAMS.md, "The pre-pass's batches").  For every dump
# the list names (tools/vustage_dumps.txt, as rd_vustage_ab.cmake reads it),
# at the Original preset at 4:3 and 1x, the Enhanced preset at 4:3 and 2x
# and at 16:9 and 2x, rd_replay_tool's --pass-dump with --stats twice under
# ICO_RD_BLEND=fixed, both with the pre-passes: with
# ICO_RD_VU_PREPASS_BATCH=0 (one point draw an entry and stage, the
# vu_X_pre_vs builds reading VuCB and VuBoneCB as uniform blocks) and with
# the batches (one point draw a run of entries, the vu_X_batch_pre_vs builds
# reading them from the ring).  Every named target's PNG must be equal byte
# for byte (the record the main shaders read, so every target after it);
# both runs must draw every VU command through the record (no mesh or grid
# draw the old way) with the same vertices; the batched run must make no
# more pre-pass draws, and fewer wherever the one-an-entry run made two or
# more.  Each run's pre-pass line is printed (the draws before and after).
# A missing list or dump is named and skipped; no dump read, or no device:
# SKIP (77).
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
    if(matched)
      list(APPEND dumps ${matched})
    else()
      message(STATUS "${l}: no dump matches, skipped")
    endif()
  elseif(EXISTS "${l}")
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

set(settings "orig" "enh2x" "enh2x169")
set(args_orig --aspect 4:3)
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x)
set(args_enh2x169 --enhanced --aspect 16:9 --resolution 2x)
set(prepassRe "stats: VU pre-pass on: [012] passes, ([0-9]+) vertices in ([0-9]+) draws, 0 mesh and grid draws the old way")
set(compared 0)
set(failures 0)
set(drawsOne 0)
set(drawsBatched 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(dirOne "${OUT}/${index}-${s}-one")
    set(dirBatch "${OUT}/${index}-${s}-batch")
    file(REMOVE_RECURSE "${dirOne}" "${dirBatch}")
    file(MAKE_DIRECTORY "${dirOne}" "${dirBatch}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_VU_PREPASS_BATCH=0
                            "${TOOL}" "${dump}" /dev/null --pass-dump "${dirOne}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOne OUTPUT_VARIABLE outOne ERROR_VARIABLE errOne)
    if(rcOne EQUAL 77)
      message(STATUS "SKIP: no device: ${errOne}")
      return()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed
                            "${TOOL}" "${dump}" /dev/null --pass-dump "${dirBatch}" --stats ${args_${s}}
                    RESULT_VARIABLE rcBatch OUTPUT_VARIABLE outBatch ERROR_VARIABLE errBatch)
    if(NOT rcOne EQUAL 0 AND "${errOne}" MATCHES "cannot show")
      # a dump drawn at 4:3 cannot be replayed at 16:9: that setting is skipped for it
      message(STATUS "${dump} ${s}: skipped (${errOne})")
      continue()
    endif()
    if(NOT rcOne EQUAL 0 OR NOT rcBatch EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOne} (one draw an entry) and ${rcBatch} "
                         "(batched):\n${errOne}\n${errBatch}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    # the pre-pass lines: "; 0 mesh ..." in the tool's text, matched with
    # the ";" taken out (a ";" would split a list item)
    string(REPLACE ";" "," flatOne "${outOne}")
    string(REPLACE ";" "," flatBatch "${outBatch}")
    if(NOT flatOne MATCHES "${prepassRe}")
      message(SEND_ERROR "${dump} ${s}: one draw an entry did not draw every VU command through "
                         "the record:\n${outOne}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(vertsOne "${CMAKE_MATCH_1}")
    set(nOne "${CMAKE_MATCH_2}")
    if(NOT flatBatch MATCHES "${prepassRe}")
      message(SEND_ERROR "${dump} ${s}: the batches did not draw every VU command through the "
                         "record:\n${outBatch}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(vertsBatch "${CMAKE_MATCH_1}")
    set(nBatch "${CMAKE_MATCH_2}")
    message(STATUS "${dump} ${s}: ${vertsOne} vertices, pre-pass draws ${nOne} one an entry, "
                   "${nBatch} batched")
    math(EXPR drawsOne "${drawsOne} + ${nOne}")
    math(EXPR drawsBatched "${drawsBatched} + ${nBatch}")
    if(NOT vertsOne EQUAL vertsBatch)
      message(SEND_ERROR "${dump} ${s}: ${vertsOne} vertices one an entry, ${vertsBatch} batched")
      math(EXPR failures "${failures} + 1")
    endif()
    if(nBatch GREATER nOne OR (nOne GREATER 1 AND NOT nBatch LESS nOne))
      message(SEND_ERROR "${dump} ${s}: ${nBatch} batched pre-pass draws against ${nOne} one an "
                         "entry")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngs RELATIVE "${dirOne}" "${dirOne}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirBatch}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written one draw an entry only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOne}/${png}"
                              "${dirBatch}/${png}" RESULT_VARIABLE same)
      math(EXPR compared "${compared} + 1")
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs between one draw an entry and the "
                           "batches")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vubatch_ab: ${failures} failures over ${ndumps} dumps")
endif()
message(STATUS "rd_vubatch_ab: ${compared} targets equal over ${ndumps} dumps at 3 settings, "
               "pre-pass draws ${drawsOne} one an entry, ${drawsBatched} batched")
