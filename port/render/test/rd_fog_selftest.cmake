# rd_fog_selftest, rd_fog_selftest_fallback: rd_fog_selftest_test run, its
# exit code checked and its log matched (rd_fog_path.c):
#   "fog: depth path <path> (self-test: <results> ...)" once at the start
#   and once more after the scene is made at a new size, and
#   "fog: probe <path> z=<5 x GS Z> idx=136,0,15,240,255 out=<5 x RGBA>"
#   once for each of the two sizes (the centre cell and the four corner
#   cells of the self-test's grid).
# -DTOOL=<rd_fog_selftest_test> -DFOG_PATH=<inplace|copy|buffer>
# -DRESULTS=<regex of the self-test's results>
execute_process(COMMAND "${TOOL}" "${FOG_PATH}" RESULT_VARIABLE rc OUTPUT_VARIABLE out
                ERROR_VARIABLE err)
message(STATUS "${out}")
if(rc EQUAL 77)
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: no usable Vulkan device")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_fog_selftest_test ${FOG_PATH} exited ${rc}:\n${out}\n${err}")
endif()
string(REGEX MATCHALL "fog: depth path ${FOG_PATH} \\(self-test: ${RESULTS}" paths "${err}")
list(LENGTH paths npaths)
if(NOT npaths EQUAL 2)
  message(FATAL_ERROR "${npaths} \"fog: depth path ${FOG_PATH} (self-test: ${RESULTS}\" "
                      "lines, 2 expected:\n${err}")
endif()
set(hex "[0-9A-F]+")
string(REGEX MATCHALL
       "fog: probe ${FOG_PATH} z=${hex}(,${hex})(,${hex})(,${hex})(,${hex}) idx=136,0,15,240,255 out=${hex}(,${hex})(,${hex})(,${hex})(,${hex})"
       probes "${err}")
list(LENGTH probes nprobes)
if(NOT nprobes EQUAL 2)
  message(FATAL_ERROR "${nprobes} \"fog: probe ${FOG_PATH} ... idx=136,0,15,240,255\" lines, "
                      "2 expected:\n${err}")
endif()
message(STATUS "rd_fog_selftest ${FOG_PATH}: the path, the self-test's results and the probe "
               "logged")
