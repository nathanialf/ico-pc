# rd_shadow_count_check, rd_shadow_count_check_sabotage: the check of the
# shadow count's forms (port/render/rd_shadow_count.h "the check";
# rd_shadowres.c checkDraw): issue 123's tablet path on the main gate
# fixture fx-adreno (ICO_VK_FAKE_PASS_ROW=qualcomm ICO_RD_VOLUME_FS=1
# ICO_RD_COMP_SPEC=1, Custom 3x, Full height, Full pixel, presented
# 3048x2032), four presented replays in a row (--repeat 4): the first draws
# the check besides its picture, the third (the first to reuse its frame
# slot) compares its readback.  -DSABOTAGE=0: the line "shadow count: the
# check at the first shadowed scene: the first level from the stencil equal
# to the full-size pass's at all N texels of WxH; the seven draws' equal
# too" (N > 0), no "differs".  -DSABOTAGE=1 (ICO_RD_SHADOW_COUNT_SABOTAGE=1:
# the step from the stencil reads an empty box): "... differs from the
# full-size pass's at N of M texels" with N > 0 and "above 1x the full-size
# pass for this run".  The line itself is the path having run.  No device:
# SKIP (77).  -DTOOL -DFIXTURE_TOOL -DFIXTURES -DFIXTURE_HELPERS -DOUT
# -DSABOTAGE
include("${FIXTURE_HELPERS}")
rd_fixture_expand(fx-adreno "${OUT}/fixtures" dump)
set(envs ICO_VK_FAKE_PASS_ROW=qualcomm ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1)
if(SABOTAGE)
  list(APPEND envs ICO_RD_SHADOW_COUNT_SABOTAGE=1)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envs}
                        "${TOOL}" "${dump}" "${OUT}/check.png" --repeat 4 --enhanced
                        --resolution 3x --full-height --full-pixel --present 3048x2032
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77)
  message(STATUS "SKIP: no device: ${err}")
  return()
endif()
string(REGEX MATCHALL "shadow count: the check[^\n]*" lines "${err}")
foreach(l IN LISTS lines)
  message(STATUS "${l}")
endforeach()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_shadow_count_check: exit ${rc}\n${err}")
endif()
set(eqRe "shadow count: the check at the first shadowed scene: the first level from the stencil equal to the full-size pass's at all ([0-9]+) texels of [0-9]+x[0-9]+; the seven draws' equal too")
set(diffRe "shadow count: the check at the first shadowed scene: the first level from the stencil differs from the full-size pass's at ([0-9]+) of ([0-9]+) texels")
set(runRe "above 1x the full-size pass for this run")
if(SABOTAGE)
  if(NOT err MATCHES "${diffRe}" OR CMAKE_MATCH_1 EQUAL 0 OR NOT err MATCHES "${runRe}")
    message(FATAL_ERROR "rd_shadow_count_check_sabotage: the wrong step from the stencil was not "
                        "refused:\n${err}")
  endif()
else()
  if(NOT err MATCHES "${eqRe}" OR CMAKE_MATCH_1 EQUAL 0 OR err MATCHES "${diffRe}")
    message(FATAL_ERROR "rd_shadow_count_check: no passed check (or a difference):\n${err}")
  endif()
endif()
message(STATUS "rd_shadow_count_check (sabotage ${SABOTAGE}): ok")
