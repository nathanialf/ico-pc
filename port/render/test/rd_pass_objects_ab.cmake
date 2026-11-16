# rd_pass_objects_ab: Qualcomm's driver row (port/rhi/rhi_pass_row.h
# rhi_vk_pass_row_qualcomm, issue 123): every render pass as a VkRenderPass
# object instead of dynamic rendering, against dynamic rendering, byte for byte.
# For every fixture the list names (port/render/test/fixtures/lists/
# pass_objects.txt: fixture names of the repository, docs/port/
# TESTING_FIXTURES.md, each with the properties this test checks on its line;
# a listed fixture that is absent or does not expand to its pinned SHA-256
# FAILS), expanded into OUT, rd_replay_tool's --pass-dump with --stats twice:
# A with [dev] legacy_passes = false (ICO_VK_LEGACY_PASSES=0: dynamic rendering
# on lavapipe's Vulkan 1.4), B with lavapipe put on the row alone
# (ICO_VK_FAKE_PASS_ROW=qualcomm: the row's own decision, nothing else of the
# driver faked), at:
#   qcom  the issue 123 tablet's setting (Enhanced 3x, Full height, Full pixel,
#         presented at 3048x2032) under a Qualcomm phone's tile rule
#         (ICO_VK_FAKE_VENDOR=0x5143: wide barriers, the single submission
#         wait) with the VU pre-pass off (ICO_RD_VU_PREPASS=0, the device
#         table's), the passes' path the only difference;
#   mali  the issue 179 phone's (1x at 21:9, presented at 2400x1080) on a
#         Mali-G68's limits and an Arm phone's vendor (ICO_VK_FAKE_LIMITS=mali,
#         ICO_VK_FAKE_VENDOR=0x13B5); a fixture drawn narrower than the aspect
#         cannot show it and is skipped there.
# Each pair: every PNG the A run writes (SCENE, DISPLAY, PRESENT and every other
# named target) the B run's byte for byte, the same draws and passes; A's log
# says "render passes dynamic", B's "render passes legacy (Qualcomm's own
# driver"; the stats line "render passes: N begun, M as VkRenderPass objects"
# M 0 on A and M = N on B (vk_cmd.c counts a pass begun only after
# vkr_pass_begin succeeds, and vk_renderpass.c counts an object at each
# vkCmdBeginRenderPass of the legacy path: under the row every pass begun is
# one object).  The path ran (rd_fixture_require on B): the line's
# pass_objects.
# A fixture whose properties no setting checked FAILS.  Over the list: at least
# one target compared.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write>
include("${FIXTURE_HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/rd_fixture_list_props.cmake")
set(failures 0)
set(nentries 0)
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_list_props(${name})
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  math(EXPR nentries "${nentries} + 1")
  set(entry_${nentries} "${d}")
  set(name_${nentries} "${name}")
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
set(objectsRe "stats: render passes: ([0-9]+) begun, ([0-9]+) as VkRenderPass objects")

set(compared 0)
set(indices "")
foreach(i RANGE 1 ${nentries})
  list(APPEND indices ${i})
endforeach()
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
                              "${TOOL}" "${dump}" "${OUT}/out.png" --pass-dump "${dir}" --stats
                              ${args_${s}}
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
      if(NOT flat MATCHES "${objectsRe}")
        message(SEND_ERROR "${dump} ${s} path ${w}: no render passes line:\n${out}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      set(begun "${CMAKE_MATCH_1}")
      set(objects "${CMAKE_MATCH_2}")
      if(w EQUAL 0)
        set(wantObjects 0)
      else()
        set(wantObjects "${begun}")
      endif()
      if(NOT objects EQUAL wantObjects OR begun EQUAL 0)
        message(SEND_ERROR "${dump} ${s} path ${w}: ${objects} of ${begun} passes begun as "
                           "VkRenderPass objects, ${wantObjects} expected")
        math(EXPR failures "${failures} + 1")
      endif()
      if(w EQUAL 1)
        # the path ran: B's passes, each begun as a VkRenderPass object
        rd_fixture_require(${name_${index}} "${out}" pass_objects)
        set(required_${index} TRUE)
      endif()
      message(STATUS "${dump} ${s} path ${w}: ${passes_${w}} passes, ${draws_${w}} draws, "
                     "${objects} as VkRenderPass objects")
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
foreach(index IN LISTS indices)
  if(NOT required_${index})
    message(SEND_ERROR "${name_${index}}: its properties were never checked (no setting could "
                       "show it): a fixture this test cannot measure is no test")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_pass_objects_ab: ${failures} failures over ${nentries} fixtures")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_pass_objects_ab: no target compared over ${nentries} fixtures")
endif()
message(STATUS "rd_pass_objects_ab: ${compared} targets the same bytes over ${nentries} fixtures")
