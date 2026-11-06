# rd_vu_bones_ring_ab: issue 123's ladder, step 1 (port/render/rd_vu_bones.c,
# rd_pipeline.c rd__vu_policy_init; docs/port/VU_SKIN_CHECK.md "The
# ladder"): the cluster entries' ring builds (vu_skin{,_spec,_debug}_ring_plain_vs,
# the bones read through t5 from VuCB's mark lane) must draw every picture
# byte for byte as the plain variants that read VuBoneCB at b3.  For each
# dump of DUMPS that is here, rd_replay_tool --pass-dump with [dev] vu_bones
# unset (ICO_RD_VU_BONES=cbuffer) and = ring, the program entries the plain
# set (ICO_RD_VU_SET=plain, the Turnip profile's; the ring builds are plain
# variants) and the VU pre-pass off (as on a tile-based GPU: every skinned
# draw through its program's entry), the skin check off (its own draws are
# not the subject here), at two settings:
#   plain   Original at the dump's aspect, presented at 640x480;
#   attach  the same with the attachment read and GS state as shader data
#           (ICO_RD_BLEND=attachment, ICO_RD_UBER=on, the interlock faked
#           off: lavapipe's form of the Turnip reporter's profile).
# Every target's PNG of the ring run must equal the cbuffer run's byte for
# byte; the ring run must say "VU bones: through the ring (storage buffer)
# on the skinned entries" and the cbuffer run "VU bones: through VuBoneCB".
# A missing dump is named and skipped; no dump, or no device (77): SKIP.
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

set(settings "plain" "attach")
set(env_common ICO_RD_VU_SKIN_CHECK=0 ICO_RD_VU_SET=plain ICO_RD_VU_PREPASS=0)
set(env_plain "")
set(env_attach ICO_VK_FAKE_NO_INTERLOCK=1 ICO_RD_BLEND=attachment ICO_RD_UBER=on)
set(line_cbuffer "rd: VU bones: through VuBoneCB")
set(line_ring "rd: VU bones: through the ring \\(storage buffer\\) on the skinned entries")
set(compared 0)
set(failures 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(ok TRUE)
    foreach(run cbuffer ring)
      set(dir_${run} "${OUT}/${index}-${s}-${run}")
      file(REMOVE_RECURSE "${dir_${run}}")
      file(MAKE_DIRECTORY "${dir_${run}}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_RD_VU_BONES=${run} ${env_common}
                              ${env_${s}} "${TOOL}" "${dump}" - --pass-dump "${dir_${run}}"
                              --present 640x480
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
      if(NOT flat MATCHES "${line_${run}}")
        message(SEND_ERROR "${dump} ${s} ${run}: no \"${line_${run}}\" line:\n${err}")
        set(ok FALSE)
        break()
      endif()
      if(s STREQUAL "attach" AND NOT flat MATCHES "GS state as shader data: on")
        message(SEND_ERROR "${dump} attach ${run}: GS state as shader data is not on:\n${err}")
        set(ok FALSE)
        break()
      endif()
    endforeach()
    if(NOT ok)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    file(GLOB pngs RELATIVE "${dir_cbuffer}" "${dir_cbuffer}/*.png")
    list(LENGTH pngs npng)
    if(npng EQUAL 0)
      message(SEND_ERROR "${dump} ${s}: no target written")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      if(NOT EXISTS "${dir_ring}/${png}")
        message(SEND_ERROR "${dump} ${s}: ${png} written through VuBoneCB only")
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir_cbuffer}/${png}"
                              "${dir_ring}/${png}" RESULT_VARIABLE same)
      math(EXPR compared "${compared} + 1")
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the bones through the ring")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
    message(STATUS "${dump} ${s}: ${npng} pictures, the ring read = VuBoneCB")
  endforeach()
endforeach()
message(STATUS "rd_vu_bones_ring_ab: ${compared} pictures compared over ${ndumps} dumps")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_vu_bones_ring_ab: ${failures} failures")
endif()
