# rd_subpass_ab: the subpass read on against off, byte for byte, at the
# reporters' profiles (v0.11.6: the read decided per device by its probe,
# port/render/rd_subpass_probe.c, port/rhi/rhi_subpass_verdict.h).  The frames:
# every fixture of the list (port/render/test/fixtures/lists/subpass.txt: a
# fixture's name and the profiles it is replayed under, comma-separated; the
# frames are expanded into OUT/fixtures, docs/port/TESTING_FIXTURES.md; a
# listed fixture that is absent FAILS).  The profiles, both on VkRenderPass
# objects (ICO_VK_FAKE_API=1.1: the read's route):
#   mali   issue 179's Mali-G52: the Arm fakes (ICO_VK_FAKE_LIMITS=mali,
#          ICO_VK_FAKE_VENDOR=0x13B5, no dual-source blending:
#          ICO_VK_FAKE_NO_DUAL=1), the record by compute dispatches
#          (ICO_RD_VU_RECORD=compute), 1x at 21:9
#   pixel  the MoltenVK fakes (ICO_VK_FAKE_LIMITS=moltenvk: a tile-based
#          profile without the attachment read), Enhanced at 2364x1080, full
#          height, full pixel, presented at 2364x1080
# Each dump at each of its profiles: rd_replay_tool --pass-dump --stats
# twice, the read on (ICO_VK_SUBPASS_READ=1, the shape the probe's start and
# a kept verdict make; DATE through the stencil bit, ICO_RD_DATE_PATH=stencil)
# and off (=0, DATE by the R8 snapshot as today), the fog in place in both
# (ICO_RD_FOG_PATH=inplace: the read has no fog read).  Every target's PNG
# equal; the on run says "render passes: the subpass read:", the off run
# "the subpass read off"; the on run draws no DATE snapshot and no fewer
# stencil draws than the off run drew snapshots, with fewer passes by at
# least the off run's snapshots (each one ended SCENE's pass and began it
# again; rd_date_subpass_ab's rule); no validation error.  Every profile
# must have compared targets.
# The path ran (rd_fixture_require, every fixture at every profile of its):
# the fixture's declared date_stencil_draws on the on run (DATE drawn through
# the bit by the subpass read) and date_snapshots on the off run.  A fixture
# whose list line says "nodate" is a shadow window with no volume (its
# composites are skipped, so it draws no DATE): the pair must draw no DATE
# either way (0 snapshots off, 0 stencil draws on) and its declared
# shadow_volume_draws (=0) is checked instead; the list must name at least one
# fixture that draws DATE at each profile.  No device (77): SKIP.
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures' folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<the
# fixture list> -DOUT=<a folder the test may write>
cmake_policy(SET CMP0057 NEW) # if(... IN_LIST ...) in a -P script
cmake_policy(SET CMP0054 NEW) # a quoted if() argument is a string, not a variable
include("${FIXTURE_HELPERS}")
set(failures 0)
set(entries "")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" dump_${name})
  string(REGEX REPLACE "[ \t]+" ";" fields "${RDFX_FIELDS_${name}}")
  set(profs "")
  set(nodate_${name} FALSE)
  foreach(f IN LISTS fields)
    if(f STREQUAL "nodate")
      set(nodate_${name} TRUE)
    else()
      string(REPLACE "," ";" profs "${f}")
    endif()
  endforeach()
  set(n 0)
  foreach(p IN LISTS profs)
    if(p STREQUAL "mali" OR p STREQUAL "pixel")
      list(APPEND entries "${p}|${name}")
      math(EXPR n "${n} + 1")
    else()
      message(SEND_ERROR "${name}: the list names a profile '${p}' (mali or pixel)")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
  if(n EQUAL 0)
    message(SEND_ERROR "${name}: the list names no profile for it")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES entries)

set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5 ICO_VK_FAKE_NO_DUAL=1
             ICO_RD_VU_RECORD=compute)
set(env_pixel ICO_VK_FAKE_LIMITS=moltenvk)
set(args_mali --aspect 21:9)
set(args_pixel --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(dateRe "stats: DATE snapshots ([0-9]+), DATE stencil draws ([0-9]+) \\(([0-9]+) setups\\)")
set(passRe "stats: [0-9]+ draws, ([0-9]+) passes,")
set(compared_mali 0)
set(compared_pixel 0)
set(date_mali 0)
set(date_pixel 0)
set(asked_mali 0)
set(asked_pixel 0)
set(index 0)
foreach(e IN LISTS entries)
  string(REGEX REPLACE "\\|.*$" "" prof "${e}")
  string(REGEX REPLACE "^[^|]*\\|" "" name "${e}")
  set(dump "${dump_${name}}")
  math(EXPR index "${index} + 1")
  math(EXPR asked_${prof} "${asked_${prof}} + 1")
  set(ok TRUE)
  foreach(run on off)
    set(dir_${run} "${OUT}/${index}-${prof}-${run}")
    file(REMOVE_RECURSE "${dir_${run}}")
    file(MAKE_DIRECTORY "${dir_${run}}")
    if(run STREQUAL "on")
      set(sub 1)
      set(path stencil)
      set(subRe "render passes: the subpass read:")
    else()
      set(sub 0)
      set(path snapshot)
      set(subRe "render passes: the subpass read off")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_VK_FAKE_API=1.1 ${env_${prof}}
                            ICO_RD_FOG_PATH=inplace ICO_VK_SUBPASS_READ=${sub}
                            ICO_RD_DATE_PATH=${path} "${TOOL}" "${dump}" - --pass-dump
                            "${dir_${run}}" --stats ${args_${prof}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    if(NOT rc EQUAL 0)
      message(SEND_ERROR "${dump} ${prof} ${run}: exit ${rc}:\n${err}")
      set(ok FALSE)
      break()
    endif()
    string(REPLACE ";" "," flat "${out}\n${err}")
    if(flat MATCHES "validation error")
      message(SEND_ERROR "${dump} ${prof} ${run}: a validation error:\n${err}")
      set(ok FALSE)
      break()
    endif()
    if(NOT flat MATCHES "${subRe}")
      message(SEND_ERROR "${dump} ${prof} ${run}: no \"${subRe}\" line:\n${err}")
      set(ok FALSE)
      break()
    endif()
    if(NOT flat MATCHES "${dateRe}")
      message(SEND_ERROR "${dump} ${prof} ${run}: no DATE stats line:\n${out}")
      set(ok FALSE)
      break()
    endif()
    set(snaps_${run} "${CMAKE_MATCH_1}")
    set(bits_${run} "${CMAKE_MATCH_2}")
    if(NOT flat MATCHES "${passRe}")
      message(SEND_ERROR "${dump} ${prof} ${run}: no passes stats line:\n${out}")
      set(ok FALSE)
      break()
    endif()
    set(passes_${run} "${CMAKE_MATCH_1}")
    set(out_${run} "${out}")
  endforeach()
  if(NOT ok)
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  math(EXPR fewer "${passes_off} - ${passes_on}")
  message(STATUS "${dump} (${prof}): on ${passes_on} passes, ${snaps_on} DATE snapshots, "
                 "${bits_on} stencil draws; off ${passes_off} passes, ${snaps_off} snapshots")
  if(nodate_${name})
    rd_fixture_require(${name} "${out_on}" shadow_volume_draws)
    if(NOT snaps_off EQUAL 0 OR NOT bits_on EQUAL 0)
      message(SEND_ERROR "${dump} (${prof}): the list says nodate, the off run drew "
                         "${snaps_off} snapshots and the on run ${bits_on} stencil draws")
      math(EXPR failures "${failures} + 1")
    endif()
  else()
    rd_fixture_require(${name} "${out_on}" date_stencil_draws)
    rd_fixture_require(${name} "${out_off}" date_snapshots)
    math(EXPR date_${prof} "${date_${prof}} + 1")
  endif()
  if(NOT bits_off EQUAL 0 OR NOT snaps_on EQUAL 0)
    message(SEND_ERROR "${dump} (${prof}): the off run drew ${bits_off} DATE draws by the bit, "
                       "the on run ${snaps_on} snapshots")
    math(EXPR failures "${failures} + 1")
  endif()
  if(snaps_off GREATER 0 AND (bits_on LESS snaps_off OR fewer LESS snaps_off))
    message(SEND_ERROR "${dump} (${prof}): ${bits_on} stencil draws and ${fewer} passes fewer "
                       "against the off run's ${snaps_off} snapshots")
    math(EXPR failures "${failures} + 1")
  endif()
  file(GLOB pngs RELATIVE "${dir_off}" "${dir_off}/*.png")
  list(LENGTH pngs npng)
  if(npng EQUAL 0)
    message(SEND_ERROR "${dump} (${prof}): no target written")
    math(EXPR failures "${failures} + 1")
  endif()
  foreach(png IN LISTS pngs)
    if(NOT EXISTS "${dir_on}/${png}")
      message(SEND_ERROR "${dump} (${prof}): ${png} written by the off run only")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir_off}/${png}"
                            "${dir_on}/${png}" RESULT_VARIABLE same)
    math(EXPR compared_${prof} "${compared_${prof}} + 1")
    if(NOT same EQUAL 0)
      message(SEND_ERROR "${dump} (${prof}): ${png} differs between the read on and off")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
foreach(p mali pixel)
  if(compared_${p} EQUAL 0 OR date_${p} EQUAL 0)
    message(SEND_ERROR "the ${p} profile compared ${compared_${p}} targets over "
                       "${asked_${p}} fixtures, ${date_${p}} drawing DATE through the read")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_subpass_ab: ${failures} failures")
endif()
message(STATUS "rd_subpass_ab: mali ${compared_mali}, pixel ${compared_pixel} targets byte for "
               "byte with the subpass read on and off")
