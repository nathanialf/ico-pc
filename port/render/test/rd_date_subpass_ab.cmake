# rd_date_subpass_ab: DATE through the stencil bit on the Vulkan VkRenderPass
# path's subpass read (port/rhi/vk/vk_renderpass.c; RhiLimits.localReadSubpass)
# against the R8 snapshot, byte for byte (issue 179: the Mali-G52's Vulkan 1.1
# driver has no dynamic rendering, so before v0.11.3 every DATE draw ended the
# scene's pass for its snapshot).  For every dump the list names (read as
# rd_store_plan_ab.cmake reads its lists), rd_replay_tool's --pass-dump with
# --stats twice under ICO_VK_FAKE_API=1.1 (VkRenderPass objects, the fence ring)
# and ICO_VK_FAKE_LIMITS=moltenvk (a tile-based profile: DATE's stencil path is
# the profile's own): ICO_RD_DATE_PATH=stencil and =snapshot, the fog in place
# (ICO_RD_FOG_PATH=inplace: the subpass read has no fog read); the stencil run
# with ICO_VK_SUBPASS_READ=1 (the read is an experiment, off by default), the
# snapshot run with =0 (as shipped: no input usage, no GENERAL, no
# self-dependency).  Every named target's PNG of the stencil run must equal
# the snapshot run's; the stencil run says "the subpass read" (the device
# line), the snapshot run "the subpass read off"; the stencil run draws no DATE snapshot
# where the snapshot run drew some, with fewer passes by at least that many
# (each snapshot ends the scene's pass and begins it again); over the list at
# least one dump draws DATE through the bit.  No validation error in either
# run.  Each dump's counts are printed.  A missing list or dump is named and
# skipped; no dump read, or no device: SKIP (77).
# -DTOOL=<rd_replay_tool> -DLIST=<dump list> -DOUT=<a folder the test may write>
set(dumps "")
if(NOT LIST STREQUAL "" AND EXISTS "${LIST}")
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
      file(GLOB matched "${l}")
      list(SORT matched)
      if(matched)
        list(APPEND dumps ${matched})
      else()
        message(STATUS "${l}: no dump matches, skipped")
      endif()
    elseif(EXISTS "${l}")
      list(APPEND dumps "${l}")
    else()
      message(STATUS "${l}: no such dump, skipped")
    endif()
  endforeach()
else()
  message(STATUS "${LIST}: no dump list")
endif()
list(REMOVE_DUPLICATES dumps)
list(LENGTH dumps ndumps)
if(ndumps EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} is here")
  return()
endif()

set(runs "stencil" "snapshot")
set(dateRe "stats: DATE snapshots ([0-9]+), DATE stencil draws ([0-9]+) \\(([0-9]+) setups\\)")
set(passRe "stats: [0-9]+ draws, ([0-9]+) passes,")
set(compared 0)
set(failures 0)
set(viaBit 0)
set(saved 0)
set(index 0)
foreach(dump IN LISTS dumps)
  math(EXPR index "${index} + 1")
  set(ok TRUE)
  foreach(run IN LISTS runs)
    set(dir_${run} "${OUT}/${index}-${run}")
    file(REMOVE_RECURSE "${dir_${run}}")
    file(MAKE_DIRECTORY "${dir_${run}}")
    # the stencil run with the subpass read on (an experiment, off by
    # default: [dev] subpass_read = on), the snapshot run with it off
    if(run STREQUAL "stencil")
      set(sub 1)
      set(subRe "render passes: the subpass read:")
    else()
      set(sub 0)
      set(subRe "render passes: the subpass read off")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ICO_VK_FAKE_API=1.1
                            ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_FOG_PATH=inplace
                            ICO_VK_SUBPASS_READ=${sub}
                            ICO_RD_DATE_PATH=${run} "${TOOL}" "${dump}" - --pass-dump
                            "${dir_${run}}" --stats
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    if(NOT rc EQUAL 0)
      message(SEND_ERROR "${dump} ${run}: exit ${rc}:\n${err}")
      set(ok FALSE)
      break()
    endif()
    string(REPLACE ";" "," flat "${out}\n${err}")
    if(flat MATCHES "validation error")
      message(SEND_ERROR "${dump} ${run}: a validation error:\n${err}")
      set(ok FALSE)
      break()
    endif()
    if(NOT flat MATCHES "${subRe}")
      message(SEND_ERROR "${dump} ${run}: no \"${subRe}\" line:\n${err}")
      set(ok FALSE)
      break()
    endif()
    if(NOT flat MATCHES "${dateRe}")
      message(SEND_ERROR "${dump} ${run}: no DATE stats line:\n${out}")
      set(ok FALSE)
      break()
    endif()
    set(snaps_${run} "${CMAKE_MATCH_1}")
    set(bits_${run} "${CMAKE_MATCH_2}")
    if(NOT flat MATCHES "${passRe}")
      message(SEND_ERROR "${dump} ${run}: no passes stats line:\n${out}")
      set(ok FALSE)
      break()
    endif()
    set(passes_${run} "${CMAKE_MATCH_1}")
  endforeach()
  if(NOT ok)
    math(EXPR failures "${failures} + 1")
    continue()
  endif()
  message(STATUS "${dump}: stencil ${passes_stencil} passes, ${snaps_stencil} DATE snapshots, "
                 "${bits_stencil} stencil draws; snapshot ${passes_snapshot} passes, "
                 "${snaps_snapshot} DATE snapshots")
  if(NOT bits_snapshot EQUAL 0)
    message(SEND_ERROR "${dump}: the snapshot run drew ${bits_snapshot} DATE draws by the bit")
    math(EXPR failures "${failures} + 1")
  endif()
  if(snaps_snapshot GREATER 0)
    math(EXPR fewer "${passes_snapshot} - ${passes_stencil}")
    if(NOT snaps_stencil EQUAL 0 OR bits_stencil EQUAL 0 OR fewer LESS snaps_snapshot)
      message(SEND_ERROR "${dump}: the stencil run drew ${snaps_stencil} snapshots and "
                         "${bits_stencil} stencil draws, ${fewer} passes fewer than the "
                         "snapshot run's ${snaps_snapshot} snapshots")
      math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR viaBit "${viaBit} + 1")
    math(EXPR saved "${saved} + ${fewer}")
  endif()
  file(GLOB pngs RELATIVE "${dir_snapshot}" "${dir_snapshot}/*.png")
  list(LENGTH pngs npng)
  if(npng EQUAL 0)
    message(SEND_ERROR "${dump}: no target written")
    math(EXPR failures "${failures} + 1")
  endif()
  foreach(png IN LISTS pngs)
    if(NOT EXISTS "${dir_stencil}/${png}")
      message(SEND_ERROR "${dump}: ${png} written by the snapshot run only")
      math(EXPR failures "${failures} + 1")
      continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir_snapshot}/${png}"
                            "${dir_stencil}/${png}" RESULT_VARIABLE same)
    math(EXPR compared "${compared} + 1")
    if(NOT same EQUAL 0)
      message(SEND_ERROR "${dump}: ${png} differs between the snapshot and the stencil bit")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
if(compared EQUAL 0 AND failures EQUAL 0)
  message(STATUS "SKIP: no dump of ${LIST} could be compared")
  return()
endif()
if(viaBit EQUAL 0)
  message(SEND_ERROR "no dump drew DATE (no snapshot in any snapshot run): nothing was compared "
                     "through the bit")
endif()
message(STATUS "rd_date_subpass_ab: ${compared} targets byte for byte; ${viaBit} dumps drew DATE "
               "through the subpass read, ${saved} passes fewer in all")
