# rd_fastps_ab: the fast pixel build (port/render/rd_fastps.h;
# docs/port/PIPELINES.md, "The fast pixel build"; issue 179) gives the same
# picture byte for byte as vu_ps_nodual, which it stands in for, and the
# passes drew through it.  The frames: every fixture of LIST
# (port/render/test/fixtures/lists/fastps.txt), each expanded into
# ${OUT}/fixtures.  For every frame, at each setting of each of its
# profiles, rd_replay_tool --pass-dump --stats --repeat 2 twice:
#   off  ICO_RD_VU_FASTPS=0: every pass through vu_ps_nodual
#   on   ICO_RD_VU_FASTPS=1: on without the self-test; the first replay asks
#        for its pipelines, the second replay's start makes the whole queue
#        and waits (rd__fps_replay_begin), so the second, whose targets are
#        written, draws through it every pass the CPU gives it
# The profiles (Vulkan):
#   corpus   the device as it is (lavapipe blends with two outputs: no pass
#            draws through vu_ps_nodual, so none through the build; the run
#            must say so), the Original preset at 4:3 and 1x
#   mali     issue 179's Mali-G52 on lavapipe (the Arm fakes, the record by
#            compute dispatches, no dual-source blending), 1x, 21:9
#   mali169  the same at 16:9 (the reporter's v0.11.14 setting)
#   desktop  the corpus device at 4:3 without dual-source blending
#            (ICO_VK_FAKE_NO_DUAL=1): the build on a desktop GPU's limits
# With BACKEND opengl (rd_fastps_ab_gl) every frame at the corpus setting
# with no dual-source blending (ICO_GL_FAKE_NO_DUAL=1).  The discard-free
# twins stay held (no self-test in the replay tool), so every pass of the set
# takes vu_ps_nodual_fast (vu_ps_nodual_fast_nd is the self-test's and
# play's).  Every target's PNG of on and off must be equal byte for byte;
# off says "fast pixel build off (ICO_RD_VU_FASTPS=0): 0 passes through it"
# and "0 modules of it made"; on under a profile without dual-source
# blending "fast pixel build on (ICO_RD_VU_FASTPS=1): N passes through it"
# with N above 0 over the list under each such profile (the count printed
# per fixture), under corpus N 0 and "not applicable here".
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
    message(SEND_ERROR "rd_fastps_ab: ${LIST}: ${name}: \"${prof}\" is not its profiles")
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
    message(FATAL_ERROR "rd_fastps_ab: ${failures} failures, no frame replayed")
  endif()
  message(STATUS "SKIP: no frame is here")
  return()
endif()

set(env_corpus "")
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute
             ICO_VK_FAKE_NO_DUAL=1)
set(env_mali169 ${env_mali})
set(env_desktop ICO_VK_FAKE_NO_DUAL=1)
set(env_gl ICO_GL_FAKE_NO_DUAL=1)
set(settings_corpus orig)
set(settings_mali mali)
set(settings_mali169 m169)
set(settings_desktop orig)
set(settings_gl orig)
set(args_orig --aspect 4:3)
set(args_mali --aspect 21:9)
set(args_m169 --aspect 16:9)
set(nodual_corpus 0)
set(nodual_mali 1)
set(nodual_mali169 1)
set(nodual_desktop 1)
set(nodual_gl 1)
# the build's run line (rd_fastps_run.c rd__fps_stats_text), matched in the
# run's text with its ';' made ','
set(ndRe "stats: fast pixel build (on|off|held)[^:]*: ([0-9]+) passes through it, ([0-9]+) triangles, refused: ([^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*, [^,]*), ([0-9]+) modules of it made")

# a run of a frame: rc, its text flattened, the twins' draws and modules
function(ndRun dump p s value dir)
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${p}} ICO_RD_VU_FASTPS=${value}
                          "${TOOL}" "${dump}" /dev/null --pass-dump "${dir}" --stats --repeat 2
                          ${args_${s}}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REPLACE ";" "," flat "${out}\n${err}")
  set(state "")
  set(draws -1)
  set(tris -1)
  set(refused "")
  set(made -1)
  if(flat MATCHES "${ndRe}")
    set(state "${CMAKE_MATCH_1}")
    set(draws "${CMAKE_MATCH_2}")
    set(tris "${CMAKE_MATCH_3}")
    set(refused "${CMAKE_MATCH_4}")
    set(made "${CMAKE_MATCH_5}")
  endif()
  set(r_tris "${tris}" PARENT_SCOPE)
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
        message(STATUS "${dump} ${p} ${s} ICO_RD_VU_FASTPS=${v}: fast pixel build ${r_state}, ${r_draws} passes through it, ${r_tris} triangles (refused: ${r_refused}), ${r_made} modules made")
        if(v EQUAL 0)
          if(NOT r_state STREQUAL "off" OR NOT r_draws EQUAL 0 OR NOT r_made EQUAL 0)
            message(SEND_ERROR "${dump} ${p} ${s}: with the build off something of it ran (${r_state}, ${r_draws} passes, ${r_made} modules):\n${r_flat}")
            math(EXPR failures "${failures} + 1")
          endif()
        elseif(nodual_${p})
          # --repeat 2: the first replay asks for every pass's pipeline ("not
          # made yet"), the second draws each through it, so the two counts
          # are one
          set(unmade -1)
          if(r_refused MATCHES "not made yet ([0-9]+)")
            set(unmade "${CMAKE_MATCH_1}")
          endif()
          if(NOT r_state STREQUAL "on" OR r_draws LESS 0)
            message(SEND_ERROR "${dump} ${p} ${s}: no fast pixel build's line:\n${r_flat}")
            math(EXPR failures "${failures} + 1")
          elseif(NOT unmade EQUAL r_draws)
            message(SEND_ERROR "${dump} ${p} ${s}: ${r_draws} passes through the build but ${unmade} found it not made yet (the second replay did not draw every pass the first asked for):\n${r_flat}")
            math(EXPR failures "${failures} + 1")
          endif()
          math(EXPR fxTwins "${fxTwins} + ${r_draws}")
        else()
          if(NOT r_draws EQUAL 0 OR NOT r_flat MATCHES "not applicable here")
            message(SEND_ERROR "${dump} ${p} ${s}: a device with dual-source blending drew through the build (${r_draws}) or did not say why not:\n${r_flat}")
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
          message(SEND_ERROR "${dump} ${p} ${s}: ${png} written without the build alone")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                                "${dirOn}/${png}" RESULT_VARIABLE same)
        if(NOT same EQUAL 0)
          message(SEND_ERROR "${dump} ${p} ${s}: ${png} differs with the fast pixel build")
          math(EXPR failures "${failures} + 1")
        endif()
        math(EXPR n "${n} + 1")
      endforeach()
      foreach(png IN LISTS pngsB)
        if(NOT png IN_LIST pngs)
          message(SEND_ERROR "${dump} ${p} ${s}: ${png} written with the build alone")
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
    message(STATUS "${name} ${p}: ${fxTwins} passes through the fast pixel build")
    if(NOT DEFINED profTwins_${p})
      set(profTwins_${p} 0)
    endif()
    math(EXPR profTwins_${p} "${profTwins_${p}} + ${fxTwins}")
    math(EXPR twinTotal "${twinTotal} + ${fxTwins}")
  endforeach()
endforeach()
# every profile without dual-source blending drew through the build over
# the list (the phone's path under each Mali profile, the desktop limits'
# under desktop)
foreach(p mali mali169 desktop)
  # a profile that drew no frame at all (every fixture skipped) fails too:
  # the test exists to compare the build, so it must have run under each
  if(BACKEND STREQUAL "vulkan" AND (NOT DEFINED profTwins_${p} OR NOT profTwins_${p} GREATER 0))
    message(SEND_ERROR "rd_fastps_ab: no pass drew through the fast pixel build under the ${p} profile over the list: it did not run")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(NOT twinTotal GREATER 0)
  message(SEND_ERROR "rd_fastps_ab (${BACKEND}): no pass drew through the fast pixel build over the list")
  math(EXPR failures "${failures} + 1")
endif()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
message(STATUS "rd_fastps_ab (${BACKEND}): ${compared} targets compared over ${nentries} frames, ${twinTotal} passes through the fast pixel build")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_fastps_ab: ${failures} failures")
endif()
