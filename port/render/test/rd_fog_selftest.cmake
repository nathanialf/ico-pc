# rd_fog_selftest, rd_fog_selftest_all, rd_fog_selftest_fallback:
# rd_fog_selftest_test run, its exit code checked and its log matched
# (rd_fog_path.c):
#   "fog: depth path <path> (self-test: <results> ...)" once at the start
#   and once more after the scene is made at a new size (the results in
#   the platform's order up to the first path that passes, "not tried" for
#   the rest, every path with ICO_RD_FOG_PATH=test), and
#   "fog: probe <path> z=<5 x GS Z> idx=<centre>,0,15,240,255 out=<5 x RGBA>"
#   once for each of the two sizes (the centre cell and the four corner
#   cells of the self-test's grid; the centre texel can sit on the edge of
#   cells 7 and 8 across or down, so the centre is 119, 120, 135 or 136).
#   On Direct3D 11 (the log has "rhi_d3d11:" lines) z and idx read n/a: that
#   backend has no depth region copy into a device buffer.
# -DTOOL=<rd_fog_selftest_test> -DFOG_PATH=<inplace|copy|buffer>
# -DARGS=<w|h|scale|aa> (optional: the scene's GS size, scale and RdAntiAlias)
# -DRESULTS=<regex of the self-test's results>
# -DEXTRA=, -DEXTRA2=<regex> (optional: each must match the log at least once;
#   the sabotage tests' failed result and the profile's "(self-test)" line)
if(ARGS)
  string(REPLACE "|" ";" ARGS "${ARGS}")
endif()
execute_process(COMMAND "${TOOL}" "${FOG_PATH}" ${ARGS} RESULT_VARIABLE rc OUTPUT_VARIABLE out
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
       "fog: probe ${FOG_PATH} z=${hex}(,${hex})(,${hex})(,${hex})(,${hex}) idx=(119|120|135|136),0,15,240,255 out=${hex}(,${hex})(,${hex})(,${hex})(,${hex})"
       probes "${err}")
list(LENGTH probes nprobes)
if(NOT nprobes EQUAL 2 AND err MATCHES "rhi_d3d11: ")
  # Direct3D 11 copies a depth texture only into a readback buffer, never a
  # region of it into a device buffer, so the probe has no depth texels there
  # (rd_fog_path.c depthRegionCopies): z and idx read n/a, the colours are read
  # as everywhere.  The fogged grid check above is what proves the fog.
  string(REGEX MATCHALL
         "fog: probe ${FOG_PATH} z=n/a idx=n/a out=${hex}(,${hex})(,${hex})(,${hex})(,${hex})"
         probes "${err}")
  list(LENGTH probes nprobes)
endif()
if(NOT nprobes EQUAL 2)
  message(FATAL_ERROR "${nprobes} \"fog: probe ${FOG_PATH} ... idx=<centre>,0,15,240,255\" lines, "
                      "2 expected:\n${err}")
endif()
foreach(_x EXTRA EXTRA2)
  if(${_x})
    string(REGEX MATCHALL "${${_x}}" xs "${err}")
    list(LENGTH xs nx)
    if(nx EQUAL 0)
      message(FATAL_ERROR "no \"${${_x}}\" line:\n${err}")
    endif()
  endif()
endforeach()
message(STATUS "rd_fog_selftest ${FOG_PATH}: the path, the self-test's results and the probe "
               "logged")
