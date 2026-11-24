# rd_ratecard_<MODE>: the rate card (port/render/rd_ratecard_run.c) on
# lavapipe through rd_vuprepass_selftest_test (on, the plain variant: SCENE
# 640x448 at 1x), which runs it after the VU self-tests, with
# ICO_RD_RATECARD=1 (the tests' switch, the budget lifted) and its results
# file FILE (ICO_RATECARD_FILE), removed first, version v0.0.0:
#   measure  run 1: the block, a head line and one item line an item of
#            HEADER's enum (rd_ratecard.h, RD_RC_SHADE .. before RD_RC_ITEMS:
#            a new item needs no edit here), each a finite number in its unit
#            with its min and max, or "not measured (...)"; the items every
#            device can time (a shaded fragment of both builds, a hidden one
#            with discard, a vertex, a triangle, the quad factor, a pass, a
#            blended fragment and its ratio) measured or, a figure from a
#            difference of two frames, "not measured (a difference not above
#            0)" (a loaded CI runner's noise; the frames were still timed);
#            the twins' two measured when TWINS is 1 (the Arm
#            fakes without dual-source blending), "not measured (the twins
#            ..." otherwise; the fast pixel build's form through the ordinary
#            build measured everywhere, through the fast build and its
#            discard-free form measured when TWINS is 1 (its self-test passed
#            under the Arm fakes), "not measured (the fast pixel build ..."
#            otherwise; FILE "done <date>" with the key and the block.
#            Run 2: the block reprinted "(from the run of <date>)", nothing
#            measured, FILE unchanged.
#   end      run 1 under ICO_RD_RATECARD_SABOTAGE=end ends inside the card
#            (exit 87) with FILE "measuring <date>"; run 2 "rate card:
#            skipped (the run of <date> ended while measuring it ...", FILE
#            "blocked <date>"; run 3 skipped again.
# Whether a fragment hidden through the twin costs less than one with its
# discard is not asserted: lavapipe tests depth early either way.  No
# device (77): SKIP.
# -DTOOL=<rd_vuprepass_selftest_test> -DFILE=<a results file> -DMODE=<mode>
# -DHEADER=<rd_ratecard.h> [-DTWINS=1]
cmake_policy(SET CMP0054 NEW)
function(run sabotage out rcOut)
  set(envs "ICO_RD_RATECARD=1" "ICO_RATECARD_FILE=${FILE}" "ICO_RATECARD_VERSION=v0.0.0")
  if(NOT sabotage STREQUAL "")
    list(APPEND envs "ICO_RD_RATECARD_SABOTAGE=${sabotage}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envs} "${TOOL}" on plain
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

function(fileStarts word what)
  if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "${MODE} ${what}: no results file")
  endif()
  file(READ "${FILE}" text)
  if(NOT text MATCHES "^${word} [0-9-]+ [0-9:]+\nkey [^\n]+ \\| [0-9]+x[0-9]+ \\| v0.0.0\n")
    message(FATAL_ERROR "${MODE} ${what}: the file holds \"${text}\", not \"${word}\"")
  endif()
endfunction()

file(REMOVE "${FILE}")
set(num "-?[0-9]+\\.[0-9]+")
set(item "rd: rate card: [^\n]*: (${num} (ns|ms|x) \\(min ${num}, max ${num}\\)|not measured \\([^\n]*\\))")
# a figure from two frames' difference: measured, or not above 0 on a loaded runner
set(nd "not measured \\(a difference not above 0\\)")
# the item count: the enum's names from RD_RC_SHADE to RD_RC_ITEMS
file(READ "${HEADER}" hdr)
string(REGEX MATCH "enum {\n    RD_RC_SHADE,[^}]*\n    RD_RC_ITEMS\n" enumText "${hdr}")
string(REGEX MATCHALL "\n    RD_RC_[A-Z0-9_]+," enumNames "${enumText}")
list(LENGTH enumNames nItems)
if(nItems LESS 15)
  message(FATAL_ERROR "rd_ratecard.cmake: ${nItems} items read from ${HEADER}'s enum, 15 or more expected")
endif()
if(MODE STREQUAL "measure")
  run("" t1 rc1)
  if(rc1 EQUAL 77)
    message(STATUS "SKIP: no usable device")
    return()
  endif()
  if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "measure run 1 exited ${rc1}:\n${t1}")
  endif()
  need("${t1}" "rd: rate card: [^\n]* on SCENE 640x448 at 640x448 texels, [^\n]*, ([0-9.]+) ms in all" "the head")
  # the spread's words: the frame's extremes against the subtracted frame's median
  need("${t1}" "rd: rate card: [^\n]*the median of 5 timed replays a frame \\(min, max beside it: the frame's least and largest against the subtracted frame's median\\)" "the spread's words")
  string(REGEX MATCHALL "${item}" items "${t1}")
  list(LENGTH items n)
  if(NOT n EQUAL nItems)
    message(FATAL_ERROR "measure run 1: ${n} item lines, ${nItems} expected:\n${t1}")
  endif()
  foreach(re
          "a shaded fragment, vu_ps(_nodual)?: (${num} ns|${nd})"
          "a shaded fragment, vu_texa_ps(_nodual)? \\(TEXA, a linear filter\\): (${num} ns|${nd})"
          "a fragment hidden by depth, vu_ps(_nodual)? with its discard: (${num} ns|${nd})"
          "a vertex \\(a corner drawn off the target, the VU pre-pass (on|off)\\): (${num} ns|${nd})"
          "a triangle binned \\([^\n]*\\): (${num} ns|${nd})"
          "the 2x2-quad factor \\([^\n]*\\): (${num} x|not measured \\(its divisor not above 0\\)|${nd})"
          "a pass with nothing drawn \\([^\n]*\\): ${num} ms"
          "a blended fragment \\(LERP As, the (two-pass|dual-source) blend\\): (${num} ns|${nd})"
          "the blended fragment over the opaque one: (${num} x|not measured \\(its divisor not above 0\\)|${nd})"
          "a shaded fragment, vu_ps(_nodual)? on the fast build's form \\(RGBA32\\): (${num} ns|${nd})"
          "a MB loaded or stored: not measured \\(")
    need("${t1}" "rd: rate card: ${re}" "an item")
  endforeach()
  if(TWINS)
    need("${t1}" "rd: rate card: a shaded fragment, the fast pixel build vu_ps_nodual_fast: (${num} ns|${nd})" "the fast build")
    need("${t1}" "rd: rate card: a shaded fragment, its discard-free form vu_ps_nodual_fast_nd: (${num} ns|${nd})" "the fast build's discard-free form")
    need("${t1}" "rd: rate card: a shaded fragment, the twin vu_ps_nodual_nd: (${num} ns|${nd})" "the twin")
    # against one layer through the twin (its own baseline): a number, or a
    # difference not above 0 (lavapipe tests depth early either way); never
    # "its layers did not draw through the twin"
    need("${t1}" "rd: rate card: a fragment hidden by depth, through the twin: (${num} ns|${nd})" "the twin")
  else()
    need("${t1}" "rd: rate card: a shaded fragment, the twin vu_ps_nodual_nd: not measured \\(the twins " "no twin")
    need("${t1}" "rd: rate card: a shaded fragment, the fast pixel build vu_ps_nodual_fast: not measured \\(the fast pixel build " "no fast build")
  endif()
  fileStarts("done" "after run 1")
  file(READ "${FILE}" f1)
  need("${f1}" "\nrate card: a pass with nothing drawn" "the file's block")
  run("" t2 rc2)
  if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "measure run 2 exited ${rc2}:\n${t2}")
  endif()
  need("${t2}" "rd: rate card: a pass with nothing drawn \\([^\n]*\\): ${num} ms \\(min ${num}, max ${num}\\) \\(from the run of [0-9-]+ [0-9:]+\\)" "the reprint")
  if(t2 MATCHES "rd: rate card: a pass with nothing drawn \\([^\n]*\\): ${num} ms \\(min ${num}, max ${num}\\)\n")
    message(FATAL_ERROR "measure run 2 measured again:\n${t2}")
  endif()
  file(READ "${FILE}" f2)
  if(NOT f1 STREQUAL f2)
    message(FATAL_ERROR "measure run 2 changed the file:\n${f2}")
  endif()
elseif(MODE STREQUAL "end")
  run("end" t1 rc1)
  if(rc1 EQUAL 77)
    message(STATUS "SKIP: no usable device")
    return()
  endif()
  if(NOT rc1 EQUAL 87)
    message(FATAL_ERROR "end run 1 exited ${rc1}, 87 expected:\n${t1}")
  endif()
  need("${t1}" "rd: rate card: ICO_RD_RATECARD_SABOTAGE=end: the start ends inside it" "the end")
  fileStarts("measuring" "after run 1")
  foreach(k 2 3)
    run("" t rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "end run ${k} exited ${rc}:\n${t}")
    endif()
    need("${t}" "rd: rate card: skipped \\(the run of [0-9-]+ [0-9:]+ ended while measuring it on this device: not measured again until the device, driver, backend, size or version changes\\)" "the skip")
    if(t MATCHES "rd: rate card: a pass with nothing drawn")
      message(FATAL_ERROR "end run ${k} measured:\n${t}")
    endif()
    fileStarts("blocked" "after run ${k}")
  endforeach()
else()
  message(FATAL_ERROR "rd_ratecard.cmake: MODE ${MODE}?")
endif()
message(STATUS "rd_ratecard ${MODE}: passed")
