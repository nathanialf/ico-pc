# rd_vu_compact_ab: the index compaction (port/render/rd_vu_compact.h;
# port/shaders/vu_tri.hlsl vu_tri_keep_cs; docs/port/PIPELINES.md "The index
# compaction") on against off, byte for byte.  The frames, two forms:
#   fixtures  -DFIXTURE_TOOL -DFIXTURES -DFIXTURE_HELPERS and LIST a fixture
#             list (port/render/test/fixtures/lists/vu_flat0.txt: a fixture,
#             the profiles it is replayed under, then its QUANTITY OP VALUE
#             words, rd_fixture_list_props.cmake): each expanded into
#             ${OUT}/fixtures (docs/port/TESTING_FIXTURES.md)
#   paths     no FIXTURE_TOOL: LIST names dumps the build writes (rd_pixel's
#             frames): each line the profiles, then a name relative to the
#             list's folder; a name that is not there is reported and skipped
# For every frame, rd_replay_tool --pass-dump with --stats twice,
# ICO_RD_VU_COMPACT=0 and =1, the record by compute dispatches in both
# (ICO_RD_VU_RECORD=compute: the only record path the compaction runs on), at
# each setting of each of its profiles, with the draw merge (its fixed raises
# through the merge build's table) and, at the first two corpus settings and
# the mali and pixel ones, without it (ICO_RD_VU_MERGE=0: the values not
# raised):
#   corpus  the device as it is: the Original preset at 4:3 and 1x, the
#           Enhanced preset at 16:9 and 2x, at 16:9 with MSAA 4x, and at
#           16:9 and 1280x720 (a fractional scale)
#   mali    issue 179's Mali-G52 on lavapipe: the Arm fakes
#           (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5) and its
#           class (ICO_RD_VU_COMPACT_ARM=1: the =1 run draws the Mali-G52's
#           form, the lists written by the CPU first and only the fillers by
#           the GPU, rd_vu_compact.h; corpus and pixel draw the other form),
#           its settings: 1x, 21:9, half height
#   pixel   the Pixel 10 Pro Fold's (ICO_VK_FAKE_LIMITS=moltenvk, Enhanced at
#           2364x1080, full height, full pixel, presented at 2364x1080)
# Every target's PNG (SCENE and DISPLAY among them) must be equal byte for
# byte; the =0 run must say "stats: VU index compaction off by default: not
# run", the =1 run "stats: VU index compaction on: N of M list 0 triangles
# left out (drop D, outside O)", with M above 0 on the mali and pixel
# profiles' frames (list 0 drawn through the record there); the mali
# profile's frames must leave triangles out (N above 0: the game's drops and
# the triangles outside the target).  COUNTS (fixtures:
# port/render/test/fixtures/lists/vu_compact_counts.txt) gives the expected
# counts by (fixture, profile, setting, merge): "NAME PROFILE SETTING MERGE N
# M D O" a line, each re-derived from a run of the fixture (the counts are
# the shader's own classes, vu_tri_keep_cs, on lavapipe).  In the fixtures
# form every pair compared must have its line and print exactly those
# numbers, and every line must be a pair this run compared; a pair without a
# line prints "rd_vu_compact_ab: count ..." (the line to copy in) and fails.
# In the paths form a pair without a line prints it only.  The path ran
# (fixtures): on every =1 run compared, the fixture's vu_compact_left_out
# (N) and region_rule_draws against their declared bounds; a fixture with no
# pair compared under one of its profiles fails.  A frame whose =0 run draws
# nothing through the record (no "stats: VU pre-pass on: [1-9]" line) is
# skipped at that setting, as is a 4:3 frame the 16:9 setting cannot show;
# every profile with a frame here must have compared targets.  No frame
# here, or no device (77): SKIP.  Vulkan only (the compute record is
# Vulkan's).
# -DTOOL=<rd_replay_tool> -DOUT=<a folder the test may write> -DLIST=<list>
# [-DCOUNTS=<counts>] [-DFIXTURE_TOOL=... -DFIXTURES=... -DFIXTURE_HELPERS=...]
cmake_policy(SET CMP0057 NEW) # if(... IN_LIST ...) in a -P script
set(BACKEND vulkan)
set(profiles corpus mali pixel)
set(entries "")
set(failures 0)
set(missing 0)
set(fixtures FALSE)
if(DEFINED FIXTURE_TOOL AND NOT "${FIXTURE_TOOL}" STREQUAL "")
  set(fixtures TRUE)
  include("${FIXTURE_HELPERS}")
  include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
  rd_fixture_list("${LIST}" fxNames)
  foreach(name IN LISTS fxNames)
    string(REGEX MATCH "^[^ \t]+" prof "${RDFX_FIELDS_${name}}")
    if(NOT prof MATCHES "^[a-z,]+$")
      message(SEND_ERROR "rd_vu_compact_ab: ${LIST}: ${name}: \"${prof}\" is not its profiles")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    string(REPLACE "," ";" profList "${prof}")
    foreach(p IN LISTS profList)
      if(NOT p IN_LIST profiles)
        message(SEND_ERROR "rd_vu_compact_ab: ${LIST}: ${name}: \"${p}\" is none of ${profiles}")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    rd_fixture_list_props(${name})
    rd_fixture_expand(${name} "${OUT}/fixtures" dump)
    list(APPEND entries "${prof}|${dump}|${name}")
  endforeach()
elseif(LIST)
  if(NOT EXISTS "${LIST}")
    message(STATUS "rd_vu_compact_ab: ${LIST}: no dump list, skipped")
  else()
    get_filename_component(listDir "${LIST}" DIRECTORY)
    file(STRINGS "${LIST}" lines)
    foreach(l IN LISTS lines)
      string(STRIP "${l}" l)
      if(l STREQUAL "" OR l MATCHES "^#")
        continue()
      endif()
      if(NOT l MATCHES "^([a-z,]+)[ \t]+(.+)$")
        message(SEND_ERROR "rd_vu_compact_ab: ${LIST}: \"${l}\" is not \"profiles name\"")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(prof "${CMAKE_MATCH_1}")
      set(path "${CMAKE_MATCH_2}")
      string(REPLACE "," ";" profList "${prof}")
      foreach(p IN LISTS profList)
        if(NOT p IN_LIST profiles)
          message(SEND_ERROR "rd_vu_compact_ab: ${LIST}: \"${p}\" is none of ${profiles}")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
      if(NOT IS_ABSOLUTE "${path}")
        set(path "${listDir}/${path}")
      endif()
      if(NOT EXISTS "${path}")
        math(EXPR missing "${missing} + 1")
        message(STATUS "rd_vu_compact_ab: ${LIST} names ${path}, which is not here: skipped")
        continue()
      endif()
      list(APPEND entries "${prof}|${path}|")
    endforeach()
  endif()
endif()
list(LENGTH entries nentries)
if(nentries EQUAL 0)
  if(failures GREATER 0)
    message(FATAL_ERROR "rd_vu_compact_ab: ${failures} failures, no frame replayed")
  endif()
  message(STATUS "SKIP: no frame is here")
  return()
endif()

set(env_corpus ICO_RD_VU_RECORD=compute)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute
             ICO_RD_VU_COMPACT_ARM=1)
set(env_pixel ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_RECORD=compute)
set(settings_corpus orig enh169 msaa frac)
set(settings_mali mali)
set(settings_pixel pixel)
set(args_orig --aspect 4:3)
set(args_enh169 --enhanced --aspect 16:9 --resolution 2x)
set(args_msaa --enhanced --aspect 16:9 --aa msaa4)
set(args_frac --enhanced --aspect 16:9 --resolution 1280x720)
set(args_mali --aspect 21:9)
set(args_pixel --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(merges_orig merge nomerge)
set(merges_enh169 merge nomerge)
set(merges_msaa merge)
set(merges_frac merge)
set(merges_mali merge nomerge)
set(merges_pixel merge nomerge)
set(env_merge "")
set(env_nomerge ICO_RD_VU_MERGE=0)
set(reqOff "stats: VU index compaction off by default: not run")
set(reqOn "stats: VU index compaction on: ([0-9]+) of ([0-9]+) list 0 triangles left out \\(drop ([0-9]+), outside ([0-9]+)\\)")
set(useRe "stats: VU pre-pass on: [1-9]")
# the expected counts (COUNTS): "NAME PROFILE SETTING MERGE N M D O"
set(expectLines "")
if(COUNTS)
  if(EXISTS "${COUNTS}")
    file(STRINGS "${COUNTS}" expectLines REGEX "^[^#]")
  elseif(fixtures)
    message(SEND_ERROR "rd_vu_compact_ab: no counts list ${COUNTS}")
    math(EXPR failures "${failures} + 1")
  endif()
endif()
set(usedLines "")
foreach(p IN LISTS profiles)
  set(asked_${p} 0)
  set(compared_${p} 0)
endforeach()
set(compared 0)
set(index 0)
foreach(e IN LISTS entries)
  string(REPLACE "|" ";" ef "${e}")
  list(GET ef 0 prof)
  list(GET ef 1 dump)
  # a frame from a dump list has no fixture name: its entry ends in an empty
  # field, which the list drops
  set(fxName "")
  list(LENGTH ef efn)
  if(efn GREATER 2)
    list(GET ef 2 fxName)
  endif()
  string(REPLACE "," ";" profList "${prof}")
  list(REMOVE_DUPLICATES profList)
  foreach(p IN LISTS profList)
    set(asked_${p} 1)
    set(fxPairs 0)
    foreach(s IN LISTS settings_${p})
      foreach(m IN LISTS merges_${s})
        math(EXPR index "${index} + 1")
        set(dirA "${OUT}/${index}-${p}-${s}-${m}-off")
        set(dirB "${OUT}/${index}-${p}-${s}-${m}-on")
        file(REMOVE_RECURSE "${dirA}" "${dirB}")
        file(MAKE_DIRECTORY "${dirA}" "${dirB}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${p}} ${env_${m}}
                                ICO_RD_VU_COMPACT=0 "${TOOL}" "${dump}" /dev/null
                                --pass-dump "${dirA}" --stats ${args_${s}}
                        RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
        if(rcA EQUAL 77)
          message(STATUS "SKIP: no device: ${errA}")
          return()
        endif()
        if(NOT rcA EQUAL 0 AND "${errA}" MATCHES "cannot show")
          message(STATUS "${dump} ${p} ${s} ${m}: skipped (${errA})")
          continue()
        endif()
        string(REPLACE ";" "," flatA "${outA}\n${errA}")
        if(rcA EQUAL 0 AND NOT flatA MATCHES "${useRe}")
          message(STATUS "${dump} ${p} ${s} ${m}: skipped (nothing drawn through the record)")
          continue()
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${p}} ${env_${m}}
                                ICO_RD_VU_COMPACT=1 "${TOOL}" "${dump}" /dev/null
                                --pass-dump "${dirB}" --stats ${args_${s}}
                        RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
        if(NOT rcA EQUAL 0 OR NOT rcB EQUAL 0)
          message(SEND_ERROR "${dump} ${p} ${s} ${m}: exit ${rcA} (=0) and ${rcB} (=1):\n${errA}\n${errB}")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        string(REPLACE ";" "," flatB "${outB}\n${errB}")
        foreach(pair "A;${reqOff}" "B;${reqOn}" "B;${useRe}")
          list(GET pair 0 run)
          list(GET pair 1 re)
          if(NOT flat${run} MATCHES "${re}")
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: run ${run} has no line matching \"${re}\"")
            math(EXPR failures "${failures} + 1")
          endif()
        endforeach()
        # the =1 run's counts: triangles judged, the mali frames' left out,
        # the expected numbers where COUNTS has them
        if(flatB MATCHES "${reqOn}")
          set(cN "${CMAKE_MATCH_1}")
          set(cM "${CMAKE_MATCH_2}")
          set(cD "${CMAKE_MATCH_3}")
          set(cO "${CMAKE_MATCH_4}")
          if(fxName STREQUAL "")
            get_filename_component(name "${dump}" NAME)
          else()
            set(name "${fxName}")
          endif()
          if(NOT p STREQUAL "corpus" AND cM EQUAL 0)
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: the compaction judged no list 0 triangle")
            math(EXPR failures "${failures} + 1")
          endif()
          if(p STREQUAL "mali" AND cN EQUAL 0)
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: no triangle left out on a Mali frame")
            math(EXPR failures "${failures} + 1")
          endif()
          set(expected "")
          foreach(x IN LISTS expectLines)
            if(x MATCHES "^${name} ${p} ${s} ${m} ")
              set(expected "${x}")
            endif()
          endforeach()
          set(got "${name} ${p} ${s} ${m} ${cN} ${cM} ${cD} ${cO}")
          if(expected STREQUAL "")
            message(STATUS "rd_vu_compact_ab: count ${got}")
            if(fixtures)
              message(SEND_ERROR "${name} ${p} ${s} ${m}: no line in ${COUNTS} (the line above)")
              math(EXPR failures "${failures} + 1")
            endif()
          else()
            list(APPEND usedLines "${expected}")
            if(NOT expected STREQUAL got)
              message(SEND_ERROR "${dump} ${p} ${s} ${m}: counts \"${got}\", expected \"${expected}\"")
              math(EXPR failures "${failures} + 1")
            endif()
          endif()
        endif()
        # the path ran: triangles left out, the camera rule's draws
        if(NOT fxName STREQUAL "")
          rd_fixture_require(${fxName} "${outB}\n${errB}" vu_compact_left_out region_rule_draws)
        endif()
        file(GLOB pngs RELATIVE "${dirA}" "${dirA}/*.png")
        foreach(need SCENE.png DISPLAY.png)
          if(NOT need IN_LIST pngs)
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: no ${need} written")
            math(EXPR failures "${failures} + 1")
          endif()
        endforeach()
        set(n 0)
        foreach(png IN LISTS pngs)
          if(NOT EXISTS "${dirB}/${png}")
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: ${png} written under ICO_RD_VU_COMPACT=0 only")
            math(EXPR failures "${failures} + 1")
            continue()
          endif()
          execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirA}/${png}"
                                  "${dirB}/${png}" RESULT_VARIABLE same)
          if(NOT same EQUAL 0)
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: ${png} differs")
            math(EXPR failures "${failures} + 1")
          endif()
          math(EXPR n "${n} + 1")
        endforeach()
        file(GLOB pngsB RELATIVE "${dirB}" "${dirB}/*.png")
        foreach(png IN LISTS pngsB)
          if(NOT EXISTS "${dirA}/${png}")
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: ${png} written under ICO_RD_VU_COMPACT=1 only")
            math(EXPR failures "${failures} + 1")
          endif()
        endforeach()
        math(EXPR compared "${compared} + ${n}")
        math(EXPR compared_${p} "${compared_${p}} + ${n}")
        if(n GREATER 0)
          math(EXPR fxPairs "${fxPairs} + 1")
        endif()
        message(STATUS "${dump} ${p} ${s} ${m}: ${n} targets compared")
        # the folders of a pair that matched are not kept (thousands of PNGs)
        if(failures EQUAL 0)
          file(REMOVE_RECURSE "${dirA}" "${dirB}")
        endif()
      endforeach()
    endforeach()
    if(NOT fxName STREQUAL "" AND fxPairs EQUAL 0)
      message(SEND_ERROR "${fxName}: no pair compared under the ${p} profile (no setting could show "
                         "it, nothing drawn through the record, or its runs failed)")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
# every expected line a pair this run compared (fixtures: no line kept for a
# pair that is gone)
if(fixtures)
  foreach(x IN LISTS expectLines)
    if(NOT x IN_LIST usedLines)
      message(SEND_ERROR "rd_vu_compact_ab: ${COUNTS}: \"${x}\" is no pair this run compared")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endif()
foreach(p IN LISTS profiles)
  if(asked_${p} AND compared_${p} EQUAL 0)
    message(SEND_ERROR "rd_vu_compact_ab: no target compared under the ${p} profile")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(fixtures)
  math(EXPR failures "${failures} + ${RDFX_FAILURES}")
endif()
message(STATUS "rd_vu_compact_ab (${BACKEND}): ${compared} targets compared over ${nentries} frames "
               "(corpus ${compared_corpus}, mali ${compared_mali}, pixel ${compared_pixel}), "
               "${missing} listed dumps not here")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vu_compact_ab: ${failures} failures")
endif()
