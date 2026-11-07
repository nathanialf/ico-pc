# rd_vu_skin_check_ab: the skinned draws' bone-block check (issue 163;
# port/render/rd_vu_skin_check.c, docs/port/VU_SKIN_CHECK.md) is designed
# not to change the picture; this measures it. For each dump of DUMPS that
# is here, rd_replay_tool --pass-dump with the check off
# (ICO_RD_VU_SKIN_CHECK=0) and with it on for every skinned command of every
# replay (=2: the probes after the last pass, the box check S3 drawn inside
# the scene pass, the checked draw kept out of the draw merge), at three
# settings:
#   plain   Original at the dump's aspect, presented at 640x480;
#   attach  the same with the attachment read and GS state as shader data
#           (ICO_RD_BLEND=attachment, ICO_RD_UBER=on, the interlock faked
#           off, the VU pre-pass off as on a tile-based GPU before
#           v0.11.0: lavapipe's form of the Turnip reporter's profile, where the twins of the
#           Z-write-off draws go through the draws' own pipelines; the on
#           run must say "GS state as shader data: on");
#   msaa    Enhanced 4:3 2x with --aa msaa4.
# Every target's PNG of the on run must equal the off run's byte for byte,
# and the on run must say "VU skin check: at the end of the run: N checks
# drawn" with N at least 1; over all the runs at least one box check must
# have been drawn. The twins (issue 123: mode 2 twins every skinned draw)
# are drawn in every plain and attach on run ("VU skin check: twins at the
# end of the run: T drawn (O through the draw's own pipeline with its
# attachment read)", T at least 1; none in msaa's multisampled passes),
# and over the attach runs O must be at least 1: the twins went through
# the draw's own pipeline with its write mask cleared, and the pictures
# above stayed byte for byte. A missing dump is named and skipped; no
# dump, or no device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DDUMPS=<dump|dump...> -DOUT=<a folder the test may write>
string(REPLACE "|" ";" DUMPS "${DUMPS}")
set(dumps "")
foreach(d IN LISTS DUMPS)
  if(EXISTS "${d}")
    list(APPEND dumps "${d}")
  else()
    message(STATUS "${d}: no such dump, skipped")
  endif()
endforeach()
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: none of the dumps is here")
  return()
endif()

set(settings "plain" "attach" "msaa")
set(args_plain --present 640x480)
set(args_attach --present 640x480)
set(args_msaa --enhanced --aspect 4:3 --resolution 2x --aa msaa4)
set(env_plain "")
set(env_attach ICO_VK_FAKE_NO_INTERLOCK=1 ICO_RD_BLEND=attachment ICO_RD_UBER=on ICO_RD_VU_PREPASS=0)
set(env_msaa "")
set(compared 0)
set(failures 0)
set(boxes 0)
set(ownTwins 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(ok TRUE)
    foreach(run off on)
      if(run STREQUAL "off")
        set(mode 0)
      else()
        set(mode 2)
      endif()
      set(dir_${run} "${OUT}/${index}-${s}-${run}")
      file(REMOVE_RECURSE "${dir_${run}}")
      file(MAKE_DIRECTORY "${dir_${run}}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_SKIN_CHECK=${mode} ${env_${s}}
                              "${TOOL}" "${dump}" - --pass-dump "${dir_${run}}" ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} ${run}: exit ${rc}:\n${err}")
        set(ok FALSE)
        break()
      endif()
      string(REPLACE ";" "," flat "${out}${err}")
      if(run STREQUAL "on")
        if(NOT flat MATCHES "VU skin check: at the end of the run: ([0-9]+) checks drawn \\(([0-9]+) box checks drawn\\)" OR
           CMAKE_MATCH_1 EQUAL 0)
          message(SEND_ERROR "${dump} ${s} on: the check drew nothing:\n${err}")
          set(ok FALSE)
          break()
        endif()
        set(drawn "${CMAKE_MATCH_1}")
        set(boxDrawn "${CMAKE_MATCH_2}")
        math(EXPR boxes "${boxes} + ${boxDrawn}")
        set(twinsDrawn 0)
        set(twinsOwn 0)
        if(flat MATCHES "VU skin check: twins at the end of the run: ([0-9]+) drawn \\(([0-9]+) through the draw's own pipeline with its attachment read\\)")
          set(twinsDrawn "${CMAKE_MATCH_1}")
          set(twinsOwn "${CMAKE_MATCH_2}")
        endif()
        # no twin in a multisampled pass (msaa: the scene's passes are)
        if(NOT s STREQUAL "msaa" AND twinsDrawn EQUAL 0)
          message(SEND_ERROR "${dump} ${s} on: no twin drawn:\n${err}")
          set(ok FALSE)
          break()
        endif()
        if(s STREQUAL "attach")
          math(EXPR ownTwins "${ownTwins} + ${twinsOwn}")
        endif()
        if(s STREQUAL "attach" AND NOT flat MATCHES "GS state as shader data: on")
          message(SEND_ERROR "${dump} attach on: GS state as shader data is not on:\n${err}")
          set(ok FALSE)
          break()
        endif()
      elseif(flat MATCHES "VU skin check: at the end of the run")
        message(SEND_ERROR "${dump} ${s} off: the check ran with ICO_RD_VU_SKIN_CHECK=0:\n${err}")
        set(ok FALSE)
        break()
      endif()
    endforeach()
    if(NOT ok)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    file(GLOB pngs RELATIVE "${dir_off}" "${dir_off}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dir_on}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written with the check off only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir_off}/${png}"
                              "${dir_on}/${png}" RESULT_VARIABLE same)
      math(EXPR compared "${compared} + 1")
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the check on")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    message(STATUS "${dump} ${s}: ${npng} pictures, the check on (${drawn} checks, ${boxDrawn} "
                   "box checks, ${twinsDrawn} twins, ${twinsOwn} through the draw's own "
                   "pipeline) = off")
  endforeach()
endforeach()
if(failures EQUAL 0 AND boxes EQUAL 0)
  message(SEND_ERROR "no box check was drawn in any run")
  set(failures 1)
endif()
if(failures EQUAL 0 AND ownTwins EQUAL 0)
  message(SEND_ERROR "no twin went through the draw's own pipeline in the attach runs")
  set(failures 1)
endif()
message(STATUS "rd_vu_skin_check_ab: ${compared} pictures compared over ${ndumps} dumps, "
               "${boxes} box checks drawn, ${ownTwins} twins through the draw's own pipeline "
               "(attach)")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vu_skin_check_ab: ${failures} failures")
endif()
