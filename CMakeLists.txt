cmake_minimum_required(VERSION 3.21)
project(bowl_fill_simulator VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# lab37's env.sh puts its vendored Qt 6.3 on CMAKE_PREFIX_PATH. That build ships no
# Wayland platform plugin, so on a Wayland session the app falls back to XWayland and
# the compositor bitmap-upscales it -- which looks like blurry text on a HiDPI screen.
# Prefer the system Qt unless the caller pins one with -DCMAKE_PREFIX_PATH.
option(BOWLFILL_PREFER_SYSTEM_QT "Look for Qt6 in system prefixes first" ON)
if(BOWLFILL_PREFER_SYSTEM_QT AND NOT Qt6_DIR AND NOT DEFINED CACHE{CMAKE_PREFIX_PATH})
  list(PREPEND CMAKE_PREFIX_PATH /usr /usr/local)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

get_target_property(_qt_core_lib Qt6::Core LOCATION)
get_filename_component(_qt_prefix "${_qt_core_lib}" DIRECTORY)
message(STATUS "bowl-fill-simulator: Qt ${Qt6_VERSION} from ${_qt_prefix}")
file(GLOB _wayland_plugin "${_qt_prefix}/qt6/plugins/platforms/libqwayland*.so"
                          "${_qt_prefix}/../plugins/platforms/libqwayland*.so")
if(NOT _wayland_plugin AND DEFINED ENV{WAYLAND_DISPLAY})
  message(WARNING
    "This Qt has no Wayland platform plugin, so the app will run under XWayland and "
    "the compositor will upscale it -- text will look soft on a HiDPI display. "
    "Configure a fresh build directory with -DCMAKE_PREFIX_PATH=/usr to use the "
    "system Qt instead.")
endif()

# Simulation core. Depends on Qt6::Core only for QString/QJson, so it stays
# linkable from a headless tool as well as the GUI.
add_library(bowlfill_core STATIC
  src/core/menu_loader.cc
  src/core/curves.cc
  src/core/simulator.cc
)
target_include_directories(bowlfill_core PUBLIC src)
target_link_libraries(bowlfill_core PUBLIC Qt6::Core)
target_compile_options(bowlfill_core PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(bowl-fill-simulator
  src/main.cc
  src/ui/main_window.cc
  src/ui/ramp_chart.cc
  src/ui/curve_chart.cc
  src/ui/bowl_diagram.cc
  src/ui/theme.cc
  src/ui/units.cc
)
target_link_libraries(bowl-fill-simulator PRIVATE bowlfill_core Qt6::Widgets)
target_compile_options(bowl-fill-simulator PRIVATE -Wall -Wextra)

# Where menus/, data/ and photos/ live. Overridable so an installed copy can point
# elsewhere; the app also accepts --assets on the command line.
target_compile_definitions(bowl-fill-simulator PRIVATE
  BOWLFILL_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(bowlfill-verify tests/verify.cc)
target_link_libraries(bowlfill-verify PRIVATE bowlfill_core)
target_compile_definitions(bowlfill-verify PRIVATE
  BOWLFILL_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

enable_testing()
add_test(NAME verify COMMAND bowlfill-verify)
