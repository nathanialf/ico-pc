# rd_shadow_fine_ab: the shadow's levels at the scene's scale above 1x
# (port/render/rd_shadowres.c), the default, against the PS2's sizes
# (ICO_RD_SHADOW_FINE=0, the tests' switch), on the dumps of -DLIST
# (tools/shadow_fine_dumps.txt: a listed dump that is absent FAILS; its second
# field the --nop spec of its three shadow composites, its third the settings
# it runs at).  Each run is rd_replay_tool's --pass-dump with --stats at a
# setting:
#   adreno   the issue 123 tablet's path (ICO_VK_FAKE_PASS_ROW=qualcomm
#            ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1), Custom 3x, Full height,
#            Full pixel, presented 3048x2032 (levels at 3 x 3);
#   e1x      the same path, Enhanced at 1x with Full pixel, presented 1280x720;
#   o1x      the same path, the Original preset, presented 1280x720;
#   phone    the PowerVR phone (ICO_VK_FAKE_LIMITS=moltenvk): Resolution Auto,
#            2364x1080 with Full height and Full pixel (4.10417 x 2.41071: levels
#            at 5 x 3);
#   msaa     Enhanced 2x under MSAA 4x (the count from the MSAA epoch's
#            covered-majority resolve, the seven draws: no full-size pass);
#   bspline  Enhanced 2x with [dev] shadow_bspline (ICO_RD_SHADOW_BSPLINE=1);
#   owner    the owner's Pixel (ICO_VK_FAKE_LIMITS=moltenvk) at its Resolution
#            window, 2385x1080 over an 848-wide frame with Full height
#            (2.8125 x 2.41071: levels at 3 x 3);
#   tall     848x896 with Full height over an 848-wide frame (1 x 2: levels at
#            1 x 3, the step and the tent the identity across);
#   pixel    the owner's Pixel as rc3 ran it (ICO_VK_FAKE_LIMITS=moltenvk):
#            Enhanced, Resolution window 2364x1080 over the 848-wide frame,
#            Full height, Full pixel, the anisotropic filter (levels at 3 x 3;
#            the frame where rc3's chain fell back: the game leaves the
#            levels' wrap at REPEAT, which the first recogniser refused);
#   force    e1x with ICO_RD_SHADOW_FINE=force against e1x's default (the fine
#            chain at 1x: S 1 on both axes);
#   gl1x, gl2x  -DBACKEND=opengl: Enhanced 1x and 2x on OpenGL (llvmpipe), on
#            every dump.
# The checks, every expectation from the code (unverified until the first run):
#   1. the PS2's sizes are v0.11.6's picture: at every setting but force and
#      the OpenGL ones, every PNG the golden list (-DGOLDENS,
#      tools/shadow_fine_goldens.txt: v0.11.6's rd_replay_tool at the same
#      settings on this machine's lavapipe) names for the dump has its SHA-256;
#   2. at 1x (e1x, o1x, gl1x) the default is the PS2's sizes: every PNG the
#      same bytes, the same draws and passes, "shadow levels: 0 shadows at the
#      scene's scale, 0 passes, 0 at the PS2's size";
#   3. above 1x (adreno, phone, msaa, bspline, owner, tall, pixel, gl2x), the
#      default against the PS2's sizes: on every dump exactly one shadow at the
#      scene's scale and none drawn at the PS2's size (no reason in the stats
#      line: a silent fallback fails here);
#      its passes 3 levels + 6 tent passes + 1 full-size count where the count
#      is the one draw ("shadow count: N in one draw", N > 0), so 10 or 9; the
#      passes exactly the PS2 run's plus those (no SCENE pass more), the draws
#      the PS2 run's plus 9 (the full-size count's draw is not counted, as the
#      one draw's is not); the levels' texels a GS pixel the size line names
#      as the setting expects (3x3, or 1x3 at tall); the present (or SCENE)
#      not the same bytes; and with lists 4-12 left out (SCENE right after
#      list 3) every SCENE texel the fine levels change lies within D of a
#      texel the PS2's levels darken, and the darkness (the RGB levels the
#      composites take away, summed) within 3 % of the PS2's
#      (shadow_fine_check.py; the edge model's: under 0.2 %);
#   4. bspline: also the default with [dev] shadow_bspline against the
#      default without it at 2x, every PNG (the B-spline does nothing to a
#      fine level);
#   5. force: the fine chain at 1x, one shadow, 9 passes of its own, SCENE
#      within one level of the default's at every channel (png_level_diff.py:
#      the step's own rounding of 0.75 / 0.25 against the GS sprite's bilinear
#      tap, then the composite's; no texel more than a level).
# No device: SKIP (77).  -DTOOL -DLIST -DGOLDENS -DPY -DCHECK -DDIFF -DOUT -DBACKEND
if(NOT IS_DIRECTORY "/primary/dev/ico-dumps")
  message(STATUS "SKIP: no dump folder on this machine (/primary/dev/ico-dumps): the list's dumps are the build machine's")
  return()
endif()
set(failures 0)
if(NOT EXISTS "${LIST}")
  message(FATAL_ERROR "rd_shadow_fine_ab: no dump list ${LIST}")
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
  if(nf LESS 3)
    message(SEND_ERROR "${path}: the list names no --nop spec or settings for it")
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
  list(GET fields 2 sets)
  string(REPLACE "," ";" sets_${path} "${sets}")
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
  message(SEND_ERROR "rd_shadow_fine_ab: no golden list ${GOLDENS}")
  math(EXPR failures "${failures} + 1")
endif()

set(tablet ICO_VK_FAKE_PASS_ROW=qualcomm ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1)
set(env_adreno ${tablet})
set(args_adreno --enhanced --resolution 3x --full-height --full-pixel --present 3048x2032)
set(levels_adreno 3x3)
set(env_e1x ${tablet})
set(args_e1x --enhanced --resolution 1x --full-pixel --present 1280x720)
set(env_o1x ${tablet})
set(args_o1x --present 1280x720)
set(env_phone ICO_VK_FAKE_LIMITS=moltenvk)
set(args_phone --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080)
set(levels_phone 5x3)
set(env_msaa "")
set(args_msaa --enhanced --resolution 2x --aa msaa4 --present 1280x720)
set(levels_msaa 3x3)
set(env_bspline ICO_RD_SHADOW_BSPLINE=1)
set(args_bspline --enhanced --resolution 2x --present 1280x720)
set(levels_bspline 3x3)
set(env_owner ICO_VK_FAKE_LIMITS=moltenvk)
set(args_owner --enhanced --resolution 2385x1080 --full-height --present 2385x1080)
set(levels_owner 3x3)
set(env_pixel ICO_VK_FAKE_LIMITS=moltenvk)
set(args_pixel --enhanced --resolution 2364x1080 --full-height --full-pixel --filter anisotropic
               --present 2364x1080)
set(levels_pixel 3x3)
set(env_tall "")
set(args_tall --enhanced --resolution 848x896 --full-height --present 1280x720)
set(levels_tall 1x3)
set(env_gl1x "")
set(args_gl1x --enhanced --resolution 1x --present 1280x720)
set(env_gl2x "")
set(args_gl2x --enhanced --resolution 2x --present 1280x720)
set(levels_gl2x 3x3)
set(same e1x o1x gl1x)

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(fineRe "stats: shadow levels: ([0-9]+) shadows at the scene's scale, ([0-9]+) passes, ([0-9]+) at the PS2's size([^\n,]*)")
set(oneRe "stats: shadow count: ([0-9]+) in one draw")
set(sizeRe "shadow levels: at the scene's scale: SHADOW0 [0-9]+x[0-9]+, SHADOW1 [0-9]+x[0-9]+, SHADOW2 [0-9]+x[0-9]+ \\(([0-9.]+)x([0-9.]+) texels a GS pixel\\) from the count's ([0-9.]+)x([0-9.]+) texels a GS pixel")
set(diffRe "png_level_diff: [0-9]+ x [0-9]+, [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, ([0-9]+) more")
set(nopRest "4:0-1000000,5:0-1000000,6:0-1000000,7:0-1000000,8:0-1000000,9:0-1000000,10:0-1000000,11:0-1000000,12:0-1000000")

# one run: -> ok_<name> draws_<name> passes_<name> chains_<name> fpasses_<name>
# refused_<name> one_<name> err_<name> (the parent's scope)
function(sf_run name dump dir envs)
  set(args ${ARGN})
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envs}
                          "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(err_${name} "${err}" PARENT_SCOPE)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    set(ok_${name} SKIP PARENT_SCOPE)
    return()
  endif()
  string(REPLACE ";" "," flat "${out}")
  if(NOT rc EQUAL 0 OR NOT flat MATCHES "${drawsRe}" OR NOT flat MATCHES "${fineRe}" OR
     NOT flat MATCHES "${oneRe}")
    message(SEND_ERROR "${dump} ${name}: exit ${rc}, or no draws, shadow levels or shadow "
                       "count line:\n${out}\n${err}")
    set(ok_${name} FALSE PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCH "${drawsRe}" m "${flat}")
  set(draws_${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(passes_${name} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  string(REGEX MATCH "${fineRe}" m "${flat}")
  set(chains_${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(fpasses_${name} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  set(refused_${name} "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(whys_${name} "${CMAKE_MATCH_4}" PARENT_SCOPE)
  string(REGEX MATCH "${oneRe}" m "${flat}")
  set(one_${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(ok_${name} TRUE PARENT_SCOPE)
endfunction()

# every PNG of dir a the same bytes in dir b, the same set
function(sf_same what a b)
  file(GLOB pa RELATIVE "${a}" "${a}/*.png")
  file(GLOB pb RELATIVE "${b}" "${b}/*.png")
  list(LENGTH pa na)
  list(LENGTH pb nb)
  if(na EQUAL 0 OR NOT na EQUAL nb)
    message(SEND_ERROR "${what}: ${na} targets written against ${nb}")
    set(sfFail TRUE PARENT_SCOPE)
    return()
  endif()
  foreach(png IN LISTS pa)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${a}/${png}" "${b}/${png}"
                    RESULT_VARIABLE differ)
    if(NOT differ EQUAL 0)
      message(SEND_ERROR "${what}: ${png} differs")
      set(sfFail TRUE PARENT_SCOPE)
    endif()
  endforeach()
endfunction()

# the golden PNGs of (dump, setting) in dir; none listed fails (the dump list
# and the goldens drifted apart)
function(sf_golden what dumpName s dir)
  list(FIND goldenKeys "${dumpName}__${s}" at)
  if(at LESS 0)
    message(SEND_ERROR "${what}: no goldens for ${dumpName} at ${s}")
    set(sfFail TRUE PARENT_SCOPE)
    return()
  endif()
  set(n 0)
  foreach(e IN LISTS gold_${dumpName}__${s})
    string(REGEX MATCH "^([^=]+)=(.*)$" m "${e}")
    set(png "${CMAKE_MATCH_1}")
    set(want "${CMAKE_MATCH_2}")
    if(NOT EXISTS "${dir}/${png}")
      message(SEND_ERROR "${what}: ${png} not written (v0.11.6 wrote it)")
      set(sfFail TRUE PARENT_SCOPE)
      continue()
    endif()
    file(SHA256 "${dir}/${png}" got)
    if(NOT got STREQUAL want)
      message(SEND_ERROR "${what}: ${png} is not v0.11.6's (${got}, ${want})")
      set(sfFail TRUE PARENT_SCOPE)
    endif()
    math(EXPR n "${n} + 1")
  endforeach()
  message(STATUS "${what}: ${n} PNGs against v0.11.6's")
endfunction()

set(compared 0)
set(acted 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  get_filename_component(dumpName "${dump}" NAME)
  if("${BACKEND}" STREQUAL "opengl")
    set(settings gl1x gl2x)
  else()
    set(settings ${sets_${dump}})
  endif()
  foreach(s IN LISTS settings)
    set(sfFail FALSE)
    set(base "${OUT}/${index}-${s}")
    if(s STREQUAL "force")
      # 5. the fine chain at 1x
      sf_run(d "${dump}" "${base}-default" "${env_e1x}" ${args_e1x})
      sf_run(f "${dump}" "${base}-force" "${env_e1x};ICO_RD_SHADOW_FINE=force" ${args_e1x})
      if(ok_d STREQUAL "SKIP" OR ok_f STREQUAL "SKIP")
        return()
      endif()
      if(NOT ok_d OR NOT ok_f)
        math(EXPR failures "${failures} + 1")
        continue()
      endif()
      math(EXPR compared "${compared} + 1")
      math(EXPR wantAll "${passes_d} + 9")
      if(NOT chains_f EQUAL 1 OR NOT fpasses_f EQUAL 9 OR NOT refused_f EQUAL 0 OR
         NOT passes_f EQUAL wantAll)
        message(SEND_ERROR "${dumpName} force: ${chains_f} shadows (1), ${fpasses_f} passes of "
                           "its own (9), ${refused_f} refused, ${passes_f} passes (${wantAll})")
        set(sfFail TRUE)
      endif()
      execute_process(COMMAND "${PY}" -I "${DIFF}" "${base}-default/SCENE.png"
                              "${base}-force/SCENE.png"
                      RESULT_VARIABLE drc OUTPUT_VARIABLE dout ERROR_VARIABLE derr)
      message(STATUS "${dumpName} force: ${dout}${derr}")
      string(REGEX MATCH "${diffRe}" m "${dout}")
      if(NOT drc EQUAL 0 OR m STREQUAL "" OR NOT CMAKE_MATCH_1 EQUAL 0)
        message(SEND_ERROR "${dumpName} force: SCENE more than a level from the default's")
        set(sfFail TRUE)
      endif()
      if(sfFail)
        math(EXPR failures "${failures} + 1")
      endif()
      continue()
    endif()
    sf_run(p "${dump}" "${base}-ps2" "${env_${s}};ICO_RD_SHADOW_FINE=0" ${args_${s}})
    if(ok_p STREQUAL "SKIP")
      return()
    endif()
    sf_run(h "${dump}" "${base}-fine" "${env_${s}}" ${args_${s}})
    if(ok_h STREQUAL "SKIP")
      return()
    endif()
    if(NOT ok_p OR NOT ok_h)
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    message(STATUS "${dumpName} ${s}: the PS2's sizes ${passes_p} passes ${draws_p} draws; the "
                   "default ${passes_h} passes ${draws_h} draws, ${chains_h} shadows at the "
                   "scene's scale (${fpasses_h} passes), ${refused_h} refused; one draw ${one_h}")
    if(NOT chains_p EQUAL 0 OR NOT fpasses_p EQUAL 0 OR NOT refused_p EQUAL 0)
      message(SEND_ERROR "${dumpName} ${s}: ICO_RD_SHADOW_FINE=0 drew ${chains_p} shadows at the "
                         "scene's scale, ${fpasses_p} passes, ${refused_p} refused")
      set(sfFail TRUE)
    endif()
    if(NOT "${BACKEND}" STREQUAL "opengl")
      sf_golden("${dumpName} ${s} the PS2's sizes" "${dumpName}" "${s}" "${base}-ps2")
    endif()
    list(FIND same "${s}" isSame)
    if(isSame GREATER -1)
      # 2. at 1x the default is the PS2's sizes
      math(EXPR compared "${compared} + 1")
      sf_same("${dumpName} ${s} the default against the PS2's sizes" "${base}-ps2"
              "${base}-fine")
      if(NOT chains_h EQUAL 0 OR NOT fpasses_h EQUAL 0 OR NOT refused_h EQUAL 0 OR
         NOT passes_h EQUAL passes_p OR NOT draws_h EQUAL draws_p)
        message(SEND_ERROR "${dumpName} ${s}: at 1x ${chains_h} shadows at the scene's scale, "
                           "${fpasses_h} passes, ${refused_h} refused; ${passes_h}/${draws_h} "
                           "passes and draws against ${passes_p}/${draws_p}")
        set(sfFail TRUE)
      endif()
    else()
      # 3. above 1x
      math(EXPR compared "${compared} + 1")
      set(full 0)
      if(one_h GREATER 0)
        set(full 1)
      endif()
      math(EXPR wantPasses "9 + ${full}")
      math(EXPR wantAll "${passes_p} + ${fpasses_h}")
      math(EXPR wantDraws "${draws_p} + 9 * ${chains_h}")
      if(NOT whys_h STREQUAL "")
        message(SEND_ERROR "${dumpName} ${s}: drawn at the PS2's size for${whys_h}:\n${err_h}")
        set(sfFail TRUE)
      endif()
      if(NOT chains_h EQUAL 1 OR NOT refused_h EQUAL 0 OR NOT fpasses_h EQUAL wantPasses OR
         NOT passes_h EQUAL wantAll OR NOT draws_h EQUAL wantDraws)
        message(SEND_ERROR "${dumpName} ${s}: ${chains_h} shadows at the scene's scale (1), "
                           "${refused_h} refused (0), ${fpasses_h} passes of its own "
                           "(${wantPasses}); ${passes_h} passes (${wantAll}), ${draws_h} draws "
                           "(${wantDraws})")
        set(sfFail TRUE)
      endif()
      set(shown PRESENT.png)
      if(NOT EXISTS "${base}-ps2/PRESENT.png")
        set(shown SCENE.png)
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${base}-ps2/${shown}"
                              "${base}-fine/${shown}" RESULT_VARIABLE differ)
      if(differ EQUAL 0)
        message(SEND_ERROR "${dumpName} ${s}: the default's ${shown} is the PS2 sizes'")
        set(sfFail TRUE)
      else()
        math(EXPR acted "${acted} + 1")
      endif()
      string(REGEX MATCH "${sizeRe}" m "${err_h}")
      if(m STREQUAL "")
        message(SEND_ERROR "${dumpName} ${s}: no size line in the log:\n${err_h}")
        set(sfFail TRUE)
      else()
        set(lv "${CMAKE_MATCH_1}x${CMAKE_MATCH_2}")
        set(scale "${CMAKE_MATCH_3}")
        if(CMAKE_MATCH_4 GREATER CMAKE_MATCH_3)
          set(scale "${CMAKE_MATCH_4}")
        endif()
        if(NOT lv STREQUAL "${levels_${s}}")
          message(SEND_ERROR "${dumpName} ${s}: the levels at ${lv} texels a GS pixel, "
                             "${levels_${s}} expected")
          set(sfFail TRUE)
        endif()
        # SCENE right after list 3: the PS2's sizes, the same without the
        # composites, the default
        sf_run(pa "${dump}" "${base}-ps2-a" "${env_${s}};ICO_RD_SHADOW_FINE=0" ${args_${s}}
               --nop "${nopRest}")
        sf_run(pb "${dump}" "${base}-ps2-b" "${env_${s}};ICO_RD_SHADOW_FINE=0" ${args_${s}}
               --nop "${nopRest},${comp_${dump}}")
        sf_run(ha "${dump}" "${base}-fine-a" "${env_${s}}" ${args_${s}} --nop "${nopRest}")
        if(NOT ok_pa OR NOT ok_pb OR NOT ok_ha)
          set(sfFail TRUE)
        else()
          execute_process(COMMAND "${PY}" -I "${CHECK}" "${base}-ps2-a/SCENE.png"
                                  "${base}-ps2-b/SCENE.png" "${base}-fine-a/SCENE.png"
                                  --scale "${scale}" --dark-tol 0.03
                          RESULT_VARIABLE crc OUTPUT_VARIABLE cout ERROR_VARIABLE cerr)
          message(STATUS "${dumpName} ${s}: ${cout}${cerr}")
          if(NOT crc EQUAL 0)
            message(SEND_ERROR "${dumpName} ${s}: the change is not confined to the shadow or "
                               "its darkness moved (exit ${crc}): ${cout}${cerr}")
            set(sfFail TRUE)
          endif()
        endif()
      endif()
      if(s STREQUAL "bspline")
        # 4. the B-spline does nothing to a fine level
        sf_run(hn "${dump}" "${base}-fine-nobspline" "" ${args_bspline})
        if(ok_hn)
          sf_same("${dumpName} bspline: the default with the B-spline against without"
                  "${base}-fine-nobspline" "${base}-fine")
        else()
          set(sfFail TRUE)
        endif()
      endif()
    endif()
    if(sfFail)
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()

if(failures GREATER 0)
  message(FATAL_ERROR "rd_shadow_fine_ab: ${failures} failures over ${ndumps} dumps")
endif()
if(compared EQUAL 0 OR acted EQUAL 0)
  message(FATAL_ERROR "rd_shadow_fine_ab: ${compared} pairs compared, the fine levels acted in ${acted}")
endif()
message(STATUS "rd_shadow_fine_ab: ${compared} pairs over ${ndumps} dumps, the fine levels acted in ${acted}")
