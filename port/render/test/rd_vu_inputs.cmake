# rd_vu_inputs.cmake: the frames the VU family's A/B scripts replay
# (rd_vustage_ab, rd_vubatch_ab, rd_vumerge_ab, rd_vufuse_ab, rd_vumerge_mali,
# rd_store_plan_ab, rd_thread_ab, rd_constant_reads_ab, rd_vu_env_ab), included
# by each.  Two forms:
#   fixtures   -DFIXTURE_TOOL, -DFIXTURES, -DFIXTURE_HELPERS and a fixture list
#              (port/render/test/fixtures/lists/*.txt): every listed fixture
#              expanded into ${OUT}/fixtures (docs/port/TESTING_FIXTURES.md);
#              a listed fixture that is absent or does not expand FAILS
#   paths      no FIXTURE_TOOL: a list of dumps the build writes (rd_pixel's
#              frames, rd_pixel_vu_dumps.txt; tools/ci_d3d11_warp.sh's copy of
#              it): one name a line relative to the list's folder; a name
#              that is not there is named and skipped
#
#   rd_vu_inputs(LIST [EXTRA])
#       sets vuDumps (the dumps in order, EXTRA's after LIST's, no repeats),
#       vuExtra (those of EXTRA), vuFx_<i> (the fixture's name of the i-th
#       dump, 1-based; "" in the paths form) and vuWords_<i> (the words after
#       its name in the list); in the fixtures form a fixture whose words hold
#       `extra` counts as EXTRA's
#   rd_vu_require(INDEX TEXT QUANTITY...)
#       the fixture of dump INDEX: rd_fixture_require on TEXT (a replay's
#       --stats output) and the fixture counted as checked; nothing in the
#       paths form
#   rd_vu_inputs_done(FAILVAR)
#       adds the fixtures' failures (rd_fixture_require's) and one for every
#       listed fixture no rd_vu_require checked to FAILVAR
set(vuFixtures FALSE)
if(DEFINED FIXTURE_TOOL AND NOT "${FIXTURE_TOOL}" STREQUAL "")
  set(vuFixtures TRUE)
  include("${FIXTURE_HELPERS}")
endif()

macro(rd_vu_inputs list)
  set(vuDumps "")
  set(vuExtra "")
  set(_vuLists "${list}")
  if(NOT "${ARGN}" STREQUAL "")
    list(APPEND _vuLists "${ARGN}")
  endif()
  set(_vuN 0)
  foreach(_vuList IN LISTS _vuLists)
    if(_vuList STREQUAL "")
      continue()
    endif()
    if(vuFixtures)
      rd_fixture_list("${_vuList}" _vuNames)
      foreach(_vuName IN LISTS _vuNames)
        rd_fixture_expand(${_vuName} "${OUT}/fixtures" _vuDump)
        list(FIND vuDumps "${_vuDump}" _vuAt)
        if(NOT _vuAt EQUAL -1)
          continue()
        endif()
        math(EXPR _vuN "${_vuN} + 1")
        list(APPEND vuDumps "${_vuDump}")
        set(vuFx_${_vuN} "${_vuName}")
        set(vuWords_${_vuN} "${RDFX_FIELDS_${_vuName}}")
        set(vuChecked_${_vuN} FALSE)
        separate_arguments(_vuW UNIX_COMMAND "${RDFX_FIELDS_${_vuName}}")
        list(FIND _vuW "extra" _vuAt)
        if(NOT _vuList STREQUAL "${list}" OR NOT _vuAt EQUAL -1)
          list(APPEND vuExtra "${_vuDump}")
        endif()
      endforeach()
    else()
      if(NOT EXISTS "${_vuList}")
        message(STATUS "${_vuList}: no dump list, skipped")
        continue()
      endif()
      get_filename_component(_vuDir "${_vuList}" DIRECTORY)
      file(STRINGS "${_vuList}" _vuLines)
      foreach(_vuL IN LISTS _vuLines)
        string(STRIP "${_vuL}" _vuL)
        if(_vuL STREQUAL "" OR _vuL MATCHES "^#")
          continue()
        endif()
        separate_arguments(_vuW UNIX_COMMAND "${_vuL}")
        list(POP_FRONT _vuW _vuDump)
        if(NOT IS_ABSOLUTE "${_vuDump}")
          set(_vuDump "${_vuDir}/${_vuDump}")
        endif()
        if(NOT EXISTS "${_vuDump}")
          message(STATUS "${_vuDump}: no such dump, skipped")
          continue()
        endif()
        list(FIND vuDumps "${_vuDump}" _vuAt)
        if(NOT _vuAt EQUAL -1)
          continue()
        endif()
        math(EXPR _vuN "${_vuN} + 1")
        list(APPEND vuDumps "${_vuDump}")
        set(vuFx_${_vuN} "")
        string(REPLACE ";" " " vuWords_${_vuN} "${_vuW}")
        set(vuChecked_${_vuN} TRUE)
        if(NOT _vuList STREQUAL "${list}")
          list(APPEND vuExtra "${_vuDump}")
        endif()
      endforeach()
    endif()
  endforeach()
endmacro()

function(rd_vu_require index text)
  if(vuFixtures AND NOT "${vuFx_${index}}" STREQUAL "")
    set(_vuName "${vuFx_${index}}")
    rd_fixture_require(${_vuName} "${text}" ${ARGN})
    set(RDFX_FAILURES ${RDFX_FAILURES} PARENT_SCOPE)
    set(RDFX_PROPS_${_vuName} "${RDFX_PROPS_${_vuName}}" PARENT_SCOPE)
    set(vuChecked_${index} TRUE PARENT_SCOPE)
  endif()
endfunction()

macro(rd_vu_inputs_done failVar)
  list(LENGTH vuDumps _vuN)
  if(vuFixtures AND _vuN GREATER 0)
    foreach(_vuI RANGE 1 ${_vuN})
      if(NOT vuChecked_${_vuI})
        message(SEND_ERROR "${vuFx_${_vuI}}: no replay of this test checked what it declares "
                           "(no setting could show it, or its runs failed)")
        math(EXPR ${failVar} "${${failVar}} + 1")
      endif()
    endforeach()
  endif()
  if(vuFixtures)
    math(EXPR ${failVar} "${${failVar}} + ${RDFX_FAILURES}")
  endif()
endmacro()
