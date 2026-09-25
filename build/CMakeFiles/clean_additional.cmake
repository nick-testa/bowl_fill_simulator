# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/bowl-fill-simulator_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/bowl-fill-simulator_autogen.dir/ParseCache.txt"
  "CMakeFiles/bowlfill-verify_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/bowlfill-verify_autogen.dir/ParseCache.txt"
  "CMakeFiles/bowlfill_core_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/bowlfill_core_autogen.dir/ParseCache.txt"
  "bowl-fill-simulator_autogen"
  "bowlfill-verify_autogen"
  "bowlfill_core_autogen"
  )
endif()
