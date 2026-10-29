# rd_constant_reads_ab: a cleared source read as its colour against the read
# itself, byte for byte (docs/port/WORK_BUFFERS.md, "A cleared source read as
# its colour"; port/render/rd_const_read.c).  For every dump the lists name
# (tools/vustage_dumps.txt and tools/constant_reads_dumps.txt, read as
# rd_vufuse_ab.cmake reads them), under ICO_RD_BLEND=fixed, rd_replay_tool's
# --pass-dump with --stats twice: with ICO_RD_CONSTANT_READS=0 (every post
# sprite reads its source target, as before) and with the default (a sprite
# whose source holds only its last clear drawn untextured in the colour the
# texture function makes of it), at four settings:
#   phone2x   the PowerVR phone's profile (the programs' own entries,
#             ICO_RD_VU_PREPASS=0), Enhanced 2x at the dump's 2.189 aspect,
#             presented at 2364x1080;
#   phoneauto the same at the phone's Resolution Auto: the output size
#             2364x1080 with Full height and Full pixel (2366x1080 texels);
#   desk43    the desktop's path (the record), Enhanced 4:3 2x;
#   desk169   the record, Enhanced 16:9 2x.
# A dump drawn narrower than a setting's aspect cannot show it: skipped for
# it.  Every named target's PNG (PRESENT.png with --present) must be equal
# byte for byte; the A/B run must say "constant reads off" with none served,
# the default "constant reads on"; both runs must count the same target reads
# (served plus kept); and every dump of tools/constant_reads_dumps.txt (the
# phone's frames: the aura with no aura object, AURA_WORK holding its clear
# when auraInspireAfter reads it) must have some served at both phone
# settings.  Each pair's counts are printed.  A missing list or dump is named
# and skipped; no dump read, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DEXTRA=<the phone's dump list>
# -DOUT=<a folder the test may write>
set(dumps "")
set(phoneDumps "")
foreach(list IN ITEMS "${LIST}" "${EXTRA}")
  if(list STREQUAL "" OR NOT EXISTS "${list}")
    message(STATUS "${list}: no dump list, skipped")
    continue()
  endif()
  get_filename_component(listDir "${list}" DIRECTORY)
  file(STRINGS "${list}" lines)
  foreach(l IN LISTS lines)
    string(STRIP "${l}" l)
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    if(NOT IS_ABSOLUTE "${l}")
      set(l "${listDir}/${l}")
    endif()
    set(found "")
    if(l MATCHES "[*]")
      file(GLOB found "${l}")
      list(SORT found)
      if(NOT found)
        message(STATUS "${l}: no dump matches, skipped")
      endif()
    elseif(EXISTS "${l}")
      set(found "${l}")
    else()
      message(STATUS "${l}: no such dump, skipped")
    endif()
    list(APPEND dumps ${found})
    if(list STREQUAL "${EXTRA}")
      list(APPEND phoneDumps ${found})
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} or ${EXTRA} is here")
  return()
endif()

set(settings "phone2x" "phoneauto" "desk43" "desk169")
set(args_phone2x --enhanced --resolution 2x --aspect 2.189 --present 2364x1080)
set(env_phone2x "ICO_RD_VU_PREPASS=0")
set(args_phoneauto --enhanced --resolution 2364x1080 --full-height --full-pixel --aspect 2.189
                   --present 2364x1080)
set(env_phoneauto "ICO_RD_VU_PREPASS=0")
set(args_desk43 --enhanced --resolution 2x --aspect 4:3)
set(env_desk43 "")
set(args_desk169 --enhanced --resolution 2x --aspect 16:9)
set(env_desk169 "")
set(constRe "stats: constant reads (on|off): ([0-9]+) sprites drawn in a cleared source's colour, ([0-9]+) target reads kept")
set(compared 0)
set(failures 0)
foreach(s IN LISTS settings)
  set(served_${s} 0)
  set(reads_${s} 0)
endforeach()
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  list(FIND phoneDumps "${dump}" isPhone)
  foreach(s IN LISTS settings)
    set(dirOff "${OUT}/${index}-${s}-off")
    set(dirOn "${OUT}/${index}-${s}-on")
    file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
    file(MAKE_DIRECTORY "${dirOff}" "${dirOn}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ICO_RD_CONSTANT_READS=0
                            ${env_${s}} "${TOOL}" "${dump}" - --pass-dump "${dirOff}" --stats
                            ${args_${s}}
                    RESULT_VARIABLE rcOff OUTPUT_VARIABLE outOff ERROR_VARIABLE errOff)
    if(rcOff EQUAL 77)
      message(STATUS "SKIP: no device: ${errOff}")
      return()
    endif()
    if(NOT rcOff EQUAL 0 AND "${errOff}" MATCHES "cannot show")
      # a dump drawn narrower than the setting's aspect: that setting is skipped for it
      message(STATUS "${dump} ${s}: skipped (${errOff})")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_BLEND=fixed ${env_${s}} "${TOOL}"
                            "${dump}" - --pass-dump "${dirOn}" --stats ${args_${s}}
                    RESULT_VARIABLE rcOn OUTPUT_VARIABLE outOn ERROR_VARIABLE errOn)
    if(NOT rcOff EQUAL 0 OR NOT rcOn EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: exit ${rcOff} (every source read) and ${rcOn} "
                         "(constant reads):\n${errOff}\n${errOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    # the tool's lines with their ";" taken out (a ";" would split a list item)
    string(REPLACE ";" "," flatOff "${outOff}")
    string(REPLACE ";" "," flatOn "${outOn}")
    set(ok TRUE)
    foreach(run Off On)
      if(NOT flat${run} MATCHES "${constRe}")
        message(SEND_ERROR "${dump} ${s}: no constant reads line:\n${out${run}}")
        set(ok FALSE)
        break()
      endif()
      set(state${run} "${CMAKE_MATCH_1}")
      set(served${run} "${CMAKE_MATCH_2}")
      set(kept${run} "${CMAKE_MATCH_3}")
    endforeach()
    if(NOT ok)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    if(NOT stateOff STREQUAL "off" OR NOT servedOff EQUAL 0 OR NOT stateOn STREQUAL "on")
      message(SEND_ERROR "${dump} ${s}: ICO_RD_CONSTANT_READS=0 says ${stateOff} with "
                         "${servedOff} served, the default ${stateOn}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    math(EXPR readsOn "${servedOn} + ${keptOn}")
    if(NOT readsOn EQUAL keptOff)
      message(SEND_ERROR "${dump} ${s}: ${keptOff} target reads with every source read, "
                         "${readsOn} (${servedOn} served, ${keptOn} kept) with the default")
      math(EXPR failures "${failures} + 1")
    endif()
    message(STATUS "${dump} ${s}: ${servedOn} of ${keptOff} target reads served by the "
                   "source's clear")
    if(isPhone GREATER -1 AND (s STREQUAL "phone2x" OR s STREQUAL "phoneauto") AND
       servedOn EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: the phone's frame read no cleared source as its colour")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR served_${s} "${served_${s}} + ${servedOn}")
    math(EXPR reads_${s} "${reads_${s}} + ${keptOff}")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dirOn}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written with every source read only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      math(EXPR compared "${compared} + 1")
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs between the source read and its "
                           "clear's colour")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
foreach(s IN LISTS settings)
  message(STATUS "rd_constant_reads_ab ${s}: ${served_${s}} of ${reads_${s}} target reads "
                 "served by the source's clear")
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_constant_reads_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} or ${EXTRA} could be compared")
  return()
endif()
message(STATUS "rd_constant_reads_ab: ${compared} targets equal over ${ndumps} dumps at 4 "
               "settings")
