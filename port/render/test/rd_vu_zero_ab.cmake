# rd_vu_zero_ab: the zero-area cull (port/render/rd_vu_zero.c, rd_vu_zero.h;
# docs/port/PIPELINES.md, "The zero-area cull"; issue 179): the draws culling
# the triangles that cover no sample point (ICO_RD_VU_ZERO=1: on without the
# self-test, every entry of every replay checked against the CPU's answers)
# against the cull off (=0), byte for byte.  The frames: every fixture of
# LIST (port/render/test/fixtures/lists/vu_zero.txt: a fixture, the profiles
# it is replayed under, then its QUANTITY OP VALUE words,
# rd_fixture_list_props.cmake), each expanded into ${OUT}/fixtures
# (docs/port/TESTING_FIXTURES.md).  For every frame, rd_replay_tool
# --pass-dump with --stats twice, ICO_RD_VU_ZERO=0 and =1, at each setting
# of each of its profiles, each with the draw merge and without it
# (ICO_RD_VU_MERGE=0):
#   corpus   the device as it is: the Original preset at 4:3 and 1x, the
#            same with Full pixel on (--full-pixel: the reduction draws the
#            whole frame, no border crop), the
#            Enhanced preset at 16:9 and at 4:3, both 2x (the lattice of
#            two samples a GS pixel each way; the 4:3 one for the frames
#            narrower than 16:9), and the Original preset with the motion
#            vectors (--velocity, its PNG in the folder: the velocity pass
#            reads the same slots without the cull)
#   mali     issue 179's Mali-G52 on lavapipe: the Arm fakes
#            (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5), the record
#            by compute dispatches as on that phone (ICO_RD_VU_RECORD=compute),
#            1x, 21:9
#   mali169  the same at the reporter's v0.11.13 settings: 1x, 16:9, half
#            height (the tool's default)
# With BACKEND other than vulkan (the OpenGL twin) every frame takes the corpus
# settings alone (the fakes are Vulkan's).  Every target's PNG (SCENE and
# DISPLAY among them) must be equal byte for byte; the =0 run must say "stats:
# VU zero-area cull off (ICO_RD_VU_ZERO=0)" and the =1 run "stats: VU
# zero-area cull on (ICO_RD_VU_ZERO=1): C of S triangle slots culled over P
# pictures checked, 0 differ" with P at least 1 (the GPU's flags the CPU's
# for every slot read back); the merged runs "merge on)", the others "merge
# off)".  A frame whose =0 run draws nothing through the record (no "stats:
# VU pre-pass on: [1-9]" line) is skipped at that setting, as is a 4:3 frame
# the 16:9 setting cannot show; every profile with a frame here must have
# compared targets.  Every =1 run compared prints its cull line (the slots
# culled, the draws culling, the refusals by reason).  The path ran: for
# each fixture and each of its profiles, the triangle slots culled summed
# over that profile's runs against the fixture's declared vu_zero_culled
# (one lattice of a profile may find nothing in a frame: 2x samples twice
# as finely as 1x), printed with the refusals summed by reason; and over the
# committed list, at least one triangle culled at the 2x lattice (the
# enh169 and enh43 runs together).  A fixture with no pair compared under
# one of its profiles fails.
# MODE=fault (rd_vu_zero_fault): the tests' check's way back instead (the
# ICO_RD_VU_ZERO=1 check; there is none in play): each
# frame once under the mali profile (a frame not listed for it: the corpus
# profile's Original 4:3 setting) with ICO_RD_VU_ZERO=1 and
# ICO_RD_VU_ZERO_SABOTAGE=check (the CPU's answers from no lattice, so every
# culled triangle differs): the log must say "rd: VU zero-area cull: off for
# the run: the GPU's answer differs from the CPU's" and the stats line "VU
# zero-area cull off (for the run: ".
# No frame here, or no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DOUT=<a folder the test may write> -DLIST=<list>
# -DFIXTURE_TOOL=... -DFIXTURES=... -DFIXTURE_HELPERS=...
# [-DBACKEND=vulkan|opengl (default vulkan)] [-DMODE=fault]
cmake_policy(SET CMP0057 NEW) # if(... IN_LIST ...) in a -P script
# an entry's empty name field (a path list's "prof|dump|") kept as an element:
# CMake 4 has no OLD behaviour, the Windows runner's CMake 3 dropped it and
# list(GET ef 2) failed (CI run 43805687280, windows job, D3D11 on WARP)
cmake_policy(SET CMP0007 NEW)
if(NOT BACKEND)
  set(BACKEND vulkan)
endif()
set(profiles corpus mali mali169)
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
    if(NOT prof MATCHES "^[a-z0-9,]+$")
      message(SEND_ERROR "rd_vu_zero_ab: ${LIST}: ${name}: \"${prof}\" is not its profiles")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    string(REPLACE "," ";" profList "${prof}")
    foreach(p IN LISTS profList)
      if(NOT p IN_LIST profiles)
        message(SEND_ERROR "rd_vu_zero_ab: ${LIST}: ${name}: \"${p}\" is none of ${profiles}")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    rd_fixture_list_props(${name})
    rd_fixture_expand(${name} "${OUT}/fixtures" dump)
    list(APPEND entries "${prof}|${dump}|${name}")
  endforeach()
elseif(LIST)
  if(NOT EXISTS "${LIST}")
    message(STATUS "rd_vu_zero_ab: ${LIST}: no dump list, skipped")
  else()
    get_filename_component(listDir "${LIST}" DIRECTORY)
    file(STRINGS "${LIST}" lines)
    foreach(l IN LISTS lines)
      string(STRIP "${l}" l)
      if(l STREQUAL "" OR l MATCHES "^#")
        continue()
      endif()
      if(NOT l MATCHES "^([a-z0-9,]+)[ \t]+(.+)$")
        message(SEND_ERROR "rd_vu_zero_ab: ${LIST}: \"${l}\" is not \"profiles name\"")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      set(prof "${CMAKE_MATCH_1}")
      set(path "${CMAKE_MATCH_2}")
      string(REPLACE "," ";" profList "${prof}")
      foreach(p IN LISTS profList)
        if(NOT p IN_LIST profiles)
          message(SEND_ERROR "rd_vu_zero_ab: ${LIST}: \"${p}\" is none of ${profiles}")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
      if(NOT IS_ABSOLUTE "${path}")
        set(path "${listDir}/${path}")
      endif()
      if(NOT EXISTS "${path}")
        math(EXPR missing "${missing} + 1")
        message(STATUS "rd_vu_zero_ab: ${LIST} names ${path}, which is not here: skipped")
        continue()
      endif()
      list(APPEND entries "${prof}|${path}|")
    endforeach()
  endif()
endif()
list(LENGTH entries nentries)
if(nentries EQUAL 0)
  if(failures GREATER 0)
    message(FATAL_ERROR "rd_vu_zero_ab: ${failures} failures, no frame replayed")
  endif()
  message(STATUS "SKIP: no frame is here")
  return()
endif()

set(env_corpus "")
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute)
set(env_mali169 ${env_mali})
set(settings_corpus orig origfp enh169 enh43 vel)
set(settings_mali mali)
set(settings_mali169 m169)
set(args_orig --aspect 4:3)
set(args_origfp --aspect 4:3 --full-pixel)
set(args_enh169 --enhanced --aspect 16:9 --resolution 2x)
set(args_enh43 --enhanced --aspect 4:3 --resolution 2x)
set(args_vel --aspect 4:3 --velocity)
set(args_mali --aspect 21:9)
set(args_m169 --aspect 16:9)
set(merges merge nomerge)
set(env_merge "")
set(env_nomerge ICO_RD_VU_MERGE=0)
set(req_merge "merge on\\)")
set(req_nomerge "merge off\\)")
set(reqOff "stats: VU zero-area cull off \\(ICO_RD_VU_ZERO=0\\)")
set(reqOn "stats: VU zero-area cull on \\(ICO_RD_VU_ZERO=1\\): [0-9]+ of [0-9]+ triangle slots culled over [1-9][0-9]* pictures checked, 0 differ")
set(reqFaultLog "rd: VU zero-area cull: off for the run: the GPU's answer differs from the CPU's in [1-9]")
set(reqFaultStats "stats: VU zero-area cull off \\(for the run: ")
# the =1 run's cull line (rd_vu_zero_run.c rd__vu_zero_stats_text) and its
# refusals by reason (rd_vu_zero.c rd_vu_zero_reason_name, in that order,
# spaces dropped); matched in the run's text with its ';' made ','
set(cullRe "stats: (VU zero-area cull on [^\n]*)")
set(cullNumRe "VU zero-area cull on [^:]*: ([0-9]+) of ([0-9]+) triangle slots culled over [0-9]+ pictures checked, [0-9]+ differ, ([0-9]+) draws culling \\(([0-9]+) triangles, ([0-9]+) shared lists given up\\), refused: ([a-z0-9 ,]*), zero-area modules made")
set(reasons off mapping scale jitter offgrid multisampled velocity ownentry)
set(culled2x 0)
if(MODE STREQUAL "fault")
  set(faults 0)
  foreach(e IN LISTS entries)
    string(REPLACE "|" ";" ef "${e}")
    list(GET ef 0 prof)
    list(GET ef 1 dump)
    string(REPLACE "," ";" profList "${prof}")
    set(fp corpus)
    set(fs orig)
    if(mali IN_LIST profList)
      set(fp mali)
      set(fs mali)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${fp}} ICO_RD_VU_ZERO=1
                            ICO_RD_VU_ZERO_SABOTAGE=check "${TOOL}" "${dump}" /dev/null --stats
                            ${args_${fs}}
                    RESULT_VARIABLE rcF OUTPUT_VARIABLE outF ERROR_VARIABLE errF)
    if(rcF EQUAL 77)
      message(STATUS "SKIP: no device: ${errF}")
      return()
    endif()
    string(REPLACE ";" "," flatF "${outF}\n${errF}")
    if(NOT rcF EQUAL 0)
      message(SEND_ERROR "${dump}: exit ${rcF}:\n${errF}")
      math(EXPR failures "${failures} + 1")
    elseif(NOT flatF MATCHES "${reqFaultLog}" OR NOT flatF MATCHES "${reqFaultStats}")
      message(SEND_ERROR "${dump}: the check's way back not taken (no \"${reqFaultLog}\" and \"${reqFaultStats}\"):\n${outF}\n${errF}")
      math(EXPR failures "${failures} + 1")
    else()
      math(EXPR faults "${faults} + 1")
    endif()
  endforeach()
  if(fixtures)
    math(EXPR failures "${failures} + ${RDFX_FAILURES}")
  endif()
  message(STATUS "rd_vu_zero_fault: the way back taken on ${faults} of ${nentries} frames")
  if(failures GREATER 0)
    message(FATAL_ERROR "rd_vu_zero_fault: ${failures} failures")
  endif()
  return()
endif()
set(useRe "stats: VU pre-pass on: [1-9]")
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
  list(GET ef 2 name)
  string(REPLACE "," ";" profList "${prof}")
  if(NOT BACKEND STREQUAL "vulkan")
    set(profList corpus)
  endif()
  list(REMOVE_DUPLICATES profList)
  foreach(p IN LISTS profList)
    set(asked_${p} 1)
    set(fxPairs 0)
    # the profile's sums over its runs (the path ran; the refusals by reason)
    set(pCulled 0)
    set(pSlots 0)
    set(pDraws 0)
    set(pTris 0)
    set(pShared 0)
    foreach(r IN LISTS reasons)
      set(pRef_${r} 0)
    endforeach()
    foreach(s IN LISTS settings_${p})
      foreach(m IN LISTS merges)
        math(EXPR index "${index} + 1")
        set(dirA "${OUT}/${index}-${p}-${s}-${m}-orig")
        set(dirB "${OUT}/${index}-${p}-${s}-${m}-zero")
        file(REMOVE_RECURSE "${dirA}" "${dirB}")
        file(MAKE_DIRECTORY "${dirA}" "${dirB}")
        # the motion vectors' picture beside the targets (compared with them)
        set(velA "")
        set(velB "")
        if(s STREQUAL "vel")
          set(velA --velocity-dump "${dirA}/velocity.png")
          set(velB --velocity-dump "${dirB}/velocity.png")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${p}} ${env_${m}}
                                ICO_RD_VU_ZERO=0 "${TOOL}" "${dump}" /dev/null
                                --pass-dump "${dirA}" --stats ${args_${s}} ${velA}
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
                                ICO_RD_VU_ZERO=1 "${TOOL}" "${dump}" /dev/null
                                --pass-dump "${dirB}" --stats ${args_${s}} ${velB}
                        RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
        if(NOT rcA EQUAL 0 OR NOT rcB EQUAL 0)
          message(SEND_ERROR "${dump} ${p} ${s} ${m}: exit ${rcA} (=0) and ${rcB} (=1):\n${errA}\n${errB}")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        string(REPLACE ";" "," flatB "${outB}\n${errB}")
        # the Direct3D 11 debug layer's findings (tools/ci_d3d11_warp.sh runs
        # this script on WARP with ICO_D3D11_DEBUG=1)
        if("${errA}${errB}" MATCHES "rhi_d3d11 debug (error|corruption)[^\n]*")
          message(SEND_ERROR "${dump} ${p} ${s} ${m}: ${CMAKE_MATCH_0}")
          math(EXPR failures "${failures} + 1")
        endif()
        foreach(pair "A;${reqOff}" "A;${req_${m}}" "B;${reqOn}" "B;${req_${m}}" "B;${useRe}")
          list(GET pair 0 run)
          list(GET pair 1 re)
          if(NOT flat${run} MATCHES "${re}")
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: run ${run} has no line matching \"${re}\"")
            math(EXPR failures "${failures} + 1")
          endif()
        endforeach()
        # the run's cull line, and its numbers into the profile's sums
        if(flatB MATCHES "${cullRe}")
          message(STATUS "${dump} ${p} ${s} ${m}: ${CMAKE_MATCH_1}")
        endif()
        if(flatB MATCHES "${cullNumRe}")
          # the groups kept before another MATCHES replaces them
          set(rCulled "${CMAKE_MATCH_1}")
          set(rRefused "${CMAKE_MATCH_6}")
          math(EXPR pCulled "${pCulled} + ${rCulled}")
          math(EXPR pSlots "${pSlots} + ${CMAKE_MATCH_2}")
          math(EXPR pDraws "${pDraws} + ${CMAKE_MATCH_3}")
          math(EXPR pTris "${pTris} + ${CMAKE_MATCH_4}")
          math(EXPR pShared "${pShared} + ${CMAKE_MATCH_5}")
          if(s STREQUAL "enh169" OR s STREQUAL "enh43")
            math(EXPR culled2x "${culled2x} + ${rCulled}")
          endif()
          string(REPLACE ", " "|" refList "${rRefused}")
          string(REPLACE "|" ";" refList "${refList}")
          foreach(rw IN LISTS refList)
            if(rw MATCHES "^([a-z ]+) ([0-9]+)$")
              string(REPLACE " " "" rk "${CMAKE_MATCH_1}")
              if(DEFINED pRef_${rk})
                math(EXPR pRef_${rk} "${pRef_${rk}} + ${CMAKE_MATCH_2}")
              endif()
            endif()
          endforeach()
        else()
          message(SEND_ERROR "${dump} ${p} ${s} ${m}: run B's cull line is not \"${cullNumRe}\"")
          math(EXPR failures "${failures} + 1")
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
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: ${png} written under ICO_RD_VU_ZERO=0 only")
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
            message(SEND_ERROR "${dump} ${p} ${s} ${m}: ${png} written under ICO_RD_VU_ZERO=1 only")
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
    if(NOT name STREQUAL "" AND fxPairs EQUAL 0)
      message(SEND_ERROR "${name}: no pair compared under the ${p} profile (no setting could show it, "
                         "nothing drawn through the record, or its runs failed)")
      math(EXPR failures "${failures} + 1")
    endif()
    set(refText "")
    foreach(r IN LISTS reasons)
      string(APPEND refText " ${r} ${pRef_${r}}")
    endforeach()
    message(STATUS "${dump} ${p}: over ${fxPairs} pairs: ${pCulled} of ${pSlots} triangle slots "
                   "culled; ${pDraws} draws culling (${pTris} triangles, ${pShared} shared lists "
                   "given up); refused:${refText}")
    # the path ran: the profile's culled slots summed over its runs
    if(NOT name STREQUAL "" AND fxPairs GREATER 0)
      rd_fixture_require(${name}
                         "VU zero-area cull on (ICO_RD_VU_ZERO=1): ${pCulled} of ${pSlots} triangle slots culled"
                         vu_zero_culled)
    endif()
  endforeach()
endforeach()
foreach(p IN LISTS profiles)
  if(asked_${p} AND compared_${p} EQUAL 0)
    message(SEND_ERROR "rd_vu_zero_ab: no target compared under the ${p} profile")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(fixtures)
  math(EXPR failures "${failures} + ${RDFX_FAILURES}")
  # the 2x lattice found something to cull in the committed frames
  if(culled2x EQUAL 0)
    message(SEND_ERROR "rd_vu_zero_ab: no triangle slot culled at 2x (the enh169 and enh43 runs) "
                       "in any committed frame: the 2x lattice is not shown culling")
    math(EXPR failures "${failures} + 1")
  endif()
endif()
message(STATUS "rd_vu_zero_ab (${BACKEND}): ${compared} targets compared over ${nentries} frames "
               "(corpus ${compared_corpus}, mali ${compared_mali}, mali169 ${compared_mali169}), "
               "${culled2x} triangle slots culled at 2x, ${missing} listed dumps not here")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vu_zero_ab: ${failures} failures")
endif()
