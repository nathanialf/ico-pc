# rd_pass_objects_ab: Qualcomm's driver row (port/rhi/rhi_pass_row.h
# rhi_vk_pass_row_qualcomm, issue 123): every render pass as a VkRenderPass
# object instead of dynamic rendering, against dynamic rendering, byte for byte.
# For every line of the list (tools/pass_objects_dumps.txt: a path, '*' globs
# allowed; a listed dump that is absent FAILS), rd_replay_tool's --pass-dump
# with --stats twice: A with [dev] legacy_passes = false (ICO_VK_LEGACY_PASSES=0:
# dynamic rendering on lavapipe's Vulkan 1.4), B with lavapipe put on the row
# alone (ICO_VK_FAKE_PASS_ROW=qualcomm: the row's own decision, nothing else of
# the driver faked), at:
#   qcom  the issue 123 tablet's setting (Enhanced 3x, Full height, Full pixel,
#         presented at 3048x2032) under a Qualcomm phone's tile rule
#         (ICO_VK_FAKE_VENDOR=0x5143: wide barriers, the single submission
#         wait) with the VU pre-pass off (ICO_RD_VU_PREPASS=0, the device
#         table's), the passes' path the only difference;
#   mali  the issue 179 phone's (1x at 21:9, presented at 2400x1080) on a
#         Mali-G68's limits and an Arm phone's vendor (ICO_VK_FAKE_LIMITS=mali,
#         ICO_VK_FAKE_VENDOR=0x13B5).
# Each pair: every PNG the A run writes (SCENE, DISPLAY, PRESENT and every other
# named target) the B run's byte for byte, the same draws and passes; A's log
# says "render passes dynamic", B's "render passes legacy (Qualcomm's own
# driver".  Over the list: at least one target compared.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# The list names dumps of the build machine alone (the owner's rule there: a
# listed dump that is absent FAILS). A machine without that folder has none of
# them: the test is skipped as the older dump tests are.
if(NOT IS_DIRECTORY "/primary/dev/ico-dumps")
  message(STATUS "SKIP: no dump folder on this machine (/primary/dev/ico-dumps): the list's dumps are the build machine's")
  return()
endif()
set(failures 0)
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_pass_objects_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(nentries 0)
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
  elseif(EXISTS "${l}")
    set(found "${l}")
  endif()
  if(NOT found)
    message(SEND_ERROR "${l}: no such dump (a listed dump must exist)")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  foreach(d IN LISTS found)
    math(EXPR nentries "${nentries} + 1")
    set(entry_${nentries} "${d}")
  endforeach()
endforeach()

set(args_qcom --enhanced --resolution 3x --full-height --full-pixel --present 3048x2032)
set(env_qcom ICO_VK_FAKE_VENDOR=0x5143 ICO_RD_VU_PREPASS=0)
set(args_mali --resolution 1x --aspect 2.3333 --present 2400x1080)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5)
set(settings qcom mali)
set(sw_0 ICO_VK_LEGACY_PASSES=0)
set(sw_1 ICO_VK_FAKE_PASS_ROW=qualcomm)
set(path_0 "render passes dynamic")
set(path_1 "render passes legacy \\(Qualcomm's own driver")

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")

set(compared 0)
set(indices "")
if(nentries GREATER 0)
  foreach(i RANGE 1 ${nentries})
    list(APPEND indices ${i})
  endforeach()
endif()
foreach(index IN LISTS indices)
  set(dump "${entry_${index}}")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(w 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${w}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${s}} ${sw_${w}}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND w EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} path ${w}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}\n${err}")
      if(NOT flat MATCHES "${drawsRe}")
        message(SEND_ERROR "${dump} ${s} path ${w}: no draws line:\n${out}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${w} "${CMAKE_MATCH_1}")
      set(passes_${w} "${CMAKE_MATCH_2}")
      if(NOT flat MATCHES "${path_${w}}")
        message(SEND_ERROR "${dump} ${s} path ${w}: the device line does not say "
                           "\"${path_${w}}\" (the run's passes are not on the path asked for)")
        math(EXPR failures "${failures} + 1")
      endif()
      message(STATUS "${dump} ${s} path ${w}: ${passes_${w}} passes, ${draws_${w}} draws")
    endforeach()
    if(skipped)
      continue()
    endif()
    if(NOT draws_1 EQUAL draws_0 OR NOT passes_1 EQUAL passes_0)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws, ${passes_1} passes as VkRenderPass "
                         "objects; ${draws_0}, ${passes_0} with dynamic rendering: the same expected")
      math(EXPR failures "${failures} + 1")
    endif()
    set(dirA "${OUT}/${index}-${s}-0")
    set(dirB "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirA}" "${dirA}/*.png")
    file(GLOB pngsB RELATIVE "${dirB}" "${dirB}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsB npngB)
    if(npng EQUAL 0 OR NOT npng EQUAL npngB)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written with dynamic rendering, ${npngB} "
                         "as VkRenderPass objects")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirA}/${png}"
                              "${dirB}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs between dynamic rendering and "
                           "VkRenderPass objects: it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_pass_objects_ab: ${failures} failures over ${nentries} entries")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_pass_objects_ab: no target compared over ${nentries} entries")
endif()
message(STATUS "rd_pass_objects_ab: ${compared} targets the same bytes over ${nentries} entries")
