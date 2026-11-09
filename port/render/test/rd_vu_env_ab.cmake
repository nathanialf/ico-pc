# rd_vu_env_ab: two runs of rd_replay_tool's --pass-dump with --stats over the
# same dumps, under two environments, every named target's PNG equal byte for
# byte (the VU record's two writes, rd_vu_record.h: rd_vurecord_ab
# ICO_RD_VU_RECORD=targets against =stores; the pre-pass's floor:
# rd_vuprepass_min_ab, ICO_RD_VU_PREPASS_MIN unset against a floor no frame
# reaches, every VU command drawn the old way).  Each run's stdout must match
# its regex (REQ_A, REQ_B: the stats lines that say the run took the path it
# was meant to).  At the Original preset at 4:3 and 1x and the Enhanced
# preset at 16:9 and 2x, under ICO_RD_BLEND=fixed.  The dumps: DUMP (one
# file, may be absent) and those LIST names (tools/vustage_dumps.txt, as
# rd_vubatch_ab.cmake reads it); none here, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DOUT=<a folder the test may write> -DNAME=<label>
# -DENV_A=<VAR=VALUE[|VAR=VALUE]> -DENV_B=<...> -DREQ_A=<regex> -DREQ_B=<regex>
# [-DDUMP=<dump>] [-DLIST=<dump list>] [-DSKIP_UNLESS=<regex>: a dump whose
# first run prints no line matching it is skipped, not compared (the list's
# frames without VU commands: 0 pre-passes, nothing for a floor to skip)]
set(dumps "")
if(DUMP AND EXISTS "${DUMP}")
  list(APPEND dumps "${DUMP}")
endif()
if(LIST AND EXISTS "${LIST}")
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
      file(GLOB matched "${l}")
      list(SORT matched)
      list(APPEND dumps ${matched})
    elseif(EXISTS "${l}")
      list(APPEND dumps "${l}")
    endif()
  endforeach()
endif()
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump is here")
  return()
endif()
string(REPLACE "|" ";" envA "${ENV_A}")
string(REPLACE "|" ";" envB "${ENV_B}")

set(settings "orig" "enh2x169")
set(args_orig --aspect 4:3)
set(args_enh2x169 --enhanced --aspect 16:9 --resolution 2x)
set(compared 0)
set(failures 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(dirA "${OUT}/${index}-${s}-a")
    set(dirB "${OUT}/${index}-${s}-b")
    file(REMOVE_RECURSE "${dirA}" "${dirB}")
    file(MAKE_DIRECTORY "${dirA}" "${dirB}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ${envA}
                            "${TOOL}" "${dump}" /dev/null --pass-dump "${dirA}" --stats ${args_${s}}
                    RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
    if(rcA EQUAL 77)
      message(STATUS "SKIP: no device: ${errA}")
      return()
    endif()
    if(NOT rcA EQUAL 0 AND "${errA}" MATCHES "cannot show")
      message(STATUS "${dump} ${s}: skipped (${errA})")
      continue()
    endif()
    if(SKIP_UNLESS AND NOT "${outA}" MATCHES "${SKIP_UNLESS}")
      # a dump the comparison cannot speak about (the list's 2D-only frames
      # have no VU command: 0 pre-passes either way): skipped, not failed
      message(STATUS "${dump} ${s}: skipped (no line matching \"${SKIP_UNLESS}\")")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ${envB}
                            "${TOOL}" "${dump}" /dev/null --pass-dump "${dirB}" --stats ${args_${s}}
                    RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
    if(NOT rcA EQUAL 0 OR NOT rcB EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcA} (${ENV_A}) and ${rcB} (${ENV_B}):\n${errA}\n${errB}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    string(REPLACE ";" "," flatA "${outA}")
    string(REPLACE ";" "," flatB "${outB}")
    if(NOT flatA MATCHES "${REQ_A}")
      message(SEND_ERROR "${dump} ${s}: the ${ENV_A} run has no line matching \"${REQ_A}\":\n${outA}")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT flatB MATCHES "${REQ_B}")
      message(SEND_ERROR "${dump} ${s}: the ${ENV_B} run has no line matching \"${REQ_B}\":\n${outB}")
      math(EXPR failures "${failures} + 1")
    endif()
    file(GLOB pngs RELATIVE "${dirA}" "${dirA}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirB}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written under ${ENV_A} only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirA}/${png}" "${dirB}/${png}"
                      RESULT_VARIABLE same)
      math(EXPR compared "${compared} + 1")
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs between ${ENV_A} and ${ENV_B}")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "${NAME}: ${failures} failures over ${ndumps} dumps")
endif()
message(STATUS "${NAME}: ${compared} targets equal over ${ndumps} dumps at 2 settings "
               "(${ENV_A} against ${ENV_B})")
