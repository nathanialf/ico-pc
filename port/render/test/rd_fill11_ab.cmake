# rd_fill11_ab: the crack fill and list 11 in one pass on SCENE (port/render/rd_fill11.c;
# docs/port/RENDER_POST.md, "The crack fill in list 11's pass"): ICO_RD_FILL11=0
# against the switch on.  For every line of the list (tools/fill11_dumps.txt: a
# path, '*' globs allowed, then optionally phoneN, deskN, glN (one replay's fills
# drawn in list 11's pass at that setting) and "nop SPEC" (rd_replay_tool's --nop,
# given to both runs); a listed dump that is absent FAILS), rd_replay_tool's
# --pass-dump with --stats and ICO_RD_VU_PREPASS=0, at:
#   phone   the PowerVR phone's Resolution Auto (Enhanced, 2364x1080, Full height,
#           Full pixel, presented at 2364x1080) under the tile class's fakes
#           (ICO_VK_FAKE_LIMITS=moltenvk) and its late pass (ICO_RD_LATE_PASS=1):
#           the profile's class is Apple's there, so the on run sets
#           ICO_RD_FILL11=1 (the PowerVR class's default on a device);
#   desk    Enhanced 2x at 4:3 (the PS2's border: no repeat), lavapipe's own
#           profile (a desktop class); the on run sets ICO_RD_FILL11=1 (off by
#           default in this release, RD_FILL11_DEFAULT);
#   mali    the phone's setting on a Mali-G68's limits and an Arm phone's vendor
#           (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5: a tile-based phone
#           GPU, RhiLimits.tilePhone): the on run sets nothing and must draw no
#           fill in list 11's pass, its self-test not run (the default off; the
#           class off too were the default on);
#   gl      -DBACKEND=opengl (rd_fill11_gl): the desk setting on OpenGL (llvmpipe),
#           the on run with ICO_RD_FILL11=1;
# a dump drawn narrower than a setting's aspect cannot show it and is skipped
# there.  Each pair: every PNG the off run writes (SCENE, DISPLAY, PRESENT and
# every other named target) the on run's byte for byte; the same draws; the off
# run's stats say the switch off with nothing drawn in list 11's pass and nothing
# held; the self-test passed in every run that ran it (the off run's too: it runs
# where ICO_RD_FILL11 is set); the on run's passes and the crack fill's own passes
# each fewer than the off run's by its fills drawn in list 11's pass exactly, its
# colour MB loaded and stored each fewer by the MB its stats name for them
# (within 0.02, two decimals' rounding), its depth and stencil MB loaded and
# stored not more, its copies the same; the marked dumps draw that many fills in
# list 11's pass at that setting.  Over the list: at least one fill drawn in list
# 11's pass (desk, or gl).  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
# -DBACKEND=vulkan|opengl
set(failures 0)
if("${LIST}" STREQUAL "" OR NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_fill11_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
# the entries: entry_<n> its dump, nop_<n> its --nop spec, want_<n>_<setting>
set(nentries 0)
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  set(nop "")
  if(l MATCHES "^(.*[^ \t])[ \t]+nop[ \t]+([0-9:,-]+)$")
    set(l "${CMAKE_MATCH_1}")
    set(nop "${CMAKE_MATCH_2}")
  endif()
  set(wants "")
  while(l MATCHES "^(.*[^ \t])[ \t]+(phone|desk|gl)([0-9]+)$")
    set(l "${CMAKE_MATCH_1}")
    list(APPEND wants "${CMAKE_MATCH_2}=${CMAKE_MATCH_3}")
  endwhile()
  if(NOT IS_ABSOLUTE "${l}")
    set(l "${listDir}/${l}")
  endif()
  set(found "")
  if(l MATCHES "[*]")
    file(GLOB found "${l}")
    list(SORT found)
  elseif(EXISTS "${l}")
    set(found "${l}")
  endif()
  if(NOT found)
    message(SEND_ERROR "${l}: no such dump (a listed dump must exist)")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  foreach(d IN LISTS found)
    math(EXPR nentries "${nentries} + 1")
    set(entry_${nentries} "${d}")
    set(nop_${nentries} "${nop}")
    foreach(w IN LISTS wants)
      string(REGEX REPLACE "=.*" "" ws "${w}")
      string(REGEX REPLACE ".*=" "" wn "${w}")
      set(want_${nentries}_${ws} "${wn}")
    endforeach()
  endforeach()
endforeach()

set(common ICO_RD_VU_PREPASS=0)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1)
set(on_phone ICO_RD_FILL11=1)
set(word_phone on)
set(args_desk --enhanced --resolution 2x --aspect 1.3333 --present 1280x960)
set(env_desk "")
set(on_desk ICO_RD_FILL11=1)
set(word_desk on)
set(args_mali ${args_phone})
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5)
set(on_mali "")
set(word_mali off)
set(args_gl ${args_desk})
set(env_gl "")
set(on_gl ICO_RD_FILL11=1)
set(word_gl on)
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
else()
  set(settings phone desk mali)
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(crackRe "stats: crack fill (on|off): ([0-9]+) passes")
set(f11Re "stats: crack fill with list 11 (on|off) \\(self-test (passed|failed|not run)\\): ([0-9]+) drawn in list 11's pass \\(([0-9.]+) MB of colour not loaded and as many not stored\\), ([0-9]+) drawn in a pass of their own")
set(trafficRe "stats: traffic: colour ([0-9.]+) MB loaded ([0-9.]+) MB stored, depth and stencil ([0-9.]+)/([0-9.]+) MB loaded/stored, [0-9.]+ MB cleared, ([0-9.]+) MB copied")

# v in hundredths of a MB (the stats print two decimals)
function(mb_hundredths out v)
  string(REGEX REPLACE "^([0-9]+)\\.([0-9][0-9])$" "\\1\\2" h "${v}")
  math(EXPR h "${h}")
  set(${out} "${h}" PARENT_SCOPE)
endfunction()

set(compared 0)
set(landedTotal 0)
set(indices "")
if(nentries GREATER 0)
  foreach(i RANGE 1 ${nentries})
    list(APPEND indices ${i})
  endforeach()
endif()
foreach(index IN LISTS indices)
  set(dump "${entry_${index}}")
  set(nopArgs "")
  if(NOT "${nop_${index}}" STREQUAL "")
    set(nopArgs --nop "${nop_${index}}")
  endif()
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(w 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${w}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      if(w EQUAL 0)
        set(sw ICO_RD_FILL11=0)
      else()
        # what a tile-based GPU leaves undefined made garbage on lavapipe
        # (rd_fill11.h ICO_RD_FILL11_POISON): an unwritten texel or a draw
        # that depends on the scratch depth shows
        set(sw ${on_${s}} ICO_RD_FILL11_POISON=1)
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${env_${s}} ${sw}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                              ${nopArgs}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND w EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (drawn narrower than the setting's aspect)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} fill11 ${w}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REPLACE ";" "," flat "${out}")
      set(ok TRUE)
      foreach(re drawsRe crackRe f11Re trafficRe)
        if(NOT flat MATCHES "${${re}}")
          message(SEND_ERROR "${dump} ${s} fill11 ${w}: no ${re} line:\n${out}")
          math(EXPR failures "${failures} + 1")
          set(ok FALSE)
        endif()
      endforeach()
      if(NOT ok)
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${w} "${CMAKE_MATCH_1}")
      set(passes_${w} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${crackRe}" m "${flat}")
      set(fillPasses_${w} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${f11Re}" m "${flat}")
      set(word_${w} "${CMAKE_MATCH_1}")
      set(test_${w} "${CMAKE_MATCH_2}")
      set(landed_${w} "${CMAKE_MATCH_3}")
      set(mb_${w} "${CMAKE_MATCH_4}")
      set(own_${w} "${CMAKE_MATCH_5}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(cl_${w} "${CMAKE_MATCH_1}")
      set(cs_${w} "${CMAKE_MATCH_2}")
      set(dl_${w} "${CMAKE_MATCH_3}")
      set(ds_${w} "${CMAKE_MATCH_4}")
      set(cp_${w} "${CMAKE_MATCH_5}")
      message(STATUS "${dump} ${s} fill11 ${w}: ${passes_${w}} passes, ${draws_${w}} draws, "
                     "${fillPasses_${w}} crack fill passes; with list 11 ${word_${w}} (self-test "
                     "${test_${w}}): ${landed_${w}} in list 11's pass (${mb_${w}} MB), "
                     "${own_${w}} held and drawn apart; colour ${cl_${w}}/${cs_${w}} MB "
                     "loaded/stored, depth and stencil ${dl_${w}}/${ds_${w}} MB, ${cp_${w}} MB "
                     "copied")
    endforeach()
    if(skipped)
      continue()
    endif()
    # the switch's words: off in the off run, the setting's in the on run;
    # nothing drawn in list 11's pass nor held with the switch off
    if(NOT word_0 STREQUAL "off" OR NOT landed_0 EQUAL 0 OR NOT own_0 EQUAL 0 OR
       NOT word_1 STREQUAL "${word_${s}}")
      message(SEND_ERROR "${dump} ${s}: the switch ${word_0} (${landed_0} landed, ${own_0} held) "
                         "off, ${word_1} on (${word_${s}} expected)")
      math(EXPR failures "${failures} + 1")
    endif()
    # the self-test passed wherever it ran (lavapipe and llvmpipe keep UNORM8
    # and the depth twins draw as their plain keys); the mali on run does not
    # run it (the class)
    if(NOT test_0 STREQUAL "passed" OR (NOT s STREQUAL "mali" AND NOT test_1 STREQUAL "passed") OR
       (s STREQUAL "mali" AND NOT test_1 STREQUAL "not run"))
      message(SEND_ERROR "${dump} ${s}: the self-test ${test_0} off, ${test_1} on")
      math(EXPR failures "${failures} + 1")
    endif()
    if(s STREQUAL "mali" AND (NOT landed_1 EQUAL 0 OR NOT own_1 EQUAL 0))
      message(SEND_ERROR "${dump} mali: ${landed_1} fills in list 11's pass, ${own_1} held: the "
                         "Arm class keeps it off")
      math(EXPR failures "${failures} + 1")
    endif()
    # the passes: fewer by the fills drawn in list 11's pass exactly, the
    # crack fill's own passes too; the same draws
    math(EXPR wantPasses "${passes_0} - ${landed_1}")
    math(EXPR wantFills "${fillPasses_0} - ${landed_1}")
    if(NOT draws_1 EQUAL draws_0 OR NOT passes_1 EQUAL wantPasses OR
       NOT fillPasses_1 EQUAL wantFills)
      message(SEND_ERROR "${dump} ${s}: ${draws_1} draws, ${passes_1} passes, ${fillPasses_1} fill "
                         "passes on; ${draws_0}, ${passes_0}, ${fillPasses_0} off; ${landed_1} "
                         "fills in list 11's pass: ${draws_0}, ${wantPasses}, ${wantFills} "
                         "expected")
      math(EXPR failures "${failures} + 1")
    endif()
    # the bytes: colour loaded and stored each fewer by the MB the stats name
    # (list 11's load and the fill's own pass's store), depth and stencil not
    # more (list 11's pass bound SCENE's depth), the copies the same
    mb_hundredths(cl0 "${cl_0}")
    mb_hundredths(cl1 "${cl_1}")
    mb_hundredths(cs0 "${cs_0}")
    mb_hundredths(cs1 "${cs_1}")
    mb_hundredths(dl0 "${dl_0}")
    mb_hundredths(dl1 "${dl_1}")
    mb_hundredths(ds0 "${ds_0}")
    mb_hundredths(ds1 "${ds_1}")
    mb_hundredths(fall "${mb_1}")
    math(EXPR dL "${cl0} - ${cl1} - ${fall}")
    math(EXPR dS "${cs0} - ${cs1} - ${fall}")
    if(dL LESS -2 OR dL GREATER 2 OR dS LESS -2 OR dS GREATER 2 OR dl1 GREATER dl0 OR
       ds1 GREATER ds0 OR NOT cp_1 STREQUAL cp_0)
      message(SEND_ERROR "${dump} ${s}: colour ${cl_0}/${cs_0} -> ${cl_1}/${cs_1} MB loaded/stored, "
                         "depth and stencil ${dl_0}/${ds_0} -> ${dl_1}/${ds_1}, copies ${cp_0} -> "
                         "${cp_1} MB: ${mb_1} MB fewer each way expected, the depth not more, "
                         "the copies the same")
      math(EXPR failures "${failures} + 1")
    endif()
    set(want "${want_${index}_${s}}")
    if(NOT want STREQUAL "" AND NOT landed_1 EQUAL want)
      message(SEND_ERROR "${dump} ${s}: ${landed_1} fills in list 11's pass, ${want} expected")
      math(EXPR failures "${failures} + 1")
    endif()
    if(NOT s STREQUAL "phone" AND NOT s STREQUAL "mali")
      math(EXPR landedTotal "${landedTotal} + ${landed_1}")
    endif()
    set(dirOff "${OUT}/${index}-${s}-0")
    set(dirOn "${OUT}/${index}-${s}-1")
    file(GLOB pngs RELATIVE "${dirOff}" "${dirOff}/*.png")
    file(GLOB pngsOn RELATIVE "${dirOn}" "${dirOn}/*.png")
    list(LENGTH pngs npng)
    list(LENGTH pngsOn npngOn)
    if(npng EQUAL 0 OR NOT npng EQUAL npngOn)
      message(SEND_ERROR "${dump} ${s}: ${npng} targets written off, ${npngOn} on")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(png IN LISTS pngs)
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with the crack fill in list 11's pass: "
                           "it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_fill11_ab: ${failures} failures over ${nentries} entries")
endif()
if(compared EQUAL 0 OR landedTotal EQUAL 0)
  message(FATAL_ERROR "rd_fill11_ab: ${compared} targets compared, ${landedTotal} fills drawn in "
                      "list 11's pass over ${nentries} entries: one at least expected")
endif()
message(STATUS "rd_fill11_ab: ${compared} targets the same bytes over ${nentries} entries; "
               "${landedTotal} fills drawn in list 11's pass")
