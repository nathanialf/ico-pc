# rd_stencil_ab: the stencil loaded and stored with the depth on every pass
# (ICO_RD_STENCIL_PACKED=1, v0.11.4's tile-based GPUs) against inside the
# shadow window only (=0; port/render/rd_stencil_selftest.c,
# docs/port/WORK_BUFFERS.md "The stencil outside the shadow window"), byte for
# byte.  For every dump the list names (tools/stencil_dumps.txt: a path, '*'
# globs allowed; a listed dump that is absent FAILS), rd_replay_tool's
# --pass-dump with --stats under the store plan (ICO_RD_STORE_PLAN=1) and its
# poison (ICO_RD_STORE_POISON=1: a stencil load the cut leaves undefined
# clears to 0x5A, so a stencil the cut lost would show; lavapipe keeps
# DONT_CARE contents), the programs' own entries (ICO_RD_VU_PREPASS=0), at:
#   phone  the PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=
#          moltenvk: a tile-based class with the local read, the fog's local
#          path, DATE through the stencil bit, the shadow volumes in the
#          scene's pass): Enhanced, 2364x1080, Full height, Full pixel,
#          presented at 2364x1080;
#   mali   the Mali-G52's (issue 179): 1x at the dump's own aspect, presented
#          at 1612x720, the fake Mali limits without the in-pass read
#          (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_NO_DST=1: the fog's copy path,
#          DATE by the snapshot);
# a dump a setting cannot show ("cannot show") is skipped there.  Each pair:
#   - every PNG the =1 run writes (SCENE, DISPLAY, PRESENT, SHADOW0-2 and
#     every other target) the =0 run's byte for byte, the same number;
#   - the same draws and passes; colour MB loaded and stored and MB copied
#     the same (the cut moves stencil only; the poison is applied after the
#     pass area and the shadow merge's arm read the ops);
#   - the =1 run says "with the depth" and asks every stencil store and load
#     (0 and 0 not asked, 0.00 MB); the =0 run says "inside the shadow window
#     only";
#   - the depth and stencil MB stored fall by exactly the =0 run's "MB
#     stored" not asked and the MB loaded by its "MB loaded", within two
#     hundredths (each of the three figures rounded to two decimals); the MB
#     cleared rise by at most its "MB loaded" (each load the cut poisons is a
#     clear; the store plan's own poison clears the stencil of a pass it
#     left undefined in the =1 run only, where that stencil was asked);
# and over the dumps each setting that compared a pair leaves out at least
# one stencil store with =0, and the phone setting compares at least one.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
set(dumps "")
set(failures 0)
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_stencil_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${listDir}/${l}")
  endif()
  if(l MATCHES "[*]")
    file(GLOB found "${l}")
    list(SORT found)
    if(NOT found)
      message(SEND_ERROR "${l}: no dump matches (a listed dump must exist)")
      math(EXPR failures "${failures} + 1")
    endif()
    list(APPEND dumps ${found})
  elseif(EXISTS "${l}")
    list(APPEND dumps "${l}")
  else()
    message(SEND_ERROR "${l}: no such dump (a listed dump must exist)")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)

set(common ICO_RD_STORE_PLAN=1 ICO_RD_STORE_POISON=1 ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(args_mali --present 1612x720)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1)
set(settings phone mali)

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored, depth and stencil ([0-9.]+)/([0-9.]+) MB loaded/stored, ([0-9.]+) MB cleared, ([0-9.]+) MB copied")
set(stencilRe "stats: stencil (with the depth|inside the shadow window only): ([0-9]+) stores and ([0-9]+) loads not asked with the depth's \\(([0-9]+\\.[0-9][0-9]) MB stored, ([0-9]+\\.[0-9][0-9]) MB loaded\\)")

# a MB figure with two decimals in hundredths
function(mb_hundredths out v)
  string(REGEX REPLACE "^([0-9]+)\\.([0-9][0-9])$" "\\1\\2" h "${v}")
  math(EXPR h "${h}")
  set(${out} "${h}" PARENT_SCOPE)
endfunction()

# |a - b| <= 2
function(near out a b)
  math(EXPR d "${a} - ${b}")
  if(d LESS -2 OR d GREATER 2)
    set(${out} FALSE PARENT_SCOPE)
  else()
    set(${out} TRUE PARENT_SCOPE)
  endif()
endfunction()

set(compared 0)
set(index 0)
foreach(s IN LISTS settings)
  set(cutStores_${s} 0)
  set(pairs_${s} 0)
endforeach()
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(packed 1 0)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${packed}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}}
                              ICO_RD_STENCIL_PACKED=${packed}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND packed EQUAL 1 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} packed ${packed}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      set(ok TRUE)
      foreach(re drawsRe trafficRe stencilRe)
        if(NOT flat MATCHES "${${re}}")
          message(SEND_ERROR "${dump} ${s} packed ${packed}: no ${re} line:\n${out}")
          math(EXPR failures "${failures} + 1")
          set(ok FALSE)
        endif()
      endforeach()
      if(NOT ok)
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${packed} "${CMAKE_MATCH_1}")
      set(passes_${packed} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(cl_${packed} "${CMAKE_MATCH_1}")
      set(cs_${packed} "${CMAKE_MATCH_2}")
      set(dl_${packed} "${CMAKE_MATCH_3}")
      set(ds_${packed} "${CMAKE_MATCH_4}")
      set(cc_${packed} "${CMAKE_MATCH_5}")
      set(cp_${packed} "${CMAKE_MATCH_6}")
      string(REGEX MATCH "${stencilRe}" m "${flat}")
      set(word_${packed} "${CMAKE_MATCH_1}")
      set(ns_${packed} "${CMAKE_MATCH_2}")
      set(nl_${packed} "${CMAKE_MATCH_3}")
      set(ms_${packed} "${CMAKE_MATCH_4}")
      set(ml_${packed} "${CMAKE_MATCH_5}")
      message(STATUS "${dump} ${s} packed ${packed}: ${passes_${packed}} passes, "
                     "${draws_${packed}} draws; depth and stencil ${dl_${packed}}/${ds_${packed}} "
                     "MB loaded/stored, ${cc_${packed}} MB cleared; stencil ${word_${packed}}: "
                     "${ns_${packed}} stores (${ms_${packed}} MB) and ${nl_${packed}} loads "
                     "(${ml_${packed}} MB) not asked")
    endforeach()
    if(skipped)
      continue()
    endif()
    # the pair
    if(NOT word_1 STREQUAL "with the depth" OR NOT ns_1 EQUAL 0 OR NOT nl_1 EQUAL 0 OR
       NOT ms_1 STREQUAL "0.00" OR NOT ml_1 STREQUAL "0.00")
      message(SEND_ERROR "${dump} ${s}: packed 1 says the stencil ${word_1}, ${ns_1} stores and "
                         "${nl_1} loads not asked: with the depth, 0 and 0 expected")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT word_0 STREQUAL "inside the shadow window only")
      message(SEND_ERROR "${dump} ${s}: packed 0 says the stencil ${word_0}")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR cutStores_${s} "${cutStores_${s}} + ${ns_0}")
    math(EXPR pairs_${s} "${pairs_${s}} + 1")
    if(NOT draws_0 EQUAL draws_1 OR NOT passes_0 EQUAL passes_1)
      message(SEND_ERROR "${dump} ${s}: ${draws_0} draws, ${passes_0} passes with the cut; "
                         "${draws_1} and ${passes_1} without")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT cl_0 STREQUAL cl_1 OR NOT cs_0 STREQUAL cs_1 OR NOT cp_0 STREQUAL cp_1)
      message(SEND_ERROR "${dump} ${s}: colour ${cl_0}/${cs_0} MB, copies ${cp_0} MB with the "
                         "cut; ${cl_1}/${cs_1} and ${cp_1} without (the cut moves stencil only)")
      math(EXPR failures "${failures} + 1")
    endif()
    mb_hundredths(l1 "${dl_1}")
    mb_hundredths(l0 "${dl_0}")
    mb_hundredths(s1 "${ds_1}")
    mb_hundredths(s0 "${ds_0}")
    mb_hundredths(c1 "${cc_1}")
    mb_hundredths(c0 "${cc_0}")
    mb_hundredths(wantS "${ms_0}")
    mb_hundredths(wantL "${ml_0}")
    math(EXPR fallS "${s1} - ${s0}")
    math(EXPR fallL "${l1} - ${l0}")
    math(EXPR riseC "${c0} - ${c1}")
    near(okS "${fallS}" "${wantS}")
    near(okL "${fallL}" "${wantL}")
    math(EXPR mostC "${wantL} + 2")
    set(okC TRUE)
    if(riseC GREATER mostC)
      set(okC FALSE)
    endif()
    if(NOT okS OR NOT okL OR NOT okC)
      message(SEND_ERROR "${dump} ${s}: depth and stencil ${dl_1}/${ds_1} -> ${dl_0}/${ds_0} MB "
                         "loaded/stored, cleared ${cc_1} -> ${cc_0} MB: the cut names ${ms_0} MB "
                         "of stores and ${ml_0} MB of loads not asked (the loads poisoned to "
                         "clears)")
      math(EXPR failures "${failures} + 1")
    endif()
    set(dirOff "${OUT}/${index}-${s}-1")
    set(dirOn "${OUT}/${index}-${s}-0")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written with the stencil forced, "
                         "${npngOn} with the cut")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the cut: it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
foreach(s IN LISTS settings)
  if(pairs_${s} GREATER 0 AND cutStores_${s} EQUAL 0)
    message(SEND_ERROR "rd_stencil_ab: the ${s} setting left out no stencil store over "
                       "${pairs_${s}} pairs")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
if(pairs_phone EQUAL 0)
  message(SEND_ERROR "rd_stencil_ab: the phone setting compared no pair")
  math(EXPR failures "${failures} + 1")
endif()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_stencil_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_stencil_ab: no target compared over ${ndumps} dumps")
endif()
message(STATUS "rd_stencil_ab: ${compared} targets the same bytes over ${ndumps} dumps; stencil "
               "stores left out: ${cutStores_phone} (phone), ${cutStores_mali} (mali)")
