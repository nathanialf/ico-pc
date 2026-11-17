# rd_hold_ab: the paused scene's hold (port/render/rd_hold.c; docs/port/RENDER.md
# "The paused scene's hold") against every picture drawn in full.  For every
# fixture the list names (port/render/test/fixtures/lists/hold.txt: fixture names
# of the repository, docs/port/TESTING_FIXTURES.md; a listed fixture that is
# absent or does not expand to its pinned SHA-256 FAILS), expanded into OUT,
# rd_replay_tool --repeat N (the frame's N ticks in a row, each taken as paused
# with ICO_RD_SCENE_HOLD=paused, against ICO_RD_SCENE_HOLD=0) at each setting:
#   tablet  the issue 123 tablet's path (ICO_VK_FAKE_PASS_ROW=qualcomm
#           ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1), Enhanced 3x, Full height,
#           Full pixel, presented at 3048x2032;
#   late    the same with the late pass on (ICO_RD_LATE_PASS=1: the crack fill
#           held to the glow's or the aura's paste, as on the tablet itself);
#   desk    Enhanced 2x at 4:3, lavapipe's own profile (a desktop class);
# in three modes: "targets" (--pass-dump: every named target after the last
# tick), "picture" (the presented picture of the last tick) and "clear" (the
# picture with ICO_RD_WORK_CLEAR=FF00FF in both runs: every target but SCENE,
# DISPLAY and the feedback cleared before every replay, so a held picture that
# read a work target lists 0-10 drew would show it).  Each pair: every PNG the
# same bytes.  The path ran: a "held" fixture's held run says the hold on, its
# self-test "equal" and N - 5 or more pictures from the held scene (rd_hold.c,
# RHI_FRAMES_IN_FLIGHT 2: two takes, the readbacks of both at the third and
# fourth replays' waits, the fourth a take as the first held picture waits for
# the self-test, the fifth the self-test's; N - 2 with no carried target; the
# chain's state key taken at list 11 of its first take, after the first
# upload set the levels' filter; the tool marks its frame paused, so the
# start-up self-tests' frames are not counted) and the full run none; a
# "refused WHY" fixture none held and its reason counted.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
set(failures 0)
set(N 8)
rd_fixture_list("${LIST}" names)

set(tablet ICO_VK_FAKE_PASS_ROW=qualcomm ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1)
set(env_tablet ${tablet})
set(args_tablet --enhanced --resolution 3x --full-height --full-pixel --present 3048x2032)
set(env_late ${tablet} ICO_RD_LATE_PASS=1)
set(args_late ${args_tablet})
set(env_desk "")
set(args_desk --enhanced --resolution 2x --aspect 1.3333 --present 1280x960)

set(heldRe "window: still scene: ([0-9]+) pictures from the held scene")
set(selfRe "still scene: self-test: the first held picture against the same picture in full: ([a-z ]+) \\(")
set(whyRe_picture "the scene reads the picture before")
set(whyRe_menu "lists 11-12 the hold cannot keep")

set(compared 0)
set(heldTotal 0)
set(n 0)
foreach(name IN LISTS names)
  math(EXPR n "${n} + 1")
  set(word "${RDFX_FIELDS_${name}}")
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)
  foreach(s tablet late desk)
    foreach(mode targets picture clear)
      foreach(w 0 1)
        set(dir "${OUT}/${n}-${s}-${mode}-${w}")
        file(REMOVE_RECURSE "${dir}")
        file(MAKE_DIRECTORY "${dir}")
        set(sw ICO_RD_SCENE_HOLD=0)
        if(w EQUAL 1)
          set(sw ICO_RD_SCENE_HOLD=paused)
        endif()
        set(extraEnv "")
        if(mode STREQUAL "clear")
          set(extraEnv ICO_RD_WORK_CLEAR=FF00FF)
        endif()
        if(mode STREQUAL "targets")
          set(outArgs - --pass-dump "${dir}")
        else()
          set(outArgs "${dir}/out.png")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_PREPASS=0 ${env_${s}} ${sw}
                                ${extraEnv} "${TOOL}" "${dump}" ${outArgs} --stats --repeat ${N}
                                ${args_${s}}
                        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(rc EQUAL 77)
          message(STATUS "SKIP: no device: ${err}")
          return()
        endif()
        if(NOT rc EQUAL 0)
          message(SEND_ERROR "${name} ${s} ${mode} ${sw}: exit ${rc}:\n${err}")
          math(EXPR failures "${failures} + 1")
          continue()
        endif()
        set(err_${w} "${err}")
      endforeach()
      # the bytes: every PNG of the held run the full run's
      set(d0 "${OUT}/${n}-${s}-${mode}-0")
      set(d1 "${OUT}/${n}-${s}-${mode}-1")
      file(GLOB pngs RELATIVE "${d0}" "${d0}/*.png")
      file(GLOB pngs1 RELATIVE "${d1}" "${d1}/*.png")
      list(LENGTH pngs np)
      list(LENGTH pngs1 np1)
      if(np EQUAL 0 OR NOT np EQUAL np1)
        message(SEND_ERROR "${name} ${s} ${mode}: ${np} pictures in full, ${np1} held")
        math(EXPR failures "${failures} + 1")
      endif()
      foreach(png IN LISTS pngs)
        math(EXPR compared "${compared} + 1")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${d0}/${png}" "${d1}/${png}"
                        RESULT_VARIABLE same)
        if(NOT same EQUAL 0)
          message(SEND_ERROR "${name} ${s} ${mode}: ${png} differs with the scene held: it must be "
                             "the same bytes")
          math(EXPR failures "${failures} + 1")
        endif()
      endforeach()
      # the path: the full run held nothing; the held run as its word says
      string(REPLACE ";" "," flat0 "${err_0}")
      string(REPLACE ";" "," flat1 "${err_1}")
      if(flat0 MATCHES "${heldRe}")
        message(SEND_ERROR "${name} ${s} ${mode}: ICO_RD_SCENE_HOLD=0 took pictures as paused")
        math(EXPR failures "${failures} + 1")
      endif()
      set(held 0)
      if(flat1 MATCHES "${heldRe}")
        set(held "${CMAKE_MATCH_1}")
      endif()
      if(word STREQUAL "held")
        set(self "")
        if(flat1 MATCHES "${selfRe}")
          set(self "${CMAKE_MATCH_1}")
        endif()
        math(EXPR least "${N} - 5")
        if(NOT self STREQUAL "equal" OR held LESS least)
          message(SEND_ERROR "${name} ${s} ${mode}: ${held} pictures held (${least} or more "
                             "expected), self-test '${self}' ('equal' expected):\n${err_1}")
          math(EXPR failures "${failures} + 1")
        endif()
        math(EXPR heldTotal "${heldTotal} + ${held}")
      elseif(word MATCHES "^refused[ \t]+([a-z]+)$")
        set(why "${CMAKE_MATCH_1}")
        if(NOT held EQUAL 0 OR NOT DEFINED whyRe_${why} OR
           NOT flat1 MATCHES "[1-9][0-9]* ${whyRe_${why}}")
          message(SEND_ERROR "${name} ${s} ${mode}: ${held} held, '${whyRe_${why}}' counted "
                             "expected:\n${err_1}")
          math(EXPR failures "${failures} + 1")
        endif()
      else()
        message(SEND_ERROR "${LIST}: ${name}: the word '${word}' is not held or refused WHY")
        math(EXPR failures "${failures} + 1")
      endif()
      message(STATUS "${name} ${s} ${mode}: ${np} pictures the same bytes, ${held} held")
    endforeach()
  endforeach()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_hold_ab: ${failures} failures over ${n} fixtures")
endif()
if(compared EQUAL 0 OR heldTotal EQUAL 0)
  message(FATAL_ERROR "rd_hold_ab: ${compared} pictures compared, ${heldTotal} held: one held at "
                      "least expected")
endif()
message(STATUS "rd_hold_ab: ${compared} pictures the same bytes over ${n} fixtures, ${heldTotal} "
               "drawn from the held scene")
