# rd_shadow_high_ab: Options > Effects > Shadow detail (port/render/rd_shadowres.c),
# Original against High, on the dumps of -DLIST (tools/shadow_high_dumps.txt: a
# listed dump that is absent FAILS; its second field the --nop spec of its three
# shadow composites, its third "all" for the dump every setting runs on).  Each
# run is rd_replay_tool's --pass-dump with --stats at a setting:
#   adreno   the issue 123 tablet's path (ICO_VK_FAKE_PASS_ROW=qualcomm
#            ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1), Custom 3x, Full height,
#            Full pixel, presented 3048x2032;
#   e1x      the same path, Enhanced at 1x with Full pixel, presented 1280x720;
#   o1x      the same path, the Original preset, presented 1280x720;
#   phone    the PowerVR phone (ICO_VK_FAKE_LIMITS=moltenvk): Resolution Auto,
#            2364x1080 with Full height and Full pixel (2.41x: the levels at 3x);
#   msaa     Enhanced 2x under MSAA 4x (the count in the MSAA epoch, the seven
#            draws: no full-size pass of its own);
#   bspline  Enhanced 2x with [dev] shadow_bspline (ICO_RD_SHADOW_BSPLINE=1);
#   gl1x, gl2x  -DBACKEND=opengl: Enhanced 1x and 2x on OpenGL (llvmpipe).
# The checks, every expectation from the code (unverified until the first run):
#   1. Original is v0.11.6's picture: at adreno, e1x and o1x every PNG the
#      golden list (-DGOLDENS, tools/shadow_high_goldens.txt: v0.11.6's
#      rd_replay_tool at the same settings on this machine's lavapipe) names
#      for the dump has its SHA-256;
#   2. at 1x (e1x, o1x, gl1x) High is Original: every PNG the same bytes, the
#      same draws and passes, "shadow detail high: 0 shadows at the scene's
#      scale, 0 passes, 0 as Original";
#   3. above 1x (adreno, phone, msaa, gl2x), High against Original: one shadow
#      at the scene's scale and none as Original; its passes 3 levels + 6 tent
#      passes + 1 full-size count where the count is the one draw ("shadow
#      count: N in one draw" with N > 0), so 10 or 9; the passes exactly
#      Original's plus those (no SCENE pass more), the draws Original's plus 9
#      (the full-size count's draw is not counted, as the one draw's is not);
#      the present (or SCENE) not the same bytes; and with lists 4-12 left out
#      (SCENE right after list 3) every SCENE texel High changes lies within D
#      of a texel Original's composites darken (shadow_high_check.py, D from
#      the levels' reach and the scale the log's size line names);
#   4. bspline: High with [dev] shadow_bspline is High without it (the
#      B-spline does nothing to a fine level), every PNG.
# No device: SKIP (77).  -DTOOL -DLIST -DGOLDENS -DPY -DCHECK -DOUT -DBACKEND
if(NOT IS_DIRECTORY "/primary/dev/ico-dumps")
  message(STATUS "SKIP: no dump folder on this machine (/primary/dev/ico-dumps): the list's dumps are the build machine's")
  return()
endif()
set(failures 0)
if(NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_shadow_high_ab: no dump list ${LIST}")
endif()
get_filename_component(listDir "${LIST}" DIRECTORY)
file(STRINGS "${LIST}" lines)
set(dumps "")
foreach(l IN LISTS lines)
  string(STRIP "${l}" l)
  if(l STREQUAL "" OR l MATCHES "^#")
    continue()
  endif()
  string(REGEX REPLACE "[ \t]+" ";" fields "${l}")
  list(LENGTH fields nf)
  list(GET fields 0 path)
  if(NOT IS_ABSOLUTE "${path}")
    set(path "${listDir}/${path}")
  endif()
  if(nf LESS 2)
    message(SEND_ERROR "${path}: no --nop spec of its composites in the list")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  if(NOT EXISTS "${path}")
    message(SEND_ERROR "${path}: no such dump (a listed dump must exist)")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  list(APPEND dumps "${path}")
  list(GET fields 1 comp_${path})
  set(all_${path} FALSE)
  if(nf GREATER 2)
    list(GET fields 2 f2)
    if(f2 STREQUAL "all")
      set(all_${path} TRUE)
    endif()
  endif()
endforeach()
list(LENGTH dumps ndumps)

# the goldens: "<dump file name> <setting> <png> <sha256>"
set(goldenKeys "")
if(EXISTS "${GOLDENS}")
  file(STRINGS "${GOLDENS}" glines)
  foreach(l IN LISTS glines)
    string(STRIP "${l}" l)
    if(l STREQUAL "" OR l MATCHES "^#")
      continue()
    endif()
    string(REGEX REPLACE "[ \t]+" ";" g "${l}")
    list(LENGTH g ng)
    if(NOT ng EQUAL 4)
      message(SEND_ERROR "${GOLDENS}: a line of ${ng} fields: ${l}")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    list(GET g 0 gd)
    list(GET g 1 gs)
    list(GET g 2 gp)
    list(GET g 3 gh)
    list(APPEND goldenKeys "${gd}__${gs}")
    list(APPEND gold_${gd}__${gs} "${gp}=${gh}")
  endforeach()
elseif(NOT "${BACKEND}" STREQUAL "opengl")
  message(SEND_ERROR "rd_shadow_high_ab: no golden list ${GOLDENS}")
  math(EXPR failures "${failures} + 1")
endif()

set(tablet ICO_VK_FAKE_PASS_ROW=qualcomm ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1)
set(env_adreno ${tablet})
set(args_adreno --enhanced --resolution 3x --full-height --full-pixel --present 3048x2032)
set(env_e1x ${tablet})
set(args_e1x --enhanced --resolution 1x --full-pixel --present 1280x720)
set(env_o1x ${tablet})
set(args_o1x --present 1280x720)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(env_msaa "")
set(args_msaa --enhanced --resolution 2x --aa msaa4 --present 1280x720)
set(env_bspline ICO_RD_SHADOW_BSPLINE=1)
set(args_bspline --enhanced --resolution 2x --present 1280x720)
set(env_gl1x "")
set(args_gl1x --enhanced --resolution 1x --present 1280x720)
set(env_gl2x "")
set(args_gl2x --enhanced --resolution 2x --present 1280x720)
if("${BACKEND}" STREQUAL "opengl")
  set(same gl1x)
  set(high gl2x)
  set(highAll "")
else()
  set(same e1x o1x)
  set(high adreno)
  set(highAll phone msaa)
endif()

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(highRe "stats: shadow detail (high|original): ([0-9]+) shadows at the scene's scale, ([0-9]+) passes, ([0-9]+) as Original")
set(oneRe "stats: shadow count: ([0-9]+) in one draw")
set(sizeRe "from the count's ([0-9.]+)x([0-9.]+) texels a GS pixel")
set(nopRest "4:0-1000000,5:0-1000000,6:0-1000000,7:0-1000000,8:0-1000000,9:0-1000000,10:0-1000000,11:0-1000000,12:0-1000000")

# one run: -> ok_<name> draws_<name> passes_<name> word_<name> chains_<name>
# hpasses_<name> orig_<name> one_<name> err_<name> (the parent's scope)
function(sh_run name dump dir detail envs)
  set(args ${ARGN})
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envs}
                          "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats
                          --shadow-detail ${detail} ${args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(err_${name} "${err}" PARENT_SCOPE)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    set(ok_${name} SKIP PARENT_SCOPE)
    return()
  endif()
  string(REPLACE ";" "," flat "${out}")
  if(NOT rc EQUAL 0 OR NOT flat MATCHES "${drawsRe}" OR NOT flat MATCHES "${highRe}" OR
     NOT flat MATCHES "${oneRe}")
    message(SEND_ERROR "${dump} ${name}: exit ${rc}, or no draws, shadow detail or shadow "
                       "count line:\n${out}\n${err}")
    set(ok_${name} FALSE PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCH "${drawsRe}" m "${flat}")
  set(draws_${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(passes_${name} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  string(REGEX MATCH "${highRe}" m "${flat}")
  set(word_${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(chains_${name} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  set(hpasses_${name} "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(orig_${name} "${CMAKE_MATCH_4}" PARENT_SCOPE)
  string(REGEX MATCH "${oneRe}" m "${flat}")
  set(one_${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(ok_${name} TRUE PARENT_SCOPE)
endfunction()

# every PNG of dir a the same bytes in dir b, the same set
function(sh_same what a b)
  file(GLOB pa RELATIVE "${a}" "${a}/*.png")
  file(GLOB pb RELATIVE "${b}" "${b}/*.png")
  list(LENGTH pa na)
  list(LENGTH pb nb)
  if(na EQUAL 0 OR NOT na EQUAL nb)
    message(SEND_ERROR "${what}: ${na} targets written against ${nb}")
    set(shFail TRUE PARENT_SCOPE)
    return()
  endif()
  foreach(png IN LISTS pa)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${a}/${png}" "${b}/${png}"
                    RESULT_VARIABLE differ)
    if(NOT differ EQUAL 0)
      message(SEND_ERROR "${what}: ${png} differs")
      set(shFail TRUE PARENT_SCOPE)
    endif()
  endforeach()
endfunction()

# the golden PNGs of (dump, setting) in dir
function(sh_golden what dumpName s dir)
  list(FIND goldenKeys "${dumpName}__${s}" at)
  if(at LESS 0)
    return()
  endif()
  set(n 0)
  foreach(e IN LISTS gold_${dumpName}__${s})
    string(REGEX MATCH "^([^=]+)=(.*)$" m "${e}")
    set(png "${CMAKE_MATCH_1}")
    set(want "${CMAKE_MATCH_2}")
    if(NOT EXISTS "${dir}/${png}")
      message(SEND_ERROR "${what}: ${png} not written (v0.11.6 wrote it)")
      set(shFail TRUE PARENT_SCOPE)
      continue()
    endif()
    file(SHA256 "${dir}/${png}" got)
    if(NOT got STREQUAL want)
      message(SEND_ERROR "${what}: ${png} is not v0.11.6's (${got}, ${want})")
      set(shFail TRUE PARENT_SCOPE)
    endif()
    math(EXPR n "${n} + 1")
  endforeach()
  message(STATUS "${what}: ${n} PNGs against v0.11.6's")
  set(goldenRan TRUE PARENT_SCOPE)
endfunction()

set(compared 0)
set(acted 0)
set(goldenRan FALSE)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  get_filename_component(dumpName "${dump}" NAME)
  set(settings ${same} ${high})
  if(all_${dump})
    list(APPEND settings ${highAll})
  endif()
  foreach(s IN LISTS settings)
    set(shFail FALSE)
    set(base "${OUT}/${index}-${s}")
    sh_run(o "${dump}" "${base}-original" original "${env_${s}}" ${args_${s}})
    if(ok_o STREQUAL "SKIP")
      return()
    endif()
    sh_run(h "${dump}" "${base}-high" high "${env_${s}}" ${args_${s}})
    if(ok_h STREQUAL "SKIP")
      return()
    endif()
    if(NOT ok_o OR NOT ok_h)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    message(STATUS "${dumpName} ${s}: Original ${passes_o} passes ${draws_o} draws; High "
                   "${passes_h} passes ${draws_h} draws, ${chains_h} shadows at the scene's "
                   "scale (${hpasses_h} passes), ${orig_h} as Original; one draw ${one_h}")
    if(NOT word_o STREQUAL "original" OR NOT chains_o EQUAL 0 OR NOT hpasses_o EQUAL 0 OR
       NOT word_h STREQUAL "high")
      message(SEND_ERROR "${dumpName} ${s}: the words: Original says ${word_o} with ${chains_o} "
                         "shadows and ${hpasses_o} passes, High says ${word_h}")
      set(shFail TRUE)
    endif()
    sh_golden("${dumpName} ${s} Original" "${dumpName}" "${s}" "${base}-original")
    list(FIND same "${s}" isSame)
    if(isSame GREATER -1)
      # 2. at 1x High is Original
      math(EXPR compared "${compared} + 1")
      sh_same("${dumpName} ${s} High against Original" "${base}-original" "${base}-high")
      if(NOT chains_h EQUAL 0 OR NOT hpasses_h EQUAL 0 OR NOT orig_h EQUAL 0 OR
         NOT passes_h EQUAL passes_o OR NOT draws_h EQUAL draws_o)
        message(SEND_ERROR "${dumpName} ${s}: at 1x High drew ${chains_h} shadows, "
                           "${hpasses_h} passes, ${orig_h} as Original; ${passes_h}/${draws_h} "
                           "passes and draws against ${passes_o}/${draws_o}")
        set(shFail TRUE)
      endif()
    else()
      # 3. above 1x
      math(EXPR compared "${compared} + 1")
      set(full 0)
      if(one_h GREATER 0)
        set(full 1)
      endif()
      math(EXPR wantPasses "9 + ${full}")
      math(EXPR wantAll "${passes_o} + ${hpasses_h}")
      math(EXPR wantDraws "${draws_o} + 9 * ${chains_h}")
      if(NOT chains_h EQUAL 1 OR NOT orig_h EQUAL 0 OR NOT hpasses_h EQUAL wantPasses OR
         NOT passes_h EQUAL wantAll OR NOT draws_h EQUAL wantDraws)
        message(SEND_ERROR "${dumpName} ${s}: High drew ${chains_h} shadows (1), ${orig_h} as "
                           "Original (0), ${hpasses_h} passes of its own (${wantPasses}); "
                           "${passes_h} passes (${wantAll}), ${draws_h} draws (${wantDraws})")
        set(shFail TRUE)
      endif()
      set(shown PRESENT.png)
      if(NOT EXISTS "${base}-original/PRESENT.png")
        set(shown SCENE.png)
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${base}-original/${shown}"
                              "${base}-high/${shown}" RESULT_VARIABLE differ)
      if(differ EQUAL 0)
        message(SEND_ERROR "${dumpName} ${s}: High's ${shown} is Original's")
        set(shFail TRUE)
      else()
        math(EXPR acted "${acted} + 1")
      endif()
      # the scale the size line names (the larger axis)
      string(REGEX MATCH "${sizeRe}" m "${err_h}")
      set(scale "${CMAKE_MATCH_1}")
      if(CMAKE_MATCH_2 GREATER CMAKE_MATCH_1)
        set(scale "${CMAKE_MATCH_2}")
      endif()
      if(scale STREQUAL "")
        message(SEND_ERROR "${dumpName} ${s}: no size line in High's log:\n${err_h}")
        set(shFail TRUE)
      else()
        # SCENE right after list 3: Original, Original without its composites, High
        set(nopA "${nopRest}")
        set(nopB "${nopRest},${comp_${dump}}")
        sh_run(oa "${dump}" "${base}-original-a" original "${env_${s}}" ${args_${s}} --nop
               "${nopA}")
        sh_run(ob "${dump}" "${base}-original-b" original "${env_${s}}" ${args_${s}} --nop
               "${nopB}")
        sh_run(ha "${dump}" "${base}-high-a" high "${env_${s}}" ${args_${s}} --nop "${nopA}")
        if(NOT ok_oa OR NOT ok_ob OR NOT ok_ha)
          set(shFail TRUE)
        else()
          execute_process(COMMAND "${PY}" -I "${CHECK}" "${base}-original-a/SCENE.png"
                                  "${base}-original-b/SCENE.png" "${base}-high-a/SCENE.png"
                                  --scale "${scale}"
                          RESULT_VARIABLE crc OUTPUT_VARIABLE cout ERROR_VARIABLE cerr)
          message(STATUS "${dumpName} ${s}: ${cout}${cerr}")
          if(NOT crc EQUAL 0)
            message(SEND_ERROR "${dumpName} ${s}: High's change is not confined to the shadow "
                               "(exit ${crc}): ${cout}${cerr}")
            set(shFail TRUE)
          endif()
        endif()
      endif()
    endif()
    if(s STREQUAL "msaa" AND all_${dump})
      # 4. the B-spline does nothing to a fine level (a run of its own)
      sh_run(hb "${dump}" "${base}-high-bspline" high "${env_bspline}" ${args_bspline})
      sh_run(hn "${dump}" "${base}-high-nobspline" high "" ${args_bspline})
      if(ok_hb AND ok_hn)
        sh_same("${dumpName} bspline: High with the B-spline against High without"
                "${base}-high-nobspline" "${base}-high-bspline")
      else()
        set(shFail TRUE)
      endif()
    endif()
    if(shFail)
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()

if(failures GREATER 0)
  message(FATAL_ERROR "rd_shadow_high_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0 OR acted EQUAL 0)
  message(FATAL_ERROR "rd_shadow_high_ab: ${compared} pairs compared, High acted in ${acted}")
endif()
if(NOT "${BACKEND}" STREQUAL "opengl" AND NOT goldenRan)
  message(FATAL_ERROR "rd_shadow_high_ab: no dump had v0.11.6's goldens")
endif()
message(STATUS "rd_shadow_high_ab: ${compared} pairs over ${ndumps} dumps, High acted in ${acted}")
