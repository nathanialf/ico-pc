# rd_blend_selftest*: rd_blend_selftest_test run, its exit code checked and
# its log matched (rd_blend_selftest.c, rd_dst_read.c, vk_device.c):
#   "blend: <name> (self-test: <results>) ... in N ms" twice: at the start
#   and after the scene is made at a new size (the results: the exact mode
#   [dev] blend opted into, then the fixed-function reference and its
#   two-pass fallback after "->", then "; kept by [dev] blend" or "; exact
#   modes not asked for: ..."; a ";" is matched as ",");
#   FAIL: a "blend: self-test: <entry> fail <n> at <sample>" line (a
#   sabotaged entry names its worst sample);
#   DENY: the device line "destination read: ... attachment read off on
#   this driver (Qualcomm driver ...)" (the fake Adreno);
#   EXTRA, EXTRA2: any other lines the log must hold (unflattened: write
#   "." for a ";").
# -DTOOL=<rd_blend_selftest_test> -DMODE=<attachment|storage|fixed>
# -DNAME=<the blend's log name, a regex> -DRESULTS=<regex of the results>
# [-DFAIL=<regex>] [-DDENY=1] [-DEXTRA=<regex>] [-DEXTRA2=<regex>]
# (rd_blend_selftest_moltenvk_storage: ICO_VK_FAKE_LIMITS=moltenvk, skipped
# on a device without the interlock)
execute_process(COMMAND "${TOOL}" "${MODE}" RESULT_VARIABLE rc OUTPUT_VARIABLE out
                ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  message(STATUS "SKIP: no usable Vulkan device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_blend_selftest_test ${MODE} exited ${rc}:\n${out}\n${err}")
endif()
# the matches are counted as list items: a ";" in a line (the log's
# "; kept by", "; attachment read off") would split one, so they are
# counted on the log with ";" made ","
string(REPLACE ";" "," flat "${err}")
string(REGEX MATCHALL "blend: ${NAME} \\(self-test: ${RESULTS}\\)[^\n]* in [0-9.]+ ms" lines
       "${flat}")
list(LENGTH lines n)
if(NOT n EQUAL 2)
  message(FATAL_ERROR "${n} \"blend: ${NAME} (self-test: ${RESULTS})\" lines, 2 expected:\n${err}")
endif()
if(FAIL)
  if(NOT err MATCHES "blend: self-test: ${FAIL} at [a-z ,]+[0-9]* \\(texel [0-9]+,[0-9]+\\): ")
    message(FATAL_ERROR "no \"blend: self-test: ${FAIL} at ...\" line:\n${err}")
  endif()
endif()
if(DENY)
  if(NOT err MATCHES "destination read: interlock (yes|no), rasterization-order attachment read off on this driver \\(Qualcomm driver [^:]*: wrong results on Adreno; ICO_VK_ATTACHMENT_READ=1 tries it\\)")
    message(FATAL_ERROR "no deny line for the attachment read:\n${err}")
  endif()
endif()
if(EXTRA)
  if(NOT err MATCHES "${EXTRA}")
    message(FATAL_ERROR "no line matching \"${EXTRA}\":\n${err}")
  endif()
endif()
if(EXTRA2)
  if(NOT err MATCHES "${EXTRA2}")
    message(FATAL_ERROR "no line matching \"${EXTRA2}\":\n${err}")
  endif()
endif()
message(STATUS "rd_blend_selftest ${MODE}: the blend and the self-test's results logged")
