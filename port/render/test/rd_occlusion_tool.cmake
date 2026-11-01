# rd_occlusion_tool: rd_replay_tool --occlusion (docs/port/RENDER.md, "Culling";
# port/render/rd_occlusion.c) on the first dump of the machine-local list
# tools/occlusion_corpus.txt that is here (read as rd_vustage_ab.cmake reads
# its list).  At the Original preset at 4:3 and 1x:
#   1. --occlusion --occlusion-out SPEC --occlusion-csv CSV: the line
#      "<dump>: occlusion: hidden N of M list-0 draws, T of U triangles (index
#      count / 3); hidden commands: S" with M > 0, N <= M, T <= U, S "none" or
#      L:A[-B] items of list 0, the second line, the settings note, SPEC
#      holding S and CSV its header and a line a command;
#   2. the plain replay and the replay with --nop @SPEC, each --pass-dump with
#      --present 640x480 under ICO_RD_BLEND=fixed: DISPLAY.png and PRESENT.png
#      equal byte for byte (a hidden draw drew no pixel).
# A missing list or dump, or no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
if(NOT EXISTS "${LIST}")
  message(STATUS "SKIP: ${LIST}: no dump list")
  return()
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(dump "")
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
      list(GET matched 0 dump)
      break()
    endif()
  elseif(EXISTS "${l}")
    set(dump "${l}")
    break()
  endif()
endforeach()
if(dump STREQUAL "")
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/plain" "${OUT}/nop")
set(spec "${OUT}/hidden.txt")

execute_process(COMMAND "${TOOL}" "${dump}" "${OUT}/occlusion.png" --aspect 4:3 --occlusion
                        --occlusion-out "${spec}" --occlusion-csv "${OUT}/commands.csv"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: no device or no occlusion queries: ${err}")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${dump}: --occlusion exit ${rc}:\n${out}\n${err}")
endif()
message(STATUS "${out}")
set(lineRe "occlusion: hidden ([0-9]+) of ([0-9]+) list-0 draws, ([0-9]+) of ([0-9]+) triangles \\(index count / 3\\); hidden commands: ([^\n]+)")
if(NOT out MATCHES "${lineRe}")
  message(FATAL_ERROR "${dump}: no occlusion line:\n${out}")
endif()
set(n "${CMAKE_MATCH_1}")
set(m "${CMAKE_MATCH_2}")
set(t "${CMAKE_MATCH_3}")
set(u "${CMAKE_MATCH_4}")
set(s "${CMAKE_MATCH_5}")
if(m EQUAL 0)
  message(FATAL_ERROR "${dump}: no list-0 draw measured:\n${out}")
endif()
if(n GREATER m OR t GREATER u)
  message(FATAL_ERROR "${dump}: hidden ${n} of ${m} draws, ${t} of ${u} triangles")
endif()
if(NOT s STREQUAL "none" AND NOT s MATCHES "^0:[0-9]+(-[0-9]+)?(,0:[0-9]+(-[0-9]+)?)*$")
  message(FATAL_ERROR "${dump}: the hidden commands are not a --nop spec: ${s}")
endif()
if(NOT out MATCHES "occlusion: [0-9]+ of [0-9]+ draw commands hidden")
  message(FATAL_ERROR "${dump}: no second occlusion line:\n${out}")
endif()
if(NOT out MATCHES "note: --occlusion: the replays run under ICO_RD_VU_MERGE=0, ICO_RD_BLEND=fixed")
  message(FATAL_ERROR "${dump}: the settings note is missing:\n${out}")
endif()
file(STRINGS "${OUT}/commands.csv" csv)
list(LENGTH csv ncsv)
list(GET csv 0 csvHead)
if(NOT csvHead STREQUAL "cmd,type,draws,counted,triangles,samples" OR ncsv LESS 2)
  message(FATAL_ERROR "${dump}: --occlusion-csv wrote ${ncsv} lines, the first '${csvHead}'")
endif()
file(READ "${spec}" written)
string(STRIP "${written}" written)
if(NOT written STREQUAL s)
  message(FATAL_ERROR "${dump}: --occlusion-out wrote '${written}', the line says '${s}'")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed "${TOOL}" "${dump}" -
                        --pass-dump "${OUT}/plain" --present 640x480 --aspect 4:3
                RESULT_VARIABLE rcPlain OUTPUT_VARIABLE outPlain ERROR_VARIABLE errPlain)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed "${TOOL}" "${dump}" -
                        --pass-dump "${OUT}/nop" --present 640x480 --aspect 4:3 --nop "@${spec}"
                RESULT_VARIABLE rcNop OUTPUT_VARIABLE outNop ERROR_VARIABLE errNop)
if(NOT rcPlain EQUAL 0 OR NOT rcNop EQUAL 0)
  message(FATAL_ERROR "${dump}: --pass-dump exit ${rcPlain} (plain) and ${rcNop} (the hidden "
                      "commands NOPs):\n${errPlain}\n${errNop}")
endif()
foreach(png DISPLAY.png PRESENT.png)
  if(NOT EXISTS "${OUT}/plain/${png}" OR NOT EXISTS "${OUT}/nop/${png}")
    message(FATAL_ERROR "${dump}: ${png} not written by both replays")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/plain/${png}"
                          "${OUT}/nop/${png}" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    message(FATAL_ERROR "${dump}: ${png} differs with the hidden commands NOPs (${s})")
  endif()
endforeach()
message(STATUS "rd_occlusion_tool: ${dump}: hidden ${n} of ${m} list-0 draws, ${t} of ${u} "
               "triangles; DISPLAY and PRESENT equal with them left out")
