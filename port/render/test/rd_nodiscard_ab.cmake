# rd_nodiscard_ab: the discard-free twins (port/render/rd_nodiscard.h;
# docs/port/PIPELINES.md, "The discard-free twins"; issue 179) give the same
# picture byte for byte as the *_nodual builds they stand in for, and the
# passes drew through them.  The frames: every fixture of LIST
# (port/render/test/fixtures/lists/nodiscard.txt: a fixture and the profiles
# it is replayed under), each expanded into ${OUT}/fixtures
# (docs/port/TESTING_FIXTURES.md).  For every frame, at each setting of each
# of its profiles, rd_replay_tool --pass-dump --stats --repeat 2 twice:
#   off  ICO_RD_VU_NODISCARD=0: every pass through its *_nodual build
#   on   ICO_RD_VU_NODISCARD=1: on without the self-test; the first replay
#        asks for the twins' pipelines, the second replay's start makes the
#        whole queue, a batch at a time, and waits (rd__nd_replay_begin,
#        rd_pipeline.c rd__pipeline_twin_tick), so the second, whose targets
#        are written, draws through a twin every pass the CPU gives one whose
#        pipeline could be made (the run's line prints "not made yet": the
#        first replay's asks)
# The profiles (Vulkan):
#   corpus   the device as it is (lavapipe blends with two outputs: no pass
#            draws through a *_nodual build, so none through a twin; the run
#            must say so), the Original preset at 4:3 and 1x
#   mali     issue 179's Mali-G52 on lavapipe: the Arm fakes
#            (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5), the record
#            by compute dispatches, and no dual-source blending as on that
#            phone (ICO_VK_FAKE_NO_DUAL=1: its log says "no dualSrcBlend"),
#            1x, 21:9
#   mali169  the same at 16:9 (the reporter's v0.11.14 setting)
# With BACKEND opengl (rd_nodiscard_ab_gl) every frame at the corpus setting
# with no dual-source blending (ICO_GL_FAKE_NO_DUAL=1).  Without the
# self-test no clipper check runs, so only clip mode none takes a twin
# (rd_nodiscard.h, "Why clip mode none"): these runs take the path of a
# phone whose clipper check was not clean; MODE=clean (below) the path of
# one whose check was clean.  Every target's PNG of on and off must
# be equal byte for byte, each run's stats line read: off says "discard-free
# twins off (ICO_RD_VU_NODISCARD=0): 0 draws through them" and "0 modules of
# them made"; on under a profile without dual-source blending "discard-free
# twins on (ICO_RD_VU_NODISCARD=1): N draws through them" with N above 0 over
# each fixture and profile (the twins RAN; the count printed), under corpus
# N 0 and "not applicable here".
# No frame here, or no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DOUT=<a folder the test may write> -DLIST=<list>
# -DFIXTURE_TOOL=... -DFIXTURES=... -DFIXTURE_HELPERS=...
# [-DBACKEND=vulkan|opengl (default vulkan)]
cmake_policy(SET CMP0057 NEW) # if(... IN_LIST ...) in a -P script
cmake_policy(SET CMP0007 NEW) # an empty list field kept
if(NOT BACKEND)
  set(BACKEND vulkan)
endif()
set(entries "")
set(failures 0)
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
rd_fixture_list("${LIST}" fxNames)
foreach(name IN LISTS fxNames)
  string(REGEX MATCH "^[^ \t]+" prof "${RDFX_FIELDS_${name}}")
  if(NOT prof MATCHES "^[a-z0-9,]+$")
    message(SEND_ERROR "rd_nodiscard_ab: ${LIST}: ${name}: \"${prof}\" is not its profiles")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(NOT BACKEND STREQUAL "vulkan")
    set(prof "gl")
  endif()
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)
  list(APPEND entries "${prof}|${dump}|${name}")
endforeach()
list(LENGTH entries nentries)
if(nentries EQUAL 0)
  if(failures GREATER 0)
    message(FATAL_ERROR "rd_nodiscard_ab: ${failures} failures, no frame replayed")
  endif()
  message(STATUS "SKIP: no frame is here")
  return()
endif()

set(env_corpus "")
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute
             ICO_VK_FAKE_NO_DUAL=1)
set(env_mali169 ${env_mali})
set(env_gl ICO_GL_FAKE_NO_DUAL=1)
# own: the fixture at the tool's default settings and its own aspect, no
# dual-source blending (the reproduction of Mesa's NaN, fx-drops-4657)
set(env_own ICO_VK_FAKE_NO_DUAL=1)
set(settings_own own)
set(args_own "")
set(nodual_own 1)
set(settings_corpus orig)
set(settings_mali mali)
set(settings_mali169 m169)
set(settings_gl orig)
set(args_orig --aspect 4:3)
set(args_mali --aspect 21:9)
set(args_m169 --aspect 16:9)
set(nodual_corpus 0)
set(nodual_mali 1)
set(nodual_mali169 1)
set(nodual_gl 1)
# the twins' run line (rd_nodiscard_run.c rd__nd_stats_text), matched in the
# run's text with its ';' made ','
set(ndRe "stats: discard-free twins (on|off|held)[^:]*: ([0-9]+) draws through them, ([0-9]+) of them by the clipper check \\(([a-z ]+)\\), refused: ([^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*), ([0-9]+) modules of them made")
# MODE=clean (rd_nodiscard_clean): the clean verdict's path.  Lavapipe
# measures its clipper not clean (rd_nodiscard_selftest.c: Mesa's NaN band
# flag on triangles crossing the eye plane), so the run with the twins takes
# the verdict as clean (ICO_RD_VU_NODISCARD_CLIPPER=clean) and the camera
# rule's passes take a twin.  These fixtures hold no eye-crossing triangle
# whose pixels differ (measured: 0 targets differing in every run so far),
# which is why a verdict taken as clean is byte for byte here.  Required:
# the verdict "clean", every target byte for byte, and per fixture and
# profile at least the clean_min=N of its list line of that class's draws
# (a floor below what was measured).
set(extraOn "")
if(MODE STREQUAL "clean")
  set(extraOn ICO_RD_VU_NODISCARD_CLIPPER=clean)
endif()

# a run of a frame: rc, its text flattened, the twins' draws and modules
function(ndRun dump p s value dir)
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  set(more "")
  if(value EQUAL 1)
    set(more ${extraOn})
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${p}} ${more} ICO_RD_VU_NODISCARD=${value}
                          "${TOOL}" "${dump}" /dev/null --pass-dump "${dir}" --stats --repeat 2
                          ${args_${s}}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REPLACE ";" "," flat "${out}\n${err}")
  set(state "")
  set(draws -1)
  set(refused "")
  set(made -1)
  set(clipDraws -1)
  set(clipper "")
  if(flat MATCHES "${ndRe}")
    set(state "${CMAKE_MATCH_1}")
    set(draws "${CMAKE_MATCH_2}")
    set(clipDraws "${CMAKE_MATCH_3}")
    set(clipper "${CMAKE_MATCH_4}")
    set(refused "${CMAKE_MATCH_5}")
    set(made "${CMAKE_MATCH_6}")
  endif()
  set(r_clip "${clipDraws}" PARENT_SCOPE)
  set(r_clipper "${clipper}" PARENT_SCOPE)
  set(r_rc "${rc}" PARENT_SCOPE)
  set(r_flat "${flat}" PARENT_SCOPE)
  set(r_err "${err}" PARENT_SCOPE)
  set(r_state "${state}" PARENT_SCOPE)
  set(r_draws "${draws}" PARENT_SCOPE)
  set(r_refused "${refused}" PARENT_SCOPE)
  set(r_made "${made}" PARENT_SCOPE)
endfunction()

set(compared 0)
set(twinTotal 0)
set(cleanDiffers 0)
set(index 0)
foreach(e IN LISTS entries)
  string(REPLACE "|" ";" ef "${e}")
  list(GET ef 0 prof)
  list(GET ef 1 dump)
  list(GET ef 2 name)
  string(REPLACE "," ";" profList "${prof}")
  list(REMOVE_DUPLICATES profList)
  foreach(p IN LISTS profList)
    if(NOT DEFINED settings_${p})
      message(SEND_ERROR "${name}: no profile \"${p}\" here")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    set(fxTwins 0)
    set(fxCompared 0)
    foreach(s IN LISTS settings_${p})
      math(EXPR index "${index} + 1")
      set(dirOff "${OUT}/${index}-${p}-${s}-off")
      set(dirOn "${OUT}/${index}-${p}-${s}-on")
      set(ok TRUE)
      foreach(v 0 1)
        if(v EQUAL 0)
          set(dir "${dirOff}")
        else()
          set(dir "${dirOn}")
        endif()
        ndRun("${dump}" ${p} ${s} ${v} "${dir}")
        if(r_rc EQUAL 77)
          message(STATUS "SKIP: no device: ${r_err}")
          return()
        endif()
        if(NOT r_rc EQUAL 0 AND "${r_err}" MATCHES "cannot show")
          message(STATUS "${dump} ${p} ${s}: skipped (${r_err})")
          set(ok FALSE)
          break()
        endif()
        if(NOT r_rc EQUAL 0)
          message(SEND_ERROR "${dump} ${p} ${s} =${v}: exit ${r_rc}:\n${r_err}")
          math(EXPR failures "${failures} + 1")
          set(ok FALSE)
          break()
        endif()
        message(STATUS "${dump} ${p} ${s} ICO_RD_VU_NODISCARD=${v}: twins ${r_state}, ${r_draws} draws through them, ${r_clip} by the clipper check (${r_clipper}) (refused: ${r_refused}), ${r_made} modules made")
        if(v EQUAL 0)
          if(NOT r_state STREQUAL "off" OR NOT r_draws EQUAL 0 OR NOT r_made EQUAL 0)
            message(SEND_ERROR "${dump} ${p} ${s}: with the twins off something of them ran (${r_state}, ${r_draws} draws, ${r_made} modules):\n${r_flat}")
            math(EXPR failures "${failures} + 1")
          endif()
        elseif(nodual_${p})
          # --repeat 2: the first replay asks for every pass's twin ("not
          # made yet"), the second draws each through it (the waited path
          # makes the whole queue between them), so the two counts are one
          set(unmade -1)
          if(r_refused MATCHES "not made yet ([0-9]+)")
            set(unmade "${CMAKE_MATCH_1}")
          endif()
          if(NOT r_state STREQUAL "on" OR r_draws LESS 0)
            message(SEND_ERROR "${dump} ${p} ${s}: no twins' line:\n${r_flat}")
            math(EXPR failures "${failures} + 1")
          elseif(NOT unmade EQUAL r_draws)
            message(SEND_ERROR "${dump} ${p} ${s}: ${r_draws} draws through a twin but ${unmade} found it not made yet (the second replay did not draw every pass the first asked for):\n${r_flat}")
            math(EXPR failures "${failures} + 1")
          elseif(MODE STREQUAL "clean")
            # the camera rule's class ran on the verdict taken as clean
            if(NOT r_clipper STREQUAL "clean")
              message(SEND_ERROR "${dump} ${p} ${s}: the verdict was not taken as clean (${r_clipper}):\n${r_flat}")
              math(EXPR failures "${failures} + 1")
            endif()
            math(EXPR fxTwins "${fxTwins} + ${r_clip}")
          else()
            # the floor alone: no clipper check without the self-test
            if(NOT r_clip EQUAL 0 OR NOT r_clipper STREQUAL "not checked")
              message(SEND_ERROR "${dump} ${p} ${s}: a camera rule pass took a twin without a clipper verdict (${r_clip}, ${r_clipper}):\n${r_flat}")
              math(EXPR failures "${failures} + 1")
            endif()
            math(EXPR fxTwins "${fxTwins} + ${r_draws}")
          endif()
        else()
          if(NOT r_draws EQUAL 0 OR NOT r_flat MATCHES "not applicable here")
            message(SEND_ERROR "${dump} ${p} ${s}: a device with dual-source blending drew through a twin (${r_draws}) or did not say why not:\n${r_flat}")
            math(EXPR failures "${failures} + 1")
          endif()
        endif()
      endforeach()
      if(NOT ok)
        continue()
      endif()
      # the pictures: every target of on and off equal
      set(n 0)
      file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
      file(GLOB pngsB RELATIVE "${dirOn}" "${dirOn}/*.png")
      foreach(need SCENE.png DISPLAY.png)
        if(NOT need IN_LIST pngs)
          message(SEND_ERROR "${dump} ${p} ${s}: no ${need} written")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
      foreach(png IN LISTS pngs)
        if(NOT png IN_LIST pngsB)
          message(SEND_ERROR "${dump} ${p} ${s}: ${png} written without the twins alone")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                                "${dirOn}/${png}" RESULT_VARIABLE same)
        if(NOT same EQUAL 0 AND MODE STREQUAL "clean")
          message(SEND_ERROR "${dump} ${p} ${s}: ${png} differs with the camera rule's passes through the twins")
          math(EXPR cleanDiffers "${cleanDiffers} + 1")
          math(EXPR failures "${failures} + 1")
        elseif(NOT same EQUAL 0)
          message(SEND_ERROR "${dump} ${p} ${s}: ${png} differs with the twins")
          math(EXPR failures "${failures} + 1")
        endif()
        math(EXPR n "${n} + 1")
      endforeach()
      foreach(png IN LISTS pngsB)
        if(NOT png IN_LIST pngs)
          message(SEND_ERROR "${dump} ${p} ${s}: ${png} written with the twins alone")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
      math(EXPR compared "${compared} + ${n}")
      math(EXPR fxCompared "${fxCompared} + ${n}")
      message(STATUS "${dump} ${p} ${s}: ${n} targets compared")
      if(failures EQUAL 0)
        file(REMOVE_RECURSE "${dirOff}" "${dirOn}")
      endif()
    endforeach()
    if(fxCompared EQUAL 0)
      message(SEND_ERROR "${name}: nothing compared under the ${p} profile")
      math(EXPR failures "${failures} + 1")
    endif()
    message(STATUS "${name} ${p}: ${fxTwins} draws through a twin")
    # the clean run's floor for this fixture (clean_min=N on its list line)
    if(MODE STREQUAL "clean" AND nodual_${p} AND "${RDFX_FIELDS_${name}}" MATCHES "clean_min=([0-9]+)")
      set(floor "${CMAKE_MATCH_1}")
      if(fxTwins LESS floor)
        message(SEND_ERROR "${name} ${p}: ${fxTwins} camera rule draws through a twin, under its floor ${floor}")
        math(EXPR failures "${failures} + 1")
      endif()
    endif()
    if(NOT DEFINED profTwins_${p})
      set(profTwins_${p} 0)
    endif()
    math(EXPR profTwins_${p} "${profTwins_${p}} + ${fxTwins}")
    math(EXPR twinTotal "${twinTotal} + ${fxTwins}")
  endforeach()
endforeach()
# every profile without dual-source blending drew through a twin over the
# list (the phone's path ran under each Mali profile; a fixture alone may
# have no eligible pass: test/fixtures/lists/nodiscard.txt)
foreach(p mali mali169)
  if(BACKEND STREQUAL "vulkan" AND DEFINED profTwins_${p} AND NOT profTwins_${p} GREATER 0)
    message(SEND_ERROR "rd_nodiscard_ab: no pass drew through a twin under the ${p} profile over the list (MODE ${MODE}): the twins did not run")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(NOT twinTotal GREATER 0)
  message(SEND_ERROR "rd_nodiscard_ab (${BACKEND}): no pass drew through a twin over the list")
  math(EXPR failures "${failures} + 1")
endif()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
message(STATUS "rd_nodiscard_ab (${BACKEND}): ${compared} targets compared over ${nentries} frames, ${twinTotal} draws through a twin")
if(MODE STREQUAL "clean")
  message(STATUS "rd_nodiscard_clean: ${cleanDiffers} targets differ with the camera rule's passes through the twins")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_nodiscard_ab: ${failures} failures")
endif()
