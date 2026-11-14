# rd_health_ab: the picture-health guard's switches in the run
# (port/render/rd_health.h; issue 179), on lavapipe through the tests' fake
# ICO_RD_HEALTH_FAKE (docs/port/TESTING.md).  DUMP (issue 179's Mali-G52
# stage 3 frame) is replayed under the Arm fakes at the reporter's settings
# (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5, the record by compute
# dispatches, 21:9, presented at 1612x720), with every group the guard
# switches in the run on: the index compaction (ICO_RD_VU_COMPACT=1), the
# record made at the run's need (ICO_RD_EARLY_PREPARE=1), one DATE set-up and
# the empty composites' skip (their defaults), the volumes' stencil-only
# entry (ICO_RD_VOLUME_FS=1 runs its self-test); the glow is off, so the
# paste's entry has no draw; the composites' entry is not offered there.
# Every run makes the presented picture REPEAT times (--repeat: the guard
# judges presented pictures with VU draws), the PNG read after the last:
#   base   no fake: the guard's start line, no picture unfinished, no step
#   hang   every judged picture read as one with stamps unwritten: one line
#          per group (a) to (e), each turning its live parts off in the run
#          ((c) and (e) naming their next-start parts), then "stepping
#          everything off in this run did not recover"
#   b..e   read as unfinished until that group is off: the steps up to it,
#          then "pictures complete again after turning off (<group>) ..."
# and every run's PNG equal to base's byte for byte (each switch draws the
# same picture: their own A/B tests' claim, here switched mid-run).  The
# numbers are the rule's (rd_health.h): the window starts empty after each
# step, so every trigger reads "16 of the last 16"; a step at record R
# judges from R + 3, the next trigger 15 records later; the confirmation
# needs 64 in a row: group e's at record 154, collected two replays later,
# so REPEAT 170 covers every run.  Where /primary/dev/ico-dumps exists an
# absent DUMP fails; elsewhere, and with no device (77), SKIP.  Vulkan only.
# -DTOOL=<rd_replay_tool> -DOUT=<a folder the test may write> -DDUMP=<dump>
set(strict FALSE)
if(EXISTS "/primary/dev/ico-dumps")
  set(strict TRUE)
endif()
if(NOT DUMP OR NOT EXISTS "${DUMP}")
  if(strict)
    message(FATAL_ERROR "rd_health_ab: ${DUMP} is not here")
  endif()
  message(STATUS "SKIP: ${DUMP} is not here")
  return()
endif()
set(REPEAT 170)
set(env ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_RD_VU_RECORD=compute
        ICO_RD_VU_COMPACT=1 ICO_RD_EARLY_PREPARE=1 ICO_RD_VOLUME_FS=1)
set(args --aspect 21:9 --present 1612x720 --stats --repeat ${REPEAT})
set(failures 0)
file(MAKE_DIRECTORY "${OUT}")
set(trig "picture health: 16 of the last 16 3D pictures came back unfinished")
set(stepA "turning off \\(a\\) the index compaction now")
set(stepB "turning off \\(b\\) the VU record sized from need now \\(the record grown to its largest at the next picture, as v0.11.5 made it\\)")
set(stepC "turning off \\(c\\) one DATE set-up now, from the next start \\(made at the start\\): \\(c\\) flat outputs at the provoking corner")
set(stepD "turning off \\(d\\) the shadow volumes by the stencil-only entry now")
set(stepE "turning off \\(e\\) empty composites skipped now")
set(nameB "\\(b\\) the VU record sized from need")
set(nameC "\\(c\\) one DATE set-up")
set(nameD "\\(d\\) the shadow volumes by the stencil-only entry")
set(nameE "\\(e\\) empty composites skipped")
foreach(run base hang b c d e)
  set(fake "")
  if(NOT run STREQUAL "base")
    set(fake "ICO_RD_HEALTH_FAKE=${run}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env} ${fake} "${TOOL}" "${DUMP}"
                          "${OUT}/${run}.png" ${args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    return()
  endif()
  string(REPLACE ";" "," flat "${out}\n${err}")
  if(NOT rc EQUAL 0)
    message(SEND_ERROR "rd_health_ab ${run}: exit ${rc}:\n${flat}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  set(want "rd: picture health: on: ")
  set(wantNot "")
  if(run STREQUAL "base")
    list(APPEND want "rd: VU index compaction: on")
    set(wantNot "came back unfinished")
  elseif(run STREQUAL "hang")
    list(APPEND want "${trig}" "${stepA}" "${stepB}" "${stepC}" "${stepD}" "${stepE}"
         "picture health: stepping everything off in this run did not recover")
    set(wantNot "pictures complete again")
  else()
    string(TOUPPER "${run}" up)
    list(APPEND want "${trig}" "${stepA}" "${step${up}}"
         "picture health: pictures complete again after turning off ${name${up}}: 64 3D pictures in a row")
    set(wantNot "did not recover")
  endif()
  foreach(re IN LISTS want)
    if(NOT flat MATCHES "${re}")
      message(SEND_ERROR "rd_health_ab ${run}: no line matching \"${re}\"")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
  if(wantNot AND flat MATCHES "${wantNot}")
    message(SEND_ERROR "rd_health_ab ${run}: a line matching \"${wantNot}\"")
    math(EXPR failures "${failures} + 1")
  endif()
  if(NOT run STREQUAL "base")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/base.png"
                            "${OUT}/${run}.png" RESULT_VARIABLE same)
    if(NOT same EQUAL 0)
      message(SEND_ERROR "rd_health_ab ${run}: the picture differs from base's")
      math(EXPR failures "${failures} + 1")
    else()
      message(STATUS "rd_health_ab ${run}: the same picture as base after the guard's steps")
    endif()
  endif()
  if(failures GREATER 0)
    message(STATUS "${flat}")
  endif()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_health_ab: ${failures} failures")
endif()
