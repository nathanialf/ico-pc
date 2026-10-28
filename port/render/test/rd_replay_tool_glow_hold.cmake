# rd_replay_tool_glow_hold: the glow held between ticks (rd_glow_hold.c)
# against drawing its chain at every present ([dev] glow_redraw,
# ICO_RD_GLOW_REDRAW=1).  rd_replay_tool --presents N renders the N presents
# of a tick; every run sets ICO_RD_STILL_REPRESENT=0, so each present is
# rendered (rd_reshow.c would show a still one again and hide the hold).
#   still   STILL as its own tick before (--interp-any): nothing moves, so
#           the later presents' frames are the first's.  Held, the run says
#           "glow held" for presents 1..N-1 and "glow drawn" for present 0;
#           with the switch it says "glow redrawn" for 1..N-1; SCENE and the
#           presented picture of every present are the same bytes both ways.
#   moving  PREV then MOVE (consecutive ticks), N presents at alpha
#           (k + 1) / N: present 0 the same bytes both ways (the first
#           present draws the chain either way), present 1.. "glow held";
#           SCENE may differ only near the glow's mask: every differing
#           pixel within REACH texels of a pixel of WORK2 (the mask, of both
#           runs' present) that is not WORK2's fill colour (with Python and
#           Pillow; without either the picture check is skipped).
# -DTOOL=<rd_replay_tool> -DSTILL=<dump> -DPREV=<dump> -DMOVE=<dump>
# -DOUT=<dir> -DARGS=<a|b|...> -DPRESENTS=<N> -DREACH=<texels>
# -DPYTHON=<python3> (optional)
foreach(_d STILL PREV MOVE)
  if(NOT EXISTS "${${_d}}")
    # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
    message(STATUS "SKIP: ${${_d}}: no dump (build-host/tmp is not in the tree)")
    return()
  endif()
endforeach()
string(REPLACE "|" ";" ARGS "${ARGS}")
math(EXPR last "${PRESENTS} - 1")

# _case: still or moving; _mode: held or redraw; _what: SCENE, WORK2 or PRESENT
function(glow_run _case _mode _what)
  set(_env ICO_RD_STILL_REPRESENT=0)
  if(_mode STREQUAL "redraw")
    list(APPEND _env ICO_RD_GLOW_REDRAW=1)
  else()
    list(APPEND _env ICO_RD_GLOW_REDRAW=0)
  endif()
  if(_case STREQUAL "still")
    set(_in "${STILL}" --interp 0.9 "${STILL}" --interp-any)
  else()
    set(_in "${MOVE}" --interp 1.0 "${PREV}")
  endif()
  if(_what STREQUAL "PRESENT")
    set(_t --present 1280x960)
  else()
    set(_t --target ${_what})
  endif()
  file(MAKE_DIRECTORY "${OUT}/${_case}/${_mode}")
  list(GET _in 0 _dump)
  list(REMOVE_AT _in 0)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${_env} "${TOOL}" "${_dump}"
                          "${OUT}/${_case}/${_mode}/${_what}.png" ${_in} --presents ${PRESENTS}
                          ${ARGS} ${_t}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 77 OR out MATCHES "SKIP:")
    set(GLOW_SKIP 1 PARENT_SCOPE)
    return()
  endif()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${TOOL} (${_case} ${_mode} ${_what}) exited ${rc}:\n${out}\n${err}")
  endif()
  # the ";" in the tool's lines would split the match lists: flattened first
  string(REPLACE ";" "," flat "${out}")
  string(REGEX MATCHALL "present [0-9]+ of [0-9]+[^\n]*, glow held," held "${flat}")
  string(REGEX MATCHALL "present [0-9]+ of [0-9]+[^\n]*, glow redrawn \\(\\[dev\\] glow_redraw\\),"
                        redrawn "${flat}")
  string(REGEX MATCHALL "present 0 of [0-9]+[^\n]*, glow drawn \\(the tick's first present\\),"
                        first "${flat}")
  list(LENGTH held n_held)
  list(LENGTH redrawn n_redrawn)
  list(LENGTH first n_first)
  if(_mode STREQUAL "held")
    set(_want_held ${last})
    set(_want_redrawn 0)
  else()
    set(_want_held 0)
    set(_want_redrawn ${last})
  endif()
  if(NOT n_first EQUAL 1 OR NOT n_held EQUAL _want_held OR NOT n_redrawn EQUAL _want_redrawn)
    message(FATAL_ERROR "${_case} ${_mode} ${_what}: ${n_first} first presents drawing the glow "
                        "(want 1), ${n_held} held (want ${_want_held}), ${n_redrawn} redrawn "
                        "(want ${_want_redrawn}):\n${out}")
  endif()
endfunction()

set(GLOW_SKIP 0)
foreach(_case still moving)
  foreach(_mode held redraw)
    foreach(_what SCENE PRESENT WORK2)
      glow_run(${_case} ${_mode} ${_what})
      if(GLOW_SKIP)
        message(STATUS "SKIP: no usable Vulkan device or mode")
        return()
      endif()
    endforeach()
  endforeach()
endforeach()

function(same _a _b _what)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${_a}" "${_b}" RESULT_VARIABLE d)
  if(NOT d EQUAL 0)
    message(FATAL_ERROR "${_what}: ${_a} and ${_b} differ")
  endif()
endfunction()

foreach(k RANGE ${last})
  foreach(_what SCENE PRESENT)
    same("${OUT}/still/held/${_what}-p${k}.png" "${OUT}/still/redraw/${_what}-p${k}.png"
         "still present ${k} ${_what}: the held glow is not the redrawn one")
  endforeach()
endforeach()
foreach(_what SCENE PRESENT)
  same("${OUT}/moving/held/${_what}-p0.png" "${OUT}/moving/redraw/${_what}-p0.png"
       "moving present 0 ${_what}: the tick's first present changed")
endforeach()
message(STATUS "still: ${PRESENTS} presents byte-identical held and redrawn; moving: present 0 "
               "byte-identical")

if(NOT PYTHON)
  message(STATUS "moving: no Python for the region check")
  return()
endif()
set(probe [=[
import sys
try:
    from PIL import Image, ImageChops, ImageFilter
except ImportError:
    print('NOPIL')
    sys.exit(0)
out, last, reach = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
B = 8  # the region's block, texels

def maxch(im):
    r, g, b = im.convert('RGB').split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b)

def binary(im):
    return im.point(lambda v: 255 if v else 0)

for k in range(1, last + 1):
    a = Image.open('%s/moving/held/SCENE-p%d.png' % (out, k))
    b = Image.open('%s/moving/redraw/SCENE-p%d.png' % (out, k))
    diff = binary(maxch(ImageChops.difference(a.convert('RGB'), b.convert('RGB'))))
    w, h = diff.size
    mask = Image.new('L', (w, h), 0)
    for mode in ('held', 'redraw'):
        m = maxch(Image.open('%s/moving/%s/WORK2-p%d.png' % (out, mode, k)))
        if m.size != (w, h):
            print('SIZE WORK2 %s vs SCENE %s' % (m.size, (w, h)))
            sys.exit(1)
        hist = m.histogram()
        fill = hist.index(max(hist))  # the fill's level, the most common
        mask = ImageChops.lighter(mask, m.point(lambda v, f=fill: 255 if v != f else 0))
    gw, gh = (w + B - 1) // B, (h + B - 1) // B
    grid = mask.resize((gw, gh), Image.BOX).point(lambda v: 255 if v else 0)
    size = 2 * ((reach + B - 1) // B) + 1
    grid = grid.filter(ImageFilter.MaxFilter(size))
    region = grid.resize((gw * B, gh * B), Image.NEAREST).crop((0, 0, w, h))
    outside = ImageChops.multiply(diff, ImageChops.invert(region))
    n = diff.histogram()[255]
    no = outside.histogram()[255]
    nm = mask.histogram()[255]
    print('PRESENT %d DIFF %d OUTSIDE %d MASK %d OF %d' % (k, n, no, nm, w * h))
]=])
execute_process(COMMAND "${PYTHON}" -I -c "${probe}" "${OUT}" "${last}" "${REACH}"
                RESULT_VARIABLE prc OUTPUT_VARIABLE pout ERROR_VARIABLE perr)
if(pout MATCHES "NOPIL")
  message(STATUS "moving: no Pillow for the region check")
  return()
endif()
if(NOT prc EQUAL 0 OR NOT pout MATCHES "PRESENT 1 DIFF")
  message(FATAL_ERROR "the region probe failed (${prc}):\n${pout}\n${perr}")
endif()
message(STATUS "moving:\n${pout}")
string(REGEX MATCHALL "OUTSIDE [0-9]+" outs "${pout}")
foreach(o ${outs})
  if(NOT o STREQUAL "OUTSIDE 0")
    message(FATAL_ERROR "moving: pixels differ farther than ${REACH} texels from the glow's "
                        "mask:\n${pout}")
  endif()
endforeach()
