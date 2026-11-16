# rd_shadow_fine_ab: the shadow's levels at the scene's scale above 1x
# (port/render/rd_shadowres.c), the default, against the PS2's sizes
# (ICO_RD_SHADOW_FINE=0, the tests' switch), on the fixtures of -DLIST
# (port/render/test/fixtures/lists/shadow_fine.txt: a fixture's name, the --nop
# spec of its three shadow composites, the settings it runs at; the frames are
# expanded into OUT/fixtures, docs/port/TESTING_FIXTURES.md; a listed fixture
# that is absent FAILS).  Each run is rd_replay_tool's --pass-dump with --stats
# at a setting:
#   adreno   the issue 123 tablet's path (ICO_VK_FAKE_PASS_ROW=qualcomm
#            ICO_RD_VOLUME_FS=1 ICO_RD_COMP_SPEC=1), Custom 3x, Full height,
#            Full pixel, presented 3048x2032 (levels at 3 x 3);
#   e1x      the same path, Enhanced at 1x with Full pixel, presented 1280x720;
#   o1x      the same path, the Original preset, presented 1280x720;
#   phone    the PowerVR phone (ICO_VK_FAKE_LIMITS=moltenvk): Resolution Auto,
#            2364x1080 with Full height and Full pixel (4.10417 x 2.41071: above
#            the release's cap of 3, check 8);
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
#            every fixture;
#   forms    the adreno setting's path and arguments with the form loop (check
#            9) alone, for a fixture with no goldens of its own (fx-123-blended;
#            no OpenGL run).
# The checks, every expectation from the code:
#   1. the PS2's sizes are v0.11.6's picture of the fixture: at every setting
#      but force and the OpenGL ones, every PNG the golden list (-DGOLDENS,
#      port/render/test/fixtures/lists/shadow_fine_goldens.txt: v0.11.6's
#      rd_replay_tool at the same settings on the expanded fixtures, lavapipe)
#      names for the fixture has its SHA-256, and every golden of the list was
#      matched (a golden no run asked for fails); the SHA-256 is compared
#      only where the run's device line (rd_replay_tool's "rhi_vk: device
#      table:" line) matches the list's "# driver: <regex>" line, the lavapipe
#      the goldens were made on: another lavapipe build or CPU architecture
#      rounds the same draws differently (CI run 43805687280: Mesa 25.2.8 with
#      LLVM 20.1.2 on x86-64 moved the anisotropic "pixel" pictures, on arm64
#      every picture), and there only the PNG set is checked (each golden's
#      PNG written) with checks 2-8, which compare runs of the same job;
#   2. at 1x (e1x, o1x, gl1x) the default is the PS2's sizes: every PNG the
#      same bytes, the same draws and passes, "shadow levels: 0 shadows at the
#      scene's scale, 0 passes, 0 at the PS2's size";
#   3. above 1x (adreno, msaa, bspline, owner, tall, pixel, gl2x), the
#      default against the PS2's sizes: on every fixture exactly one shadow at the
#      scene's scale and none drawn at the PS2's size (no reason in the stats
#      line: a silent fallback fails here);
#      its passes, under the cuts, 3 steps + 3 tents + 1 full-size count where
#      the count is the one draw ("shadow count: N in one draw", N > 0), so 7 or
#      6; the passes exactly the PS2 run's plus those (no SCENE pass more), the
#      draws the PS2 run's plus at most 6 (a level whose box holds nothing is a
#      clear alone; the full-size count's draw is not counted, as the
#      one draw's is not); the levels' texels a GS pixel the size line names
#      as the setting expects (3x3, or 1x3 at tall); the present (or SCENE)
#      not the same bytes; and with lists 4-12 left out (SCENE right after
#      list 3) every SCENE texel the fine levels change lies within D of a
#      texel the PS2's levels darken (shadow_fine_check.py; the darkness ratio
#      it prints is not judged: below);
#   3b. above 1x the levels themselves: rd_replay_tool --fine-check with the
#      step from the stencil (ICO_RD_SHADOW_COUNT_FORM=stencil; the tool then
#      draws the full-size pass after it) reads back the count and each level's
#      step and tent and compares them with the CPU reference
#      (rd_shadowres_math.c) on the same input: every tent texel the
#      reference's bytes (the tent is whole-number arithmetic), every step
#      texel the reference's byte or, where the reference's value lies within
#      the float bound of a rounding boundary, the byte beside it
#      (tools/rd_replay_fine.c derives the bound from the shader); the line
#      "fine check: 3 levels, 0 step texels outside the rule, 0 tent texels
#      not the reference's: ok".  This replaces a bound on the picture's
#      darkness against the PS2-size chain (10 % until the fixtures): the
#      PS2-size chain's darkness on a shadow a few texels thin swings with
#      where its 2:1 grid falls (the reference on a bar 2 GS rows tall moved
#      over 16 rows: 0.73 to 1.36 of its mean; 6 rows: 0.91 to 1.08; 24 rows:
#      0.985 to 1.011; the fine chain 0.92 to 1.06, 0.99 to 1.02 and 0.997 to
#      1.004 of its own, its mean 0.974, 0.995 and 0.999 of the PS2 chain's
#      mean), and the composites darken only the texels whose destination
#      alpha passes DATE, so one picture's ratio is bounded for broad shadows
#      on broad surfaces alone (the real frames: 0.944 to 1.002; the fixtures'
#      generated slivers: 0.84 to 1.03);
#   4. bspline: also the default with [dev] shadow_bspline against the
#      default without it at 2x, every PNG (the B-spline does nothing to a
#      fine level);
#   6. above 1x the cuts (the default) against ICO_RD_SHADOW_FINE_CUTS=0 (the
#      chain as dbedd6025 drew it): every PNG the same bytes; the cuts' own
#      passes 6 + the full-size count, at most 6 draws + its one; without
#      them 9 + it, 9 draws + its one;
#   7. above 1x three replays in a row with the cuts and without: the
#      presented pictures the same bytes (the render area's tracking);
#   8. above the cap (phone; rd_shadowres.h RD_SR_LEVEL_CAP): the default the
#      PS2's sizes, every PNG the same bytes, the same passes and draws, no
#      shadow at the scene's scale and one drawn at the PS2's size for the
#      scale, "(scale 1)", and the line "shadow levels: the PS2's sizes: the
#      scene at ... above the cap of 3";
#   9. the full-size count's forms (port/render/rd_shadow_count.h; issue 123,
#      v0.11.8), above 1x where the count is the one draw (adreno, owner,
#      pixel, tall, forms, gl2x): ICO_RD_SHADOW_COUNT_FORM=full (v0.11.7's
#      chain: the one draw, then the full-size pass), =stencil (no pass: the
#      first level's step reads the stencil through the table) and =seven (the
#      seven stencil-tested draws at the full size and box_reduce_ps), and the
#      default; every PNG of each the bytes of full's.  Each form RAN, from
#      --stats' "shadow count above 1x: S from the stencil, F full-size passes,
#      V seven draws, G guard fallbacks (form X)" line (rd_shadow_count.c):
#      full S 0 F 1 V 0, stencil S 1 F 0 V 0, seven S 0 F 0 V 1 (and "0 in
#      one draw, 1 in seven draws"), G 0 and the form's name in each; the
#      default the full-size pass (S 0 F 1 G 0: a --pass-dump replay is not
#      presented, so no check runs, and until a check passed every replay
#      takes the full-size pass, rd_shadow_count.c); the passes and draws
#      from the code against stencil's (the stencil form's count: the one
#      draw's 1 draw in 1 pass): full one pass and one draw more (fullPass's
#      1 in a pass of its own), seven one pass more and 7 draws more (the
#      seven draws' pass, RD_SHADOW_RESOLVE_PASSES 7 draws at step 4, every
#      fixture of the list (a step-8 count skips bit 5's: 6), and
#      shadowReduce's pass and 1 draw, for the one draw's pass and 1 draw:
#      7 + 1 - 1 = 7 draws, 2 - 1 = 1 pass; against full's 2 draws in 2
#      passes: 6 and 0, v0.11.7's measured 205 -> 211 on fx-adreno), the
#      default full's.  The guard (adreno and forms):
#      ICO_RD_SHADOW_COUNT_TOUCH=1 touches the depth before the step from the
#      stencil, so the full-size pass is drawn after all: S 0 F 1 G 1, every
#      PNG full's bytes;
#   5. force: the fine chain at 1x, one shadow, 6 passes of its own, SCENE
#      at most 6 levels from the default's at any channel (png_level_diff.py).
#      At 1x the fine chain is the GS step exactly; the PS2-size chain on the
#      GPU is the sprite's hardware bilinear, which lavapipe rounds up by 1
#      level on about 10 % of the nonzero level texels (measured from the
#      dumped SHADOW0..2 of rc5's run: 19 of 252 and 19 of 86 above the exact
#      step, 1 below).  A level off by 1 changes a composite's As by at most 1
#      ((a w) >> 7, w <= 128), so SCENE by at most ceil(255 / 128) = 2 a
#      composite, 6 over the three (rc5's run: largest 4, 1011 pixels).
# The path ran (rd_fixture_require): on the default run of every pair above
# 1x, and on the forced run, the fixture's declared shadow_levels and
# shadow_level_texels (the levels drawn at the scene's scale); a fixture no
# such run checked fails.
# No device: SKIP (77).  -DTOOL -DFIXTURE_TOOL -DFIXTURES -DFIXTURE_HELPERS
# -DLIST -DGOLDENS -DPY -DCHECK -DDIFF -DOUT -DBACKEND
include("${FIXTURE_HELPERS}")
set(failures 0)
rd_fixture_list("${LIST}" names)
set(dumps "")
foreach(name IN LISTS names)
  string(REGEX REPLACE "[ \t]+" ";" fields "${RDFX_FIELDS_${name}}")
  list(LENGTH fields nf)
  if(NOT nf EQUAL 2)
    message(SEND_ERROR "${name}: the list names no --nop spec or settings for it")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  rd_fixture_expand(${name} "${OUT}/fixtures" path)
  list(APPEND dumps "${path}")
  set(name_${path} "${name}")
  set(checked_${name} 0)
  list(GET fields 0 comp_${path})
  list(GET fields 1 sets)
  string(REPLACE "," ";" sets_${path} "${sets}")
endforeach()
list(LENGTH dumps ndumps)

# the goldens: "<fixture> <setting> <png> <sha256>", and "# driver: <regex>"
# naming the device line of the lavapipe they were made on (check 1)
set(goldenKeys "")
set(goldenDriverRe "")
set(goldenCompared 0)
set(goldenOtherDriver 0)
if(EXISTS "${GOLDENS}")
  file(STRINGS "${GOLDENS}" glines)
  foreach(l IN LISTS glines)
    string(STRIP "${l}" l)
    if(l MATCHES "^# driver: (.+)$")
      set(goldenDriverRe "${CMAKE_MATCH_1}")
      continue()
    endif()
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
set(env_forms ${tablet})
set(args_forms ${args_adreno})
# check 9's settings (the one draw's count above 1x) and the guard's
set(formed adreno owner pixel tall forms gl2x)
set(guarded adreno forms)
# the seven draws' draws over the stencil form's, step 4 (rd_internal.h
# RD_SHADOW_RESOLVE_PASSES 7 and shadowReduce's 1, for the one draw's 1;
# the stencil form draws no full-size pass: 7 + 1 - 1)
set(sevenExtraDraws 7)
set(same e1x o1x gl1x)
# above the release's cap (rd_shadowres.h RD_SR_LEVEL_CAP 3: levels at 5 x 3
# would be drawn): the PS2's sizes, v0.11.6's shadow
set(capped phone)
set(capRe "shadow levels: the PS2's sizes: the scene at [0-9.]+x[0-9.]+ texels a GS pixel, above the cap of 3")

set(drawsRe "stats: ([0-9]+) draws, ([0-9]+) passes")
set(fineRe "stats: shadow levels: ([0-9]+) shadows at the scene's scale, ([0-9]+) passes, ([0-9]+) at the PS2's size([^\n,]*)")
set(oneRe "stats: shadow count: ([0-9]+) in one draw, ([0-9]+) in seven draws")
set(countRe "stats: shadow count above 1x: ([0-9]+) from the stencil, ([0-9]+) full-size passes, ([0-9]+) seven draws, ([0-9]+) guard fallbacks \\(form ([a-z]+)\\)")
set(sizeRe "shadow levels: at the scene's scale: SHADOW0 [0-9]+x[0-9]+, SHADOW1 [0-9]+x[0-9]+, SHADOW2 [0-9]+x[0-9]+ \\(([0-9.]+)x([0-9.]+) texels a GS pixel\\) from the count's ([0-9.]+)x([0-9.]+) texels a GS pixel")
set(diffRe "png_level_diff: [0-9]+ x [0-9]+, [0-9]+ channels: [0-9]+ equal, [0-9]+ one level apart, ([0-9]+) more \\(largest ([0-9]+)\\)")
set(fineOkRe "fine check: 3 levels, 0 step texels outside the rule, 0 tent texels not the reference's: ok")
set(nopRest "4:0-1000000,5:0-1000000,6:0-1000000,7:0-1000000,8:0-1000000,9:0-1000000,10:0-1000000,11:0-1000000,12:0-1000000")

# one run: -> ok_<name> draws_<name> passes_<name> chains_<name> fpasses_<name>
# refused_<name> one_<name> err_<name> out_<name> (the parent's scope)
function(sf_run name dump dir envs)
  set(args ${ARGN})
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envs}
                          "${TOOL}" "${dump}" - --pass-dump "${dir}" --stats ${args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(err_${name} "${err}" PARENT_SCOPE)
  set(out_${name} "${out}" PARENT_SCOPE)
  if(rc EQUAL 77)
    message(STATUS "SKIP: no device: ${err}")
    set(ok_${name} SKIP PARENT_SCOPE)
    return()
  endif()
  string(REPLACE ";" "," flat "${out}")
  if(NOT rc EQUAL 0 OR NOT flat MATCHES "${drawsRe}" OR NOT flat MATCHES "${fineRe}" OR
     NOT flat MATCHES "${oneRe}" OR NOT flat MATCHES "${countRe}")
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
  set(seven_${name} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  string(REGEX MATCH "${countRe}" m "${flat}")
  set(cst_${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(cfull_${name} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  set(cseven_${name} "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(cguard_${name} "${CMAKE_MATCH_4}" PARENT_SCOPE)
  set(cform_${name} "${CMAKE_MATCH_5}" PARENT_SCOPE)
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

# the golden PNGs of (fixture, setting) in dir; none listed fails (the fixture
# list and the goldens drifted apart); devErr: the run's stderr (its device
# line); the bytes are compared on the goldens' driver alone
function(sf_golden what dumpName s dir devErr)
  list(FIND goldenKeys "${dumpName}__${s}" at)
  if(at LESS 0)
    message(SEND_ERROR "${what}: no goldens for ${dumpName} at ${s}")
    set(sfFail TRUE PARENT_SCOPE)
    return()
  endif()
  set(goldenUsed ${goldenUsed} "${dumpName}__${s}" PARENT_SCOPE)
  set(bytes TRUE)
  if(NOT goldenDriverRe STREQUAL "" AND NOT devErr MATCHES "${goldenDriverRe}")
    set(bytes FALSE)
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
    math(EXPR n "${n} + 1")
    if(NOT bytes)
      continue()
    endif()
    file(SHA256 "${dir}/${png}" got)
    if(NOT got STREQUAL want)
      message(SEND_ERROR "${what}: ${png} is not v0.11.6's (${got}, ${want})")
      set(sfFail TRUE PARENT_SCOPE)
    endif()
  endforeach()
  if(bytes)
    math(EXPR c "${goldenCompared} + 1")
    set(goldenCompared ${c} PARENT_SCOPE)
    message(STATUS "${what}: ${n} PNGs against v0.11.6's")
  else()
    math(EXPR c "${goldenOtherDriver} + 1")
    set(goldenOtherDriver ${c} PARENT_SCOPE)
    string(REGEX MATCH "rhi_vk: device table: [^\n]*" dev "${devErr}")
    message(STATUS "${what}: ${n} PNGs written; their bytes not compared: the device is not "
                   "the goldens' (${goldenDriverRe}): ${dev}")
  endif()
endfunction()

# 9. the forms of the full-size count (the header): runs of dump at setting s
# under base; sets sfFail in the caller on a failure
function(sf_forms dumpName dump s base)
  sf_run(ff "${dump}" "${base}-form-full" "${env_${s}};ICO_RD_SHADOW_COUNT_FORM=full" ${args_${s}})
  sf_run(fs "${dump}" "${base}-form-stencil" "${env_${s}};ICO_RD_SHADOW_COUNT_FORM=stencil"
         ${args_${s}})
  sf_run(fv "${dump}" "${base}-form-seven" "${env_${s}};ICO_RD_SHADOW_COUNT_FORM=seven"
         ${args_${s}})
  sf_run(fd "${dump}" "${base}-form-default" "${env_${s}}" ${args_${s}})
  if(NOT ok_ff OR NOT ok_fs OR NOT ok_fv OR NOT ok_fd)
    set(sfFail TRUE PARENT_SCOPE)
    return()
  endif()
  set(bad FALSE)
  message(STATUS "${dumpName} ${s} forms: full ${passes_ff}/${draws_ff} passes and draws, stencil "
                 "${passes_fs}/${draws_fs}, seven ${passes_fv}/${draws_fv}; the default form "
                 "${cform_fd} (${cst_fd} from the stencil, ${cfull_fd} full-size passes)")
  if(NOT one_ff EQUAL 1 OR NOT cst_ff EQUAL 0 OR NOT cfull_ff EQUAL 1 OR NOT cseven_ff EQUAL 0 OR
     NOT cguard_ff EQUAL 0 OR NOT cform_ff STREQUAL "full")
    message(SEND_ERROR "${dumpName} ${s}: the full-size pass did not run as asked: one draw "
                       "${one_ff}, ${cst_ff} from the stencil, ${cfull_ff} full-size passes, "
                       "${cseven_ff} seven draws, ${cguard_ff} guard fallbacks, form ${cform_ff}")
    set(bad TRUE)
  endif()
  if(NOT one_fs EQUAL 1 OR NOT cst_fs EQUAL 1 OR NOT cfull_fs EQUAL 0 OR
     NOT cseven_fs EQUAL 0 OR NOT cguard_fs EQUAL 0 OR NOT cform_fs STREQUAL "stencil")
    message(SEND_ERROR "${dumpName} ${s}: the step from the stencil did not run: one draw "
                       "${one_fs}, ${cst_fs} from the stencil, ${cfull_fs} full-size passes, "
                       "${cseven_fs} seven draws, ${cguard_fs} guard fallbacks, form ${cform_fs}")
    set(bad TRUE)
  endif()
  # the default (no check in a --pass-dump replay): the full-size pass
  if(NOT one_fd EQUAL 1 OR NOT cst_fd EQUAL 0 OR NOT cfull_fd EQUAL 1 OR NOT cseven_fd EQUAL 0 OR
     NOT cguard_fd EQUAL 0 OR NOT cform_fd STREQUAL "full")
    message(SEND_ERROR "${dumpName} ${s}: the default is not the full-size pass: one draw "
                       "${one_fd}, ${cst_fd} from the stencil, ${cfull_fd} full-size passes, "
                       "${cseven_fd} seven draws, ${cguard_fd} guard fallbacks, form ${cform_fd}")
    set(bad TRUE)
  endif()
  if(NOT one_fv EQUAL 0 OR NOT seven_fv EQUAL 1 OR NOT cst_fv EQUAL 0 OR NOT cfull_fv EQUAL 0 OR
     NOT cseven_fv EQUAL 1 OR NOT cguard_fv EQUAL 0 OR NOT cform_fv STREQUAL "seven")
    message(SEND_ERROR "${dumpName} ${s}: the seven draws did not run as asked: ${one_fv} in one "
                       "draw, ${seven_fv} in seven, ${cst_fv} from the stencil, ${cfull_fv} "
                       "full-size passes, ${cseven_fv} seven draws, form ${cform_fv}")
    set(bad TRUE)
  endif()
  math(EXPR wFullP "${passes_fs} + 1")
  math(EXPR wFullD "${draws_fs} + 1")
  math(EXPR wSevenP "${passes_fs} + 1")
  math(EXPR wSevenD "${draws_fs} + ${sevenExtraDraws}")
  if(NOT passes_ff EQUAL wFullP OR NOT draws_ff EQUAL wFullD OR NOT passes_fv EQUAL wSevenP OR
     NOT draws_fv EQUAL wSevenD OR NOT passes_fd EQUAL passes_ff OR NOT draws_fd EQUAL draws_ff)
    message(SEND_ERROR "${dumpName} ${s}: passes and draws: full ${passes_ff}/${draws_ff} "
                       "(${wFullP}/${wFullD}), seven ${passes_fv}/${draws_fv} "
                       "(${wSevenP}/${wSevenD}), default ${passes_fd}/${draws_fd} "
                       "(${passes_ff}/${draws_ff})")
    set(bad TRUE)
  endif()
  set(sfFail FALSE)
  sf_same("${dumpName} ${s}: the stencil step against the full-size pass" "${base}-form-full"
          "${base}-form-stencil")
  sf_same("${dumpName} ${s}: the seven draws against the full-size pass" "${base}-form-full"
          "${base}-form-seven")
  sf_same("${dumpName} ${s}: the default against the full-size pass" "${base}-form-full"
          "${base}-form-default")
  list(FIND guarded "${s}" isGuarded)
  if(isGuarded GREATER -1)
    sf_run(fg "${dump}" "${base}-form-guard"
           "${env_${s}};ICO_RD_SHADOW_COUNT_FORM=stencil;ICO_RD_SHADOW_COUNT_TOUCH=1" ${args_${s}})
    if(NOT ok_fg)
      set(bad TRUE)
    else()
      if(NOT cst_fg EQUAL 0 OR NOT cfull_fg EQUAL 1 OR NOT cguard_fg EQUAL 1 OR
         NOT passes_fg EQUAL passes_ff OR NOT draws_fg EQUAL draws_ff OR
         NOT err_fg MATCHES "shadow count: the full-size pass drawn after all: the depth was touched before the level's step")
        message(SEND_ERROR "${dumpName} ${s}: the guard: ${cst_fg} from the stencil (0), "
                           "${cfull_fg} full-size passes (1), ${cguard_fg} guard fallbacks (1), "
                           "${passes_fg}/${draws_fg} passes and draws (${passes_ff}/${draws_ff}), "
                           "or no guard line:\n${err_fg}")
        set(bad TRUE)
      endif()
      sf_same("${dumpName} ${s}: the guard's full-size pass against the full-size pass"
              "${base}-form-full" "${base}-form-guard")
    endif()
  endif()
  if(bad OR sfFail)
    set(sfFail TRUE PARENT_SCOPE)
  endif()
endfunction()

set(compared 0)
set(acted 0)
set(index 0)
set(goldenUsed "")
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(dumpName "${name_${dump}}")
  if("${BACKEND}" STREQUAL "opengl" AND "${sets_${dump}}" STREQUAL "forms")
    # a fixture for the forms alone has no OpenGL run (gl2x's form loop runs
    # on the others where the count is the one draw there)
    unset(checked_${dumpName})
    continue()
  elseif("${BACKEND}" STREQUAL "opengl")
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
      rd_fixture_require(${dumpName} "${out_f}" shadow_levels shadow_level_texels)
      math(EXPR checked_${dumpName} "${checked_${dumpName}} + 1")
      math(EXPR wantAll "${passes_d} + 6")
      if(NOT chains_f EQUAL 1 OR NOT fpasses_f EQUAL 6 OR NOT refused_f EQUAL 0 OR
         NOT passes_f EQUAL wantAll)
        message(SEND_ERROR "${dumpName} force: ${chains_f} shadows (1), ${fpasses_f} passes of "
                           "its own (6), ${refused_f} refused, ${passes_f} passes (${wantAll})")
        set(sfFail TRUE)
      endif()
      execute_process(COMMAND "${PY}" -I "${DIFF}" "${base}-default/SCENE.png"
                              "${base}-force/SCENE.png"
                      RESULT_VARIABLE drc OUTPUT_VARIABLE dout ERROR_VARIABLE derr)
      message(STATUS "${dumpName} force: ${dout}${derr}")
      string(REGEX MATCH "${diffRe}" m "${dout}")
      if(NOT drc EQUAL 0 OR m STREQUAL "" OR CMAKE_MATCH_2 GREATER 6)
        message(SEND_ERROR "${dumpName} force: SCENE more than 6 levels from the default's")
        set(sfFail TRUE)
      endif()
      if(sfFail)
        math(EXPR failures "${failures} + 1")
      endif()
      continue()
    endif()
    if(s STREQUAL "forms")
      # 9. the forms alone (a fixture with no goldens)
      sf_forms("${dumpName}" "${dump}" "${s}" "${base}")
      math(EXPR compared "${compared} + 1")
      math(EXPR checked_${dumpName} "${checked_${dumpName}} + 1")
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
      sf_golden("${dumpName} ${s} the PS2's sizes" "${dumpName}" "${s}" "${base}-ps2"
                "${err_p}")
    endif()
    list(FIND same "${s}" isSame)
    list(FIND capped "${s}" isCapped)
    if(isCapped GREATER -1)
      # 8. above the cap: every PNG the PS2 sizes' bytes, its passes and draws,
      # no shadow at the scene's scale, one refused for the scale, the line
      math(EXPR compared "${compared} + 1")
      sf_same("${dumpName} ${s} above the cap against the PS2's sizes" "${base}-ps2"
              "${base}-fine")
      if(NOT chains_h EQUAL 0 OR NOT fpasses_h EQUAL 0 OR NOT refused_h EQUAL 1 OR
         NOT whys_h STREQUAL " (scale 1)" OR NOT passes_h EQUAL passes_p OR
         NOT draws_h EQUAL draws_p)
        message(SEND_ERROR "${dumpName} ${s}: above the cap ${chains_h} shadows at the scene's "
                           "scale (0), ${fpasses_h} passes (0), ${refused_h} refused${whys_h} (1 "
                           "for the scale); ${passes_h}/${draws_h} passes and draws against "
                           "${passes_p}/${draws_p}")
        set(sfFail TRUE)
      endif()
      if(NOT err_h MATCHES "${capRe}")
        message(SEND_ERROR "${dumpName} ${s}: no cap line in the log:\n${err_h}")
        set(sfFail TRUE)
      endif()
    elseif(isSame GREATER -1)
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
      rd_fixture_require(${dumpName} "${out_h}" shadow_levels shadow_level_texels)
      math(EXPR checked_${dumpName} "${checked_${dumpName}} + 1")
      # the full-size passes the default drew (rd_shadow_count.h: a
      # --pass-dump replay is not presented, no check runs, and until one
      # passed the count is the full-size pass's)
      set(full "${cfull_h}")
      if(one_h GREATER 0 AND (NOT cst_h EQUAL 0 OR NOT cfull_h EQUAL 1 OR NOT cguard_h EQUAL 0))
        message(SEND_ERROR "${dumpName} ${s}: the default's count: ${cst_h} from the stencil (0), "
                           "${cfull_h} full-size passes (1), ${cguard_h} guard fallbacks (0)")
        set(sfFail TRUE)
      endif()
      # the cuts: a step and the tent in one pass a level (a level whose box
      # holds nothing: its clear alone, no draw)
      math(EXPR wantPasses "6 + ${full}")
      math(EXPR wantAll "${passes_p} + ${fpasses_h}")
      # every draw the RHI records counts, the one draw's full-size count's too
      math(EXPR wantDraws "${draws_p} + 6 * ${chains_h} + ${full}")
      if(NOT whys_h STREQUAL "")
        message(SEND_ERROR "${dumpName} ${s}: drawn at the PS2's size for${whys_h}:\n${err_h}")
        set(sfFail TRUE)
      endif()
      if(NOT chains_h EQUAL 1 OR NOT refused_h EQUAL 0 OR NOT fpasses_h EQUAL wantPasses OR
         NOT passes_h EQUAL wantAll OR draws_h GREATER wantDraws OR draws_h LESS draws_p)
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
                                  --scale "${scale}" --dark-tol none
                          RESULT_VARIABLE crc OUTPUT_VARIABLE cout ERROR_VARIABLE cerr)
          message(STATUS "${dumpName} ${s}: ${cout}${cerr}")
          if(NOT crc EQUAL 0)
            message(SEND_ERROR "${dumpName} ${s}: the change is not confined to the shadow "
                               "(exit ${crc}): ${cout}${cerr}")
            set(sfFail TRUE)
          endif()
        endif()
      endif()
      # 3b. the levels against the CPU reference on the same count
      # (the step from the stencil, ICO_RD_SHADOW_COUNT_FORM=stencil: --fine-check
      # draws the full-size pass after it, so the reference reads the count
      # the step stood for: the stencil-to-table equality)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${s}} ICO_RD_SHADOW_COUNT_FORM=stencil
                              "${TOOL}" "${dump}" "${base}-finecheck.png" --fine-check
                              ${args_${s}}
                      RESULT_VARIABLE frc OUTPUT_VARIABLE fout ERROR_VARIABLE ferr)
      string(REPLACE ";" "," fflat "${fout}")
      string(REGEX MATCHALL "fine check: [^\n]*" flines "${fflat}")
      foreach(fl IN LISTS flines)
        message(STATUS "${dumpName} ${s}: ${fl}")
      endforeach()
      if(NOT frc EQUAL 0 OR NOT fflat MATCHES "${fineOkRe}")
        message(SEND_ERROR "${dumpName} ${s}: the levels are not the CPU reference's (exit "
                           "${frc}):\n${fout}\n${ferr}")
        set(sfFail TRUE)
      endif()
      # 6. the cuts against the chain without them (ICO_RD_SHADOW_FINE_CUTS=0:
      # nine passes over every texel): every PNG the same bytes, its passes
      # 9 + the full-size count, 9 draws
      sf_run(hc "${dump}" "${base}-fine-nocuts" "${env_${s}};ICO_RD_SHADOW_FINE_CUTS=0"
             ${args_${s}})
      if(NOT ok_hc)
        set(sfFail TRUE)
      else()
        sf_same("${dumpName} ${s}: the cuts against the chain without them" "${base}-fine-nocuts"
                "${base}-fine")
        math(EXPR wantNc "9 + ${cfull_hc}")
        math(EXPR wantNcDraws "${draws_p} + 9 + ${cfull_hc}")
        if(NOT chains_hc EQUAL 1 OR NOT fpasses_hc EQUAL wantNc OR NOT draws_hc EQUAL wantNcDraws)
          message(SEND_ERROR "${dumpName} ${s}: without the cuts ${chains_hc} shadows, "
                             "${fpasses_hc} passes (${wantNc}), ${draws_hc} draws (${wantNcDraws})")
          set(sfFail TRUE)
        endif()
      endif()
      # 7. the render area across pictures: three replays in a row (the second
      # and third pass over the union of the box and what the texture held),
      # the presented picture the same bytes as the chain without the cuts
      foreach(cv 1 0)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env_${s}} ICO_RD_SHADOW_FINE_CUTS=${cv}
                                "${TOOL}" "${dump}" "${base}-repeat-${cv}.png" --repeat 3
                                ${args_${s}}
                        RESULT_VARIABLE rrc OUTPUT_VARIABLE rout ERROR_VARIABLE rerr)
        if(NOT rrc EQUAL 0)
          message(SEND_ERROR "${dumpName} ${s}: --repeat 3 with the cuts ${cv}: exit ${rrc}\n${rerr}")
          set(sfFail TRUE)
        endif()
      endforeach()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${base}-repeat-1.png"
                              "${base}-repeat-0.png" RESULT_VARIABLE rdiffer)
      if(NOT rdiffer EQUAL 0)
        message(SEND_ERROR "${dumpName} ${s}: the third replay with the cuts is not the chain's")
        set(sfFail TRUE)
      endif()
      list(FIND formed "${s}" isFormed)
      if(isFormed GREATER -1 AND one_h GREATER 0)
        sf_forms("${dumpName}" "${dump}" "${s}" "${base}")
      elseif(isFormed GREATER -1)
        message(STATUS "${dumpName} ${s}: the count is not the one draw here (${seven_h} in seven "
                       "draws): the full-size count's forms do not apply")
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

foreach(name IN LISTS names)
  if(DEFINED checked_${name} AND checked_${name} EQUAL 0)
    message(SEND_ERROR "${name}: no setting above 1x ran it: its properties were never checked")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()
# every golden matched: one no run asked for is a drifted list
if(NOT "${BACKEND}" STREQUAL "opengl")
  list(REMOVE_DUPLICATES goldenKeys)
  foreach(k IN LISTS goldenKeys)
    list(FIND goldenUsed "${k}" at)
    if(at LESS 0)
      message(SEND_ERROR "${GOLDENS}: the goldens of ${k} were asked for by no run")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endif()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
if(failures GREATER 0)
  message(FATAL_ERROR "rd_shadow_fine_ab: ${failures} failures over ${ndumps} fixtures")
endif()
if(compared EQUAL 0 OR acted EQUAL 0)
  message(FATAL_ERROR "rd_shadow_fine_ab: ${compared} pairs compared, the fine levels acted in ${acted}")
endif()
message(STATUS "rd_shadow_fine_ab: ${compared} pairs over ${ndumps} fixtures, the fine levels acted in ${acted}; goldens: ${goldenCompared} runs compared, ${goldenOtherDriver} on another driver")
