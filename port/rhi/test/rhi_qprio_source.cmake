# rhi_vk_qprio_source: no code path asks a global queue priority.  v0.11.7
# chained VkDeviceQueueGlobalPriorityCreateInfo at high onto the game's queue
# on Qualcomm's own driver under Android; v0.11.8 took it off (issues 215 and
# 123).  This scans every C, C++ and Objective-C source and header under
# port/ (the game, the platform, the renderer and every backend) for the
# structure's type name and its sType, in their KHR and EXT spellings: none
# may hold them.  The read-only offer line (vk_device.c vkr_offer_line) reads
# VkQueueFamilyGlobalPriorityProperties, a different structure, and stays.
# The number of files scanned is printed and must include vk_device.c, so the
# test cannot pass on an empty scan.
# -DSRC=<the repository's root>
cmake_policy(SET CMP0054 NEW) # a quoted if() argument is a string, not a variable
if(NOT IS_DIRECTORY "${SRC}/port/rhi/vk")
  message(FATAL_ERROR "rhi_qprio_source: ${SRC}/port/rhi/vk is not a directory")
endif()
file(GLOB_RECURSE files LIST_DIRECTORIES false "${SRC}/port/*.c" "${SRC}/port/*.h"
     "${SRC}/port/*.cpp" "${SRC}/port/*.hpp" "${SRC}/port/*.m" "${SRC}/port/*.mm")
list(LENGTH files n)
list(FIND files "${SRC}/port/rhi/vk/vk_device.c" vkDevice)
if(vkDevice LESS 0)
  message(FATAL_ERROR "rhi_qprio_source: ${n} files scanned, port/rhi/vk/vk_device.c not among them")
endif()
set(found "")
foreach(f IN LISTS files)
  file(STRINGS "${f}" hits REGEX "DeviceQueueGlobalPriorityCreateInfo|DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO")
  if(hits)
    file(RELATIVE_PATH rel "${SRC}" "${f}")
    string(APPEND found "\n  ${rel}: ${hits}")
  endif()
endforeach()
if(NOT found STREQUAL "")
  message(FATAL_ERROR "rhi_qprio_source: a global queue priority is asked:${found}")
endif()
message(STATUS "rhi_qprio_source: ${n} files under port/ scanned, none asks a global queue priority")
