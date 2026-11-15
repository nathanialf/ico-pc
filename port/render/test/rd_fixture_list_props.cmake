# rd_fixture_list_props.cmake: a list line's own properties and words, for the
# -P test scripts that replay the committed fixtures (rd_fixture_helpers.cmake,
# included first; docs/port/TESTING_FIXTURES.md, "A list's own properties").
#
# A fixture declares what it carries (expect=).  What one test's settings make
# of a fixture another package owns (the glow's plan of a shadow fixture, its
# chains, its pastes) is declared on the test's own list line instead, as the
# same QUANTITY OP VALUE words after the name, so a second package never
# rewrites a fixture file the first one pinned.
#
#   rd_fixture_list_props(NAME)
#       the words after NAME on its list line (RDFX_FIELDS_<NAME>,
#       rd_fixture_list) that are QUANTITY OP VALUE join the fixture's own
#       declared properties for every later rd_fixture_require of NAME.  Call
#       it once a fixture, after rd_fixture_list and before the first
#       rd_fixture_require.  A macro: it sets RDFX_PROPS_<NAME> in the
#       script's scope, which rd_fixture_require reads
#   rd_fixture_list_word(NAME WORD OUT)
#       OUT TRUE when the line of NAME holds WORD as a word of its own (a
#       test's switch for that fixture, e.g. nocull)
macro(rd_fixture_list_props _rdlp_name)
  execute_process(COMMAND "${FIXTURE_TOOL}" props "${FIXTURES}/${_rdlp_name}.rdfix"
                  RESULT_VARIABLE _rdlp_rc OUTPUT_VARIABLE _rdlp_o ERROR_VARIABLE _rdlp_e)
  if(NOT _rdlp_rc EQUAL 0)
    message(FATAL_ERROR "${_rdlp_name}: cannot read its properties (exit ${_rdlp_rc}): ${_rdlp_e}")
  endif()
  string(REGEX REPLACE "\n$" "" _rdlp_o "${_rdlp_o}")
  string(REPLACE "\n" ";" _rdlp_o "${_rdlp_o}")
  string(REGEX REPLACE "[ \t]+" ";" _rdlp_w "${RDFX_FIELDS_${_rdlp_name}}")
  foreach(_rdlp_p IN LISTS _rdlp_w)
    if(_rdlp_p MATCHES "^([a-z0-9_]+)(>=|<=|!=|>|<|=)(.+)$")
      # the line's word for a quantity replaces the fixture's own (one test's
      # settings, e.g. the cull off, may change what the fixture shows)
      set(_rdlp_q "${CMAKE_MATCH_1}")
      set(_rdlp_keep "")
      foreach(_rdlp_old IN LISTS _rdlp_o)
        if(NOT _rdlp_old MATCHES "^${_rdlp_q}(>=|<=|!=|>|<|=)")
          list(APPEND _rdlp_keep "${_rdlp_old}")
        endif()
      endforeach()
      set(_rdlp_o "${_rdlp_keep}")
      list(APPEND _rdlp_o "${_rdlp_p}")
    endif()
  endforeach()
  set(RDFX_PROPS_${_rdlp_name} "${_rdlp_o}")
endmacro()

function(rd_fixture_list_word name word outVar)
  string(REGEX REPLACE "[ \t]+" ";" words "${RDFX_FIELDS_${name}}")
  list(FIND words "${word}" at)
  if(at LESS 0)
    set(${outVar} FALSE PARENT_SCOPE)
  else()
    set(${outVar} TRUE PARENT_SCOPE)
  endif()
endfunction()
