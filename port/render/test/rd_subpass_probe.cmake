# rd_subpass_probe_<MODE>: the subpass read's first-start probe
# (port/render/rd_subpass_probe.c) on lavapipe through rd_fog_selftest_test
# (its first rd_fog_selftest runs DATE's self-test, whose end runs the
# probe), on the 1.1 profile with a tile-based profile (ICO_VK_FAKE_API=1.1,
# ICO_VK_FAKE_LIMITS=moltenvk in the test's environment: VkRenderPass
# objects, no local read, DATE's stencil path the profile's own) and no
# [dev] subpass_read: the device decides by the verdict file FILE
# (ICO_VK_SUBPASS_VERDICT), which starts absent, so the first run is the
# probe's start.  Two runs:
#   kept      ICO_RD_SUBPASS_PROBE_SABOTAGE=fast: "subpass read: kept (probe:
#             the read X vs the snapshot Y ms, a plain pass Z vs W ms without
#             the shape)", FILE "v1 K kept: probe: ..."; the second run (no
#             sabotage) "kept by the probe of an earlier start (probe: ...)"
#             and no probe line
#   refused   =slow: "refused (probe: the read slower, ...", "profile: DATE
#             stencil off (probe)", FILE "refused: probe: the read slower";
#             the second run "the subpass read off (refused by the probe of
#             an earlier start (probe: the read slower"
#   plain     =plain: "refused (probe: a plain pass slower with the shape, "
#   selftest  ICO_RD_DATE_SABOTAGE=1: DATE's self-test fails on the read:
#             "subpass read: refused (DATE's self-test on the read: stencil
#             fail", FILE "refused: DATE's self-test on the read: stencil fail"
#   end       =end: the first run ends inside the probe (exit 86) with FILE
#             "probing"; the second "the subpass read off (refused for good:
#             the start that probed it ended before its verdict", FILE
#             "refused: the start that probed it ended before its verdict"
# Each run but the end's exits 0 (the fog self-test's own checks, no
# validation error); none prints a line of another mode's verdict.  No
# device (77): SKIP.
# -DTOOL=<rd_fog_selftest_test> -DFILE=<a verdict file path> -DMODE=<mode>
cmake_policy(SET CMP0054 NEW) # a quoted if() argument is a string, not a variable
function(run sabotage dateSabotage out rcOut)
  set(envs "ICO_VK_SUBPASS_VERDICT=${FILE}")
  if(NOT sabotage STREQUAL "")
    list(APPEND envs "ICO_RD_SUBPASS_PROBE_SABOTAGE=${sabotage}")
  endif()
  if(dateSabotage)
    list(APPEND envs "ICO_RD_DATE_SABOTAGE=1")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envs} "${TOOL}" inplace
                  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
  string(REPLACE ";" "," flat "${o}\n${e}")
  set(${out} "${flat}" PARENT_SCOPE)
  set(${rcOut} "${rc}" PARENT_SCOPE)
endfunction()

function(need text re what)
  if(NOT text MATCHES "${re}")
    message(FATAL_ERROR "${MODE} ${what}: no \"${re}\" in:\n${text}")
  endif()
endfunction()

function(fileNeeds re what)
  if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "${MODE} ${what}: no verdict file")
  endif()
  file(READ "${FILE}" text)
  if(NOT text MATCHES "^v1 [0-9a-f]+:[0-9a-f]+:[0-9a-f]+ ${re}")
    message(FATAL_ERROR "${MODE} ${what}: the file holds \"${text}\", not \"${re}\"")
  endif()
endfunction()

set(sab "")
set(dsab FALSE)
if(MODE STREQUAL "kept")
  set(sab fast)
elseif(MODE STREQUAL "refused")
  set(sab slow)
elseif(MODE STREQUAL "plain")
  set(sab plain)
elseif(MODE STREQUAL "selftest")
  set(dsab TRUE)
elseif(MODE STREQUAL "end")
  set(sab end)
else()
  message(FATAL_ERROR "MODE ${MODE}: not kept, refused, plain, selftest or end")
endif()

file(REMOVE "${FILE}")
run("${sab}" ${dsab} log rc)
if(rc EQUAL 77)
  message(STATUS "SKIP: no usable Vulkan device")
  return()
endif()
need("${log}" "render passes: the subpass read: [^\n]*no verdict for this device and driver yet"
     "first run")
if(MODE STREQUAL "end")
  if(NOT rc EQUAL 86)
    message(FATAL_ERROR "end: the first run exited ${rc}, not 86:\n${log}")
  endif()
  need("${log}" "subpass read: ICO_RD_SUBPASS_PROBE_SABOTAGE=end" "first run")
  fileNeeds("probing: " "after the first run")
else()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the first run exited ${rc}:\n${log}")
  endif()
  if(log MATCHES "validation error")
    message(FATAL_ERROR "a validation error in the first run:\n${log}")
  endif()
endif()
if(MODE STREQUAL "kept")
  need("${log}" "subpass read: kept \\(probe: the read [0-9.]+ vs the snapshot [0-9.]+ ms, a plain pass [0-9.]+ vs [0-9.]+ ms without the shape\\)"
       "first run")
  need("${log}" "subpass read: ICO_RD_SUBPASS_PROBE_SABOTAGE=fast" "first run")
  fileNeeds("kept: probe: the read " "after the first run")
elseif(MODE STREQUAL "refused")
  need("${log}" "subpass read: refused \\(probe: the read slower, " "first run")
  need("${log}" "profile: DATE stencil off \\(probe\\)" "first run")
  fileNeeds("refused: probe: the read slower, " "after the first run")
elseif(MODE STREQUAL "plain")
  need("${log}" "subpass read: refused \\(probe: a plain pass slower with the shape, " "first run")
  fileNeeds("refused: probe: a plain pass slower with the shape, " "after the first run")
elseif(MODE STREQUAL "selftest")
  need("${log}" "subpass read: refused \\(DATE's self-test on the read: stencil fail" "first run")
  fileNeeds("refused: DATE's self-test on the read: stencil fail" "after the first run")
endif()
if(NOT MODE STREQUAL "kept" AND log MATCHES "subpass read: kept")
  message(FATAL_ERROR "${MODE}: a kept verdict:\n${log}")
endif()

# the second start: decided by the file, no probe
run("" FALSE log2 rc2)
if(NOT rc2 EQUAL 0)
  message(FATAL_ERROR "the second run exited ${rc2}:\n${log2}")
endif()
if(log2 MATCHES "validation error")
  message(FATAL_ERROR "a validation error in the second run:\n${log2}")
endif()
if(log2 MATCHES "subpass read: (kept|refused) \\(")
  message(FATAL_ERROR "the second run probed again:\n${log2}")
endif()
if(MODE STREQUAL "kept")
  need("${log2}" "render passes: the subpass read: [^\n]*kept by the probe of an earlier start \\(probe: the read "
       "second run")
  fileNeeds("kept: probe: the read " "after the second run")
elseif(MODE STREQUAL "end")
  need("${log2}" "the subpass read off \\(refused for good: the start that probed it ended before its verdict"
       "second run")
  fileNeeds("refused: the start that probed it ended before its verdict" "after the second run")
else()
  need("${log2}" "the subpass read off \\(refused by the probe of an earlier start \\(" "second run")
endif()
file(REMOVE "${FILE}")
message(STATUS "rd_subpass_probe_${MODE}: the probe's verdict written and the next start decided by it")
