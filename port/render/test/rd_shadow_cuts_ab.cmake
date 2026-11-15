# rd_shadow_cuts_ab: the shadow window's three cuts of v0.11.6, each off
# against on, byte for byte (-DITEM):
#   dateone     one DATE set-up for the shadow composites (rd_date_one.c;
#               ICO_RD_DATE_ONE=0 against 1): rd_date_one_ab;
#   emptyskip   a composite whose level's box is empty skipped with its set-up
#               (rd_shadow_box.c; ICO_RD_EMPTY_SKIP=0 against 1): rd_empty_skip_ab;
#   novolbreak  a shadow window with no volume taken with SCENE's pass ended at
#               the resolve (rd_no_volume.c's break mode; ICO_RD_NO_VOLUME_BREAK=0
#               against 1): rd_no_volume_break_ab;
# the other two cuts off in both runs, and the no-volume window's open mode
# (ICO_RD_NO_VOLUME=0), so each pair moves by its own cut alone.  For every
# fixture the list names (-DLIST: port/render/test/fixtures/lists/
# shadow_cuts.txt, fixture names of the repository, docs/port/
# TESTING_FIXTURES.md; a listed fixture that is absent or does not expand to
# its pinned SHA-256 FAILS; the word after a name is "volume" or "novolume",
# and a third word the passes fewer at the Mali setting with the empty
# composites on), expanded into OUT, then rd_replay_tool's --pass-dump with
# --stats under the store plan (ICO_RD_STORE_PLAN=1) and the programs' own
# entries (ICO_RD_VU_PREPASS=0), at the settings each item names:
#   mali       the Mali-G52's: ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1
#              ICO_VK_FAKE_NO_DUAL=1, 1x, presented 1612x720, DATE by the R8
#              snapshot (ICO_RD_DATE_PATH=snapshot), the fixed-function blend
#              (ICO_RD_BLEND=fixed) and the store plan's poison
#              (ICO_RD_STORE_POISON=1); an integer scale: the DATE cut on;
#   adreno     the Adreno 740's: ICO_VK_FAKE_ADRENO=1 ICO_VK_FAKE_NO_DST=1,
#              Custom 3x, Full height, Full pixel, presented 3048x2032, the
#              snapshot, the fixed blend, the poison (integer: the cut on);
#   phonesnap  the PowerVR phone's Resolution Auto (2364x1080, Full height, Full
#              pixel) without its in-pass read (ICO_VK_FAKE_NO_DST=1), the
#              snapshot, the fixed blend, the poison: a fractional scale, the
#              cut keeps every depth;
#   phone      the PowerVR phone on lavapipe (ICO_VK_FAKE_LIMITS=moltenvk, the
#              late passes ICO_RD_LATE_PASS=1): DATE through the stencil bit;
#   dstencil   lavapipe at 1x, presented 1280x720, DATE through the stencil bit
#              (ICO_RD_DATE_PATH=stencil);
#   phonepoison  phone with ICO_RD_NO_VOLUME_POISON=1 (the zero clears the
#              break mode records write magenta, none left out for a target
#              holding zero): every PNG but SHADOW0-2 the same bytes;
#   phonerepeat  phone with --repeat 2: the second replay finds the count and
#              the levels holding the first's zeros and records no clear;
#   gl         -DBACKEND=opengl (the _gl twins): 1x on OpenGL, presented
#              1280x720 (DATE by the snapshot).
# dateone: mali, adreno, phonesnap, phone (gl); emptyskip: mali, phone (gl);
# novolbreak: phone, dstencil, phonepoison, phonerepeat, mali (gl).  A fixture a
# setting cannot show ("cannot show") is skipped there.  Each pair, every PNG
# the off run writes the on run's byte for byte (SCENE, DISPLAY, PRESENT,
# SHADOW0-2, every other target), and, from the code:
#   dateone     off shares nothing; on keeps none to its own (no stage data
#               reaches the bound), begins no pass again for the depth; at the
#               snapshot settings each composite that shared takes no R8
#               snapshot (one draw and one pass fewer) and ends no SCENE pass
#               (one pass fewer): snapshots, draws by S, passes by 2 S, the
#               colour loaded and stored each by S x 5 bytes a texel of the
#               shared targets (SCENE's 4 and the R8 snapshot's 1, the
#               snapshot as large as SCENE: rd_core.c); the depth and stencil
#               moved the same at the integer settings (the cut's two passes
#               had no depth; the composites now draw in list 4's pass, which
#               has it) and fewer by 4 to 5 bytes a texel of each shared
#               composite's pass at phonesnap (those passes had the depth); the
#               DATE cut's passes no more than off's; at phone S set-ups fewer,
#               2 S draws fewer (a set-up is two draws), the same passes and
#               traffic; "volume" and "novolume" fixtures share 2 at every
#               setting; --keys at mali: on asks for no pipeline the start-up
#               set lacks that off does not;
#   emptyskip   off skips nothing; on skips K composites, D of them with DATE:
#               at mali D snapshots and D draws fewer (their R8 passes), the
#               passes D to 2 D fewer (the R8 passes, and the composites' SCENE
#               passes of no draw where the next draw begins its own: exactly
#               the list's third field where it has one); at phone D set-ups
#               fewer, 2 D draws fewer, the passes the same or one fewer
#               (a "novolume" fixture: the same, and the same traffic); "novolume"
#               fixtures skip 3 (3 with DATE), "volume" fixtures none;
#   novolbreak  off takes no window; on takes the listed windows at the stencil
#               settings ("novolume" 1, "volume" 0) and none at mali or gl (the
#               composites' DATE by the snapshot): a taken window draws no count
#               pass and no level passes (4 passes fewer) and records its zero
#               clears (C, its clause), so the passes are off's less 4 W plus
#               C, less the passes of the levels' chain at the scene's scale
#               that off draws above 1x and on does not (rd_shadowres.c; the
#               stats' "shadow levels: ... P passes", off's P less on's), and
#               at dstencil less W more: off draws the window's
#               composites (the empty composites pinned off) in list 3's SCENE
#               pass, the break mode skips them, and the fog after them reads
#               the depth outside the pass there (not the local read:
#               rd_replay.c's fog ends the pass and begins its own), so that
#               pass is begun by nothing; at the phone settings the fog's local
#               read draws in list 3's pass, begun by it instead (one pass
#               either way); the set-ups 3 fewer a window ("novolume"); at
#               phonerepeat no clear (C 0) and the targets found holding zero
#               counted; a setting that takes none: the same passes and
#               traffic.
# The path ran (rd_fixture_require, the fixtures' declared properties): on the
# on run, dateone shows date_one_shared, emptyskip empty_composites_skipped,
# novolbreak no_volume_break_taken at the stencil settings (a snapshot setting
# takes none), each with shadow_volume_draws; on the off run a fixture that
# declares it shows date_snapshots at the snapshot settings and
# date_stencil_draws at the stencil ones (the composites' DATE was there to
# cut).  A fixture whose properties no setting checked FAILS.  Over the
# fixtures the item does something at least once.  No device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DFIXTURE_TOOL=<rd_fixture_tool> -DFIXTURES=<the
# fixtures folder> -DFIXTURE_HELPERS=<rd_fixture_helpers.cmake> -DLIST=<a
# fixture list> -DOUT=<a folder the test may write> -DBACKEND=vulkan|opengl
# -DITEM=dateone|emptyskip|novolbreak
include("${FIXTURE_HELPERS}")
set(failures 0)
rd_fixture_list("${LIST}" names)
set(dumps "")
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" d)
  list(APPEND dumps "${d}")
  set(name_${d} "${name}")
  string(REGEX REPLACE "[ \t]+" ";" fields "${RDFX_FIELDS_${name}}")
  list(LENGTH fields nf)
  set(class "")
  set(third "")
  if(nf GREATER 0)
    list(GET fields 0 class)
  endif()
  if(nf GREATER 1)
    list(GET fields 1 third)
  endif()
  if(NOT class STREQUAL "volume" AND NOT class STREQUAL "novolume")
    message(SEND_ERROR "${name}: the list's word after it is '${class}': volume or novolume")
    math(EXPR failures "${failures} + 1")
  endif()
  set(class_${d} "${class}")
  set(third_${d} "${third}")
endforeach()
list(LENGTH dumps ndumps)

set(common ICO_RD_STORE_PLAN=1 ICO_RD_VU_PREPASS=0 ICO_RD_NO_VOLUME=0)
set(snapEnv ICO_RD_BLEND=fixed ICO_RD_STORE_POISON=1 ICO_RD_DATE_PATH=snapshot)
set(phoneArgs --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_mali ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DUAL=1 ${snapEnv})
set(args_mali --present 1612x720)
set(env_adreno ICO_VK_FAKE_ADRENO=1 ICO_VK_FAKE_NO_DST=1 ${snapEnv})
set(args_adreno --enhanced --resolution 3x --full-height --full-pixel --present 3048x2032)
set(env_phonesnap ICO_VK_FAKE_NO_DST=1 ${snapEnv})
set(args_phonesnap ${phoneArgs})
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1)
set(args_phone ${phoneArgs})
set(env_dstencil ICO_RD_DATE_PATH=stencil)
set(args_dstencil --present 1280x720)
set(env_phonepoison ${env_phone} ICO_RD_NO_VOLUME_POISON=1)
set(args_phonepoison ${phoneArgs})
set(env_phonerepeat ${env_phone})
set(args_phonerepeat ${phoneArgs} --repeat 2)
set(env_gl "")
set(args_gl --present 1280x720)
# the settings DATE takes the stencil bit at
set(stencil_phone TRUE)
set(stencil_dstencil TRUE)
set(stencil_phonepoison TRUE)
set(stencil_phonerepeat TRUE)
# the integer scales (the DATE cut on)
set(integer_mali TRUE)
set(integer_adreno TRUE)
set(integer_gl TRUE)
# the settings whose fog reads the depth outside the pass (list 3's SCENE pass
# of the composites is the window's own there)
set(fogown_dstencil TRUE)

if(ITEM STREQUAL "dateone")
  set(var ICO_RD_DATE_ONE)
  set(pinned ICO_RD_EMPTY_SKIP=0 ICO_RD_NO_VOLUME_BREAK=0)
  set(vk mali adreno phonesnap phone)
elseif(ITEM STREQUAL "emptyskip")
  set(var ICO_RD_EMPTY_SKIP)
  set(pinned ICO_RD_DATE_ONE=0 ICO_RD_NO_VOLUME_BREAK=0)
  set(vk mali phone)
elseif(ITEM STREQUAL "novolbreak")
  set(var ICO_RD_NO_VOLUME_BREAK)
  set(pinned ICO_RD_DATE_ONE=0 ICO_RD_EMPTY_SKIP=0)
  set(vk phone dstencil phonepoison phonerepeat mali)
else()
  message(FATAL_ERROR "rd_shadow_cuts_ab: -DITEM=dateone|emptyskip|novolbreak, not ${ITEM}")
endif()
if("${BACKEND}" STREQUAL "opengl")
  set(settings gl)
else()
  set(settings ${vk})
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(dateRe "stats: DATE snapshots ([0-9]+), DATE stencil draws ([0-9]+) \\(([0-9]+) setups\\)")
set(cutRe "stats: DATE cut (on|off): ([0-9]+) passes begun without the depth \\([0-9]+ draws, [0-9]+ runs, [0-9.]+ MB of depth and stencil each way at most\\), ([0-9]+) begun again with it")
set(trafficRe "stats: traffic: colour [0-9.]+ MB loaded [0-9.]+ MB stored, depth and stencil [0-9.]+/[0-9.]+ MB loaded/stored, [0-9.]+ MB cleared")
set(colourRe "stats: traffic: colour ([0-9]+)\\.([0-9][0-9]) MB loaded ([0-9]+)\\.([0-9][0-9]) MB stored")
set(depthRe "MB stored, depth and stencil ([0-9]+)\\.([0-9][0-9])/([0-9]+)\\.([0-9][0-9]) MB loaded/stored")
set(breakRe "break (on|off): ([0-9]+) taken \\(([0-9]+) zero clears, ([0-9]+) targets holding zero\\)")
set(emptyRe "empty composites (on|off): ([0-9]+) skipped \\(([0-9]+) with DATE\\)")
set(oneRe "DATE one set-up (on|off|force): ([0-9]+) shared \\(([0-9]+) texels\\), ([0-9]+) kept their own")
# (optional: the levels' chain at the scene's scale, rd_shadowres.c)
set(levelsRe "shadow levels: [0-9]+ shadows at the scene's scale, ([0-9]+) passes")
set(keysRe "keys: [0-9]+ asked in [0-9]+ rounds, [0-9]+ in the set of [0-9]+, ([0-9]+) missing")

# hundredths of a MB from the integer and fraction a figure printed
function(sc_hundredths out i f)
  if(f MATCHES "^0([0-9])$")
    set(f "${CMAKE_MATCH_1}")
  endif()
  math(EXPR v "${i} * 100 + ${f}")
  set(${out} "${v}" PARENT_SCOPE)
endfunction()

# off - on (hundredths) within the rounding of lo..hi bytes (two printed
# figures, each within half a hundredth)
function(sc_fall what off on lo hi)
  math(EXPR got "${off} - ${on}")
  math(EXPR a "${lo} * 100 / 1048576 - 1")
  math(EXPR b "${hi} * 100 / 1048576 + 2")
  if(got LESS a OR got GREATER b)
    message(SEND_ERROR "${what}: off less on ${got} hundredths of a MB, ${lo}..${hi} bytes "
                       "(${a}..${b}) expected")
    set(scFail TRUE PARENT_SCOPE)
  endif()
endfunction()

set(compared 0)
set(acted 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(class "${class_${dump}}")
  foreach(s IN LISTS settings)
    set(skipped FALSE)
    foreach(v 0 1)
      if(skipped)
        break()
      endif()
      set(dir "${OUT}/${index}-${s}-${v}")
      file(REMOVE_RECURSE "${dir}")
      file(MAKE_DIRECTORY "${dir}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${pinned} ${env_${s}} ${var}=${v}
                              "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args_${s}}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      if(NOT rc EQUAL 0 AND v EQUAL 0 AND "${err}" MATCHES "cannot show")
        message(STATUS "${dump} ${s}: skipped (the setting cannot show it)")
        set(skipped TRUE)
        continue()
      endif()
      if(NOT rc EQUAL 0)
        message(SEND_ERROR "${dump} ${s} ${var}=${v}: exit ${rc}:\n${err}")
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      set(out_${v} "${out}")
      string(REPLACE ";" "," flat "${out}")
      set(ok TRUE)
      foreach(re drawsRe dateRe cutRe trafficRe breakRe emptyRe oneRe)
        if(NOT flat MATCHES "${${re}}")
          message(SEND_ERROR "${dump} ${s} ${var}=${v}: no ${re} line:\n${out}")
          set(ok FALSE)
        endif()
      endforeach()
      if(NOT ok)
        math(EXPR failures "${failures} + 1")
        set(skipped TRUE)
        continue()
      endif()
      string(REGEX MATCH "${drawsRe}" m "${flat}")
      set(draws_${v} "${CMAKE_MATCH_1}")
      set(passes_${v} "${CMAKE_MATCH_2}")
      string(REGEX MATCH "${dateRe}" m "${flat}")
      set(snaps_${v} "${CMAKE_MATCH_1}")
      set(setups_${v} "${CMAKE_MATCH_3}")
      string(REGEX MATCH "${cutRe}" m "${flat}")
      set(cut_${v} "${CMAKE_MATCH_2}")
      set(again_${v} "${CMAKE_MATCH_3}")
      string(REGEX MATCH "${trafficRe}" m "${flat}")
      set(traffic_${v} "${m}")
      string(REGEX MATCH "${colourRe}" m "${flat}")
      sc_hundredths(cl_${v} "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
      sc_hundredths(cs_${v} "${CMAKE_MATCH_3}" "${CMAKE_MATCH_4}")
      string(REGEX MATCH "${depthRe}" m "${flat}")
      set(d1 "${CMAKE_MATCH_1}")
      set(d2 "${CMAKE_MATCH_2}")
      set(d3 "${CMAKE_MATCH_3}")
      set(d4 "${CMAKE_MATCH_4}")
      sc_hundredths(dl_${v} "${d1}" "${d2}")
      sc_hundredths(ds_${v} "${d3}" "${d4}")
      string(REGEX MATCH "${breakRe}" m "${flat}")
      set(bword_${v} "${CMAKE_MATCH_1}")
      set(taken_${v} "${CMAKE_MATCH_2}")
      set(clears_${v} "${CMAKE_MATCH_3}")
      set(held_${v} "${CMAKE_MATCH_4}")
      string(REGEX MATCH "${emptyRe}" m "${flat}")
      set(eword_${v} "${CMAKE_MATCH_1}")
      set(skip_${v} "${CMAKE_MATCH_2}")
      set(skipDate_${v} "${CMAKE_MATCH_3}")
      set(chain_${v} 0)
      if(flat MATCHES "${levelsRe}")
        set(chain_${v} "${CMAKE_MATCH_1}")
      endif()
      string(REGEX MATCH "${oneRe}" m "${flat}")
      set(oword_${v} "${CMAKE_MATCH_1}")
      set(shared_${v} "${CMAKE_MATCH_2}")
      set(texels_${v} "${CMAKE_MATCH_3}")
      set(kept_${v} "${CMAKE_MATCH_4}")
      message(STATUS "${dump} ${s} ${var}=${v}: ${passes_${v}} passes, ${draws_${v}} draws, "
                     "${snaps_${v}} snapshots, ${setups_${v}} set-ups, cut ${cut_${v}}; "
                     "DATE one ${oword_${v}} ${shared_${v}} shared (${texels_${v}} texels) "
                     "${kept_${v}} kept; empty ${eword_${v}} ${skip_${v}} (${skipDate_${v}}); "
                     "break ${bword_${v}} ${taken_${v}} (${clears_${v}} clears, ${held_${v}} "
                     "held); ${traffic_${v}}")
    endforeach()
    if(skipped)
      continue()
    endif()
    set(scFail FALSE)
    set(stencil FALSE)
    if(stencil_${s})
      set(stencil TRUE)
    endif()
    # ---------------------------------------------------------- the items
    if(ITEM STREQUAL "dateone")
      if(NOT oword_0 STREQUAL "off" OR NOT shared_0 EQUAL 0 OR NOT oword_1 STREQUAL "on" OR
         NOT kept_1 EQUAL 0 OR NOT again_1 EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: DATE one set-up ${oword_0} sharing ${shared_0} off, "
                           "${oword_1} sharing ${shared_1} with ${kept_1} kept to their own and "
                           "${again_1} passes begun again on")
        set(scFail TRUE)
      endif()
      set(S "${shared_1}")
      if(S GREATER 0)
        math(EXPR acted "${acted} + 1")
      endif()
      if((class STREQUAL "volume" OR class STREQUAL "novolume") AND NOT S EQUAL 2)
        message(SEND_ERROR "${dump} ${s}: ${S} shared, 2 expected (the three composites)")
        set(scFail TRUE)
      endif()
      if(stencil)
        math(EXPR wantSetups "${setups_0} - ${S}")
        math(EXPR wantDraws "${draws_0} - 2 * ${S}")
        if(NOT setups_1 EQUAL wantSetups OR NOT draws_1 EQUAL wantDraws OR
           NOT passes_1 EQUAL passes_0 OR NOT traffic_1 STREQUAL traffic_0)
          message(SEND_ERROR "${dump} ${s}: ${setups_1} set-ups (${wantSetups}), ${draws_1} draws "
                             "(${wantDraws}), ${passes_1} passes (${passes_0}), traffic\n  "
                             "${traffic_0}\n  ${traffic_1}")
          set(scFail TRUE)
        endif()
      else()
        math(EXPR wantSnaps "${snaps_0} - ${S}")
        math(EXPR wantDraws "${draws_0} - ${S}")
        math(EXPR wantPasses "${passes_0} - 2 * ${S}")
        if(NOT snaps_1 EQUAL wantSnaps OR NOT draws_1 EQUAL wantDraws OR
           NOT passes_1 EQUAL wantPasses OR cut_1 GREATER cut_0)
          message(SEND_ERROR "${dump} ${s}: ${snaps_1} snapshots (${wantSnaps}), ${draws_1} draws "
                             "(${wantDraws}), ${passes_1} passes (${wantPasses}), the cut's "
                             "${cut_0} -> ${cut_1}")
          set(scFail TRUE)
        endif()
        math(EXPR colour "${texels_1} * 5")
        sc_fall("${dump} ${s}: colour loaded" "${cl_0}" "${cl_1}" "${colour}" "${colour}")
        sc_fall("${dump} ${s}: colour stored" "${cs_0}" "${cs_1}" "${colour}" "${colour}")
        if(integer_${s})
          sc_fall("${dump} ${s}: depth and stencil loaded" "${dl_0}" "${dl_1}" 0 0)
          sc_fall("${dump} ${s}: depth and stencil stored" "${ds_0}" "${ds_1}" 0 0)
        else()
          math(EXPR dlo "${texels_1} * 4")
          math(EXPR dhi "${texels_1} * 5")
          sc_fall("${dump} ${s}: depth and stencil loaded" "${dl_0}" "${dl_1}" "${dlo}" "${dhi}")
          sc_fall("${dump} ${s}: depth and stencil stored" "${ds_0}" "${ds_1}" "${dlo}" "${dhi}")
        endif()
      endif()
    elseif(ITEM STREQUAL "emptyskip")
      if(NOT eword_0 STREQUAL "off" OR NOT skip_0 EQUAL 0 OR NOT eword_1 STREQUAL "on")
        message(SEND_ERROR "${dump} ${s}: the empty composites ${eword_0} skipping ${skip_0} off, "
                           "${eword_1} on")
        set(scFail TRUE)
      endif()
      set(K "${skip_1}")
      set(D "${skipDate_1}")
      if(K GREATER 0)
        math(EXPR acted "${acted} + 1")
      endif()
      if(class STREQUAL "novolume" AND (NOT K EQUAL 3 OR NOT D EQUAL 3))
        message(SEND_ERROR "${dump} ${s}: ${K} skipped (${D} with DATE), 3 (3) expected")
        set(scFail TRUE)
      elseif(class STREQUAL "volume" AND NOT K EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${K} skipped in a picture with volumes")
        set(scFail TRUE)
      endif()
      if(stencil)
        math(EXPR wantSetups "${setups_0} - ${D}")
        math(EXPR wantDraws "${draws_0} - 2 * ${D}")
        math(EXPR least "${passes_0} - 1")
        if(NOT setups_1 EQUAL wantSetups OR NOT draws_1 EQUAL wantDraws OR
           passes_1 GREATER passes_0 OR passes_1 LESS least OR
           (class STREQUAL "novolume" AND (NOT passes_1 EQUAL passes_0 OR
                                           NOT traffic_1 STREQUAL traffic_0)))
          message(SEND_ERROR "${dump} ${s}: ${setups_1} set-ups (${wantSetups}), ${draws_1} draws "
                             "(${wantDraws}), ${passes_1} passes (${passes_0} drawn), traffic\n  "
                             "${traffic_0}\n  ${traffic_1}")
          set(scFail TRUE)
        endif()
      else()
        math(EXPR wantSnaps "${snaps_0} - ${D}")
        math(EXPR wantDraws "${draws_0} - ${D}")
        math(EXPR most "${passes_0} - ${D}")
        math(EXPR least "${passes_0} - 2 * ${D}")
        set(exact "${third_${dump}}")
        if(NOT snaps_1 EQUAL wantSnaps OR NOT draws_1 EQUAL wantDraws OR
           passes_1 GREATER most OR passes_1 LESS least)
          message(SEND_ERROR "${dump} ${s}: ${snaps_1} snapshots (${wantSnaps}), ${draws_1} draws "
                             "(${wantDraws}), ${passes_1} passes (${least}..${most})")
          set(scFail TRUE)
        endif()
        if(s STREQUAL "mali" AND NOT exact STREQUAL "")
          math(EXPR wantPasses "${passes_0} - ${exact}")
          if(NOT passes_1 EQUAL wantPasses)
            message(SEND_ERROR "${dump} ${s}: ${passes_1} passes, ${wantPasses} expected (the "
                               "list's ${exact} fewer)")
            set(scFail TRUE)
          endif()
        endif()
      endif()
    else() # novolbreak
      if(NOT bword_0 STREQUAL "off" OR NOT taken_0 EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: the break mode ${bword_0} taking ${taken_0} off")
        set(scFail TRUE)
      endif()
      set(W "${taken_1}")
      set(want "")
      if(class STREQUAL "novolume")
        set(want 1)
      elseif(class STREQUAL "volume")
        set(want 0)
      endif()
      if(NOT stencil)
        set(want 0) # the composites' DATE by the snapshot: not taken
      endif()
      if(NOT want STREQUAL "" AND NOT W EQUAL want)
        message(SEND_ERROR "${dump} ${s}: ${W} windows taken, ${want} expected")
        set(scFail TRUE)
      endif()
      if(W GREATER 0)
        math(EXPR acted "${acted} + 1")
        # above 1x the levels are drawn at the scene's scale by their own
        # chain (rd_shadowres.c; the stats' "shadow levels: N shadows at the
        # scene's scale, P passes"): the off run draws the empty count's chain
        # (P passes more than the PS2-size levels alone), a taken window none
        math(EXPR wantPasses
             "${passes_0} - 4 * ${W} + ${clears_1} - (${chain_0} - ${chain_1})")
        if(fogown_${s})
          math(EXPR wantPasses "${wantPasses} - ${W}") # list 3's composites' pass
        endif()
        if(NOT passes_1 EQUAL wantPasses OR setups_1 GREATER setups_0)
          message(SEND_ERROR "${dump} ${s}: ${passes_1} passes, ${wantPasses} expected (${W} "
                             "windows, ${clears_1} zero clears); set-ups ${setups_0} -> "
                             "${setups_1}")
          set(scFail TRUE)
        endif()
        if(class STREQUAL "novolume")
          math(EXPR wantSetups "${setups_0} - 3 * ${W}")
          if(NOT setups_1 EQUAL wantSetups)
            message(SEND_ERROR "${dump} ${s}: ${setups_1} set-ups, ${wantSetups} expected")
            set(scFail TRUE)
          endif()
        endif()
        if(s STREQUAL "phonerepeat" AND (NOT clears_1 EQUAL 0 OR held_1 EQUAL 0))
          message(SEND_ERROR "${dump} ${s}: the second replay recorded ${clears_1} zero clears "
                             "and found ${held_1} targets holding zero: 0 and some expected")
          set(scFail TRUE)
        endif()
        if(NOT s STREQUAL "phonerepeat" AND NOT held_1 EQUAL 0)
          message(SEND_ERROR "${dump} ${s}: ${held_1} targets holding zero in a first replay")
          set(scFail TRUE)
        endif()
      elseif(NOT passes_1 EQUAL passes_0 OR NOT traffic_1 STREQUAL traffic_0)
        message(SEND_ERROR "${dump} ${s}: nothing taken, but ${passes_1} passes against "
                           "${passes_0}, or the traffic moved:\n  ${traffic_0}\n  ${traffic_1}")
        set(scFail TRUE)
      endif()
    endif()
    if(scFail)
      math(EXPR failures "${failures} + 1")
    endif()
    # ------------------------------------------------------ the path ran
    # the cut's own quantity on the on run (the break mode's at the stencil
    # settings alone: a snapshot setting takes no window, above)
    set(nm "${name_${dump}}")
    if(ITEM STREQUAL "dateone")
      rd_fixture_require(${nm} "${out_1}" date_one_shared shadow_volume_draws)
    elseif(ITEM STREQUAL "emptyskip")
      rd_fixture_require(${nm} "${out_1}" empty_composites_skipped shadow_volume_draws)
    elseif(stencil)
      rd_fixture_require(${nm} "${out_1}" no_volume_break_taken shadow_volume_draws)
    else()
      rd_fixture_require(${nm} "${out_1}" shadow_volume_draws)
    endif()
    set(required_${dump} TRUE)
    # the composites' DATE on the off run, where the fixture declares it
    if(stencil)
      set(dq date_stencil_draws)
    else()
      set(dq date_snapshots)
    endif()
    if(";${RDFX_PROPS_${nm}};" MATCHES ";${dq}[<>=!]")
      rd_fixture_require(${nm} "${out_0}" ${dq})
    endif()
    # ------------------------------------------------------- the bytes
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
      if(s STREQUAL "phonepoison" AND png MATCHES "^SHADOW[0-2]\\.png$")
        continue() # the poison's own: the levels hold magenta where the walk owed them zero
      endif()
      math(EXPR compared "${compared} + 1")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dirOff}/${png}"
                              "${dirOn}/${png}" RESULT_VARIABLE same)
      if(NOT same EQUAL 0)
        message(SEND_ERROR "${dump} ${s}: ${png} differs with ${var}=1: it must be the same bytes")
        math(EXPR failures "${failures} + 1")
      endif()
    endforeach()
  endforeach()
  # the start-up set: one DATE set-up asks for no key the set lacks
  if(ITEM STREQUAL "dateone" AND NOT "${BACKEND}" STREQUAL "opengl")
    foreach(v 0 1)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${common} ${pinned} ${env_mali} ${var}=${v}
                              "${TOOL}" "${dump}" - --keys ${args_mali}
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
      if(rc EQUAL 77)
        message(STATUS "SKIP: no device: ${err}")
        return()
      endif()
      string(REPLACE ";" "," flat "${out}")
      if(NOT flat MATCHES "${keysRe}")
        message(SEND_ERROR "${dump} --keys ${var}=${v}: exit ${rc}, no keys line:\n${out}\n${err}")
        math(EXPR failures "${failures} + 1")
        set(missing_${v} 0)
        continue()
      endif()
      set(missing_${v} "${CMAKE_MATCH_1}")
    endforeach()
    message(STATUS "${dump} --keys: ${missing_0} missing off, ${missing_1} on")
    if(missing_1 GREATER missing_0)
      message(SEND_ERROR "${dump}: one DATE set-up asks for ${missing_1} pipelines the start-up "
                         "set lacks, ${missing_0} without it")
      math(EXPR failures "${failures} + 1")
    endif()
  endif()
endforeach()
foreach(dump IN LISTS dumps)
  if(NOT required_${dump})
    message(SEND_ERROR "${name_${dump}}: its properties were never checked (no setting could "
                       "show it): a fixture this test cannot measure is no test")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_shadow_cuts_ab ${ITEM}: ${failures} failures over ${ndumps} fixtures")
endif()
if(compared EQUAL 0)
  message(FATAL_ERROR "rd_shadow_cuts_ab ${ITEM}: no target compared over ${ndumps} fixtures")
endif()
if(ITEM STREQUAL "novolbreak" AND "${BACKEND}" STREQUAL "opengl")
  # OpenGL takes DATE by the snapshot: the break mode takes no window there,
  # and the twin's job is that both arms draw the same bytes
  if(NOT acted EQUAL 0)
    message(FATAL_ERROR "rd_shadow_cuts_ab ${ITEM}: the break mode acted on OpenGL (${acted} pairs)")
  endif()
elseif(acted EQUAL 0)
  message(FATAL_ERROR "rd_shadow_cuts_ab ${ITEM}: the cut did nothing over ${ndumps} fixtures")
endif()
message(STATUS "rd_shadow_cuts_ab ${ITEM}: ${compared} targets the same bytes over ${ndumps} "
               "fixtures; ${acted} pairs where it acted")
