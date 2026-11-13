# rhi_vk_subpass_verdict: the subpass read decided at a Vulkan start by its
# verdict file (port/rhi/rhi_subpass_verdict.h, vk_device.c
# vkr_subpass_read_decide) with no [dev] subpass_read, on the 1.1 profile
# (ICO_VK_FAKE_API=1.1 in the test's environment: VkRenderPass objects, no
# local read).  First the class defaults (none probed but the Intel driver
# row's this release): lavapipe's vendor and the Arm fakes' are off by
# default with their words and write no file; then, with the tests' switch
# over the class (ICO_VK_SUBPASS_PROBE_CLASS=1),
# rhi_vk_test's subpass cell (rhi_vk_subpass.c) runs five times
# with ICO_VK_SUBPASS_VERDICT=FILE, ICO_TEST_SUBPASS_WANT saying whether the
# read must be on:
#   1. no file: on for the probe ("no verdict for this device and driver
#      yet"), the mark written; the cell writes no verdict, so the clean
#      shutdown takes the mark away ("its mark taken away") and FILE is gone;
#      the key is read from the device line ("device and driver K")
#   2. "v1 K kept: from the test": on, no mark ("kept by the probe of an
#      earlier start (from the test)"), FILE unchanged
#   3. "v1 K probing: the test's mark" (a start that ended inside its probe):
#      off ("refused for good"), FILE now "v1 K refused: the start that
#      probed it ended before its verdict"
#   4. that refusal: off ("refused by the probe of an earlier start")
#   5. another driver's "kept": on for the probe again, the mark away at the
#      end
# No Vulkan device (77): SKIP.  No validation error in any run.
# -DTOOL=<rhi_vk_test> -DFILE=<the verdict file, a path the test may write>
cmake_policy(SET CMP0054 NEW) # a quoted if() argument is a string, not a variable
function(run want out)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ICO_VK_SUBPASS_VERDICT=${FILE}"
                          "ICO_TEST_SUBPASS_WANT=${want}" ${classEnv} "${TOOL}" subpass
                  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no Vulkan device")
    set(${out} "SKIP" PARENT_SCOPE)
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "rhi_vk_test subpass (want ${want}) exited ${rc}:\n${o}\n${e}")
  endif()
  string(REPLACE ";" "," flat "${o}\n${e}")
  if(flat MATCHES "validation error")
    message(FATAL_ERROR "a validation error:\n${o}\n${e}")
  endif()
  set(${out} "${flat}" PARENT_SCOPE)
endfunction()

function(need text re what)
  if(NOT text MATCHES "${re}")
    message(FATAL_ERROR "${what}: no \"${re}\" in:\n${text}")
  endif()
endfunction()

file(REMOVE "${FILE}")
# 0. this release's class defaults (rhi_subpass_verdict.h): lavapipe's
# vendor is no class the probe runs on (only the Intel driver row's), and
# under the Arm fakes (ICO_VK_FAKE_VENDOR=0x13B5) Arm's: off by default with
# its words, no probe, no mark
set(classEnv "")
run(0 log)
if(log STREQUAL "SKIP")
  return()
endif()
need("${log}" "the subpass read off by default \\(probed on the Intel driver row only this release"
     "0 (lavapipe's class)")
set(classEnv "ICO_VK_FAKE_VENDOR=0x13B5")
run(0 log)
need("${log}" "the subpass read off by default \\(off on this driver this release: a new kind of GPU work at start-up on a driver that lost the device before, one pass a picture"
     "0 (Arm's class)")
if(EXISTS "${FILE}")
  message(FATAL_ERROR "0: a class off by default wrote ${FILE}")
endif()
# the rest with the tests' switch over the class's default
set(classEnv "ICO_VK_SUBPASS_PROBE_CLASS=1")
# 1. no file: the probe's start, the mark away at the clean shutdown
run(1 log)
need("${log}" "render passes: the subpass read: [^\n]*no verdict for this device and driver yet"
     "1 (no file)")
need("${log}" "device and driver ([0-9a-f]+:[0-9a-f]+:[0-9a-f]+)" "1 (the key)")
set(key "${CMAKE_MATCH_1}")
need("${log}" "its mark taken away, the next start probes" "1 (the mark)")
if(EXISTS "${FILE}")
  file(READ "${FILE}" left)
  message(FATAL_ERROR "1: the mark was left: ${left}")
endif()
# 2. kept
file(WRITE "${FILE}" "v1 ${key} kept: from the test\n")
run(1 log)
need("${log}" "kept by the probe of an earlier start \\(from the test\\)" "2 (kept)")
if(log MATCHES "its mark taken away")
  message(FATAL_ERROR "2: a mark taken away on a kept verdict:\n${log}")
endif()
file(READ "${FILE}" text)
if(NOT text STREQUAL "v1 ${key} kept: from the test\n")
  message(FATAL_ERROR "2: the kept verdict changed: ${text}")
endif()
# 3. the mark of a start that ended inside its probe
file(WRITE "${FILE}" "v1 ${key} probing: the test's mark\n")
run(0 log)
need("${log}" "the subpass read off \\(refused for good: the start that probed it ended before its verdict"
     "3 (ended inside)")
file(READ "${FILE}" text)
if(NOT text STREQUAL "v1 ${key} refused: the start that probed it ended before its verdict\n")
  message(FATAL_ERROR "3: the file after: ${text}")
endif()
# 4. refused
run(0 log)
need("${log}" "the subpass read off \\(refused by the probe of an earlier start \\(the start that probed it"
     "4 (refused)")
# 5. another driver's verdict: the probe again
file(WRITE "${FILE}" "v1 0000:00000000:00000000 kept: another driver\n")
run(1 log)
need("${log}" "no verdict for this device and driver yet" "5 (another driver)")
need("${log}" "its mark taken away" "5 (the mark)")
if(EXISTS "${FILE}")
  message(FATAL_ERROR "5: the mark was left")
endif()
message(STATUS "rhi_vk_subpass_verdict: none, kept, ended inside, refused and another driver's "
               "verdict decided as written (${key})")
