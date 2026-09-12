# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(gaius LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

include_directories(${CMAKE_SOURCE_DIR})

# --- SDL2 (Phase 1+: platform layer and viewer app) -----------------------
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 REQUIRED sdl2)
  endif()
endif()

# --- vendored stb_image_write (single-header PNG writer, public domain) ---
add_library(stb_image_write STATIC third_party/stb/stb_image_write_impl.cpp)
target_include_directories(stb_image_write PUBLIC third_party/stb)

# --- vendored stb_image (single-header image reader, for golden-image tests) ---
add_library(stb_image STATIC third_party/stb/stb_image_impl.cpp)
target_include_directories(stb_image PUBLIC third_party/stb)

# --- formats library (Layer 1: exact import) ---------------------------
add_library(gaius_formats STATIC
  formats/p32/p32.cpp
  formats/pal256/pal256.cpp
  formats/vpx/vpx.cpp
  formats/pl8/pl8.cpp
  formats/empire2/empire2.cpp
  formats/save/save.cpp
  formats/exepack/exepack.cpp
)
target_include_directories(gaius_formats PUBLIC ${CMAKE_SOURCE_DIR})

# --- CLI tools -----------------------------------------------------------
add_executable(dump_vpx tools/dump_vpx.cpp)
target_link_libraries(dump_vpx PRIVATE gaius_formats)
target_link_libraries(dump_vpx PRIVATE stb_image_write)

add_executable(dump_pl8 tools/dump_pl8.cpp)
target_link_libraries(dump_pl8 PRIVATE gaius_formats)
target_link_libraries(dump_pl8 PRIVATE stb_image_write)

add_executable(empire_view tools/empire_view.cpp)
target_link_libraries(empire_view PRIVATE gaius_formats)
target_link_libraries(empire_view PRIVATE stb_image_write)

add_executable(save_inspect tools/save_inspect.cpp)
target_link_libraries(save_inspect PRIVATE gaius_formats gaius_model)

add_executable(bindiff_exe tools/bindiff_exe.cpp)
target_link_libraries(bindiff_exe PRIVATE gaius_formats)

# --- model library (Phase 2: normalized data model, Layer 2) ---------------
add_library(gaius_model STATIC
  model/city_state.cpp
)
target_link_libraries(gaius_model PUBLIC gaius_formats)

# --- systems library (Phase 3: service propagation; Phase 4: housing) ------
add_library(gaius_systems STATIC
  systems/service.cpp
  systems/housing.cpp
  systems/construction.cpp
)
target_link_libraries(gaius_systems PUBLIC gaius_model)

# --- ui library (Phase 5: scalable toolbar, masterplan 5a point 3) ---------
# Deliberately free of SDL and of apps/: pure geometry + pixel writes into
# an RGB24 buffer, so layout/hit-testing is unit-testable headlessly.
add_library(gaius_ui STATIC
  ui/metrics.cpp
  ui/font.cpp
  ui/toolbar.cpp
)
target_link_libraries(gaius_ui PUBLIC gaius_systems)

# --- platform layer (Phase 1: window/input/paths) --------------------------
add_library(gaius_platform STATIC
  platform/window.cpp
  platform/input.cpp
  platform/paths.cpp
)
target_include_directories(gaius_platform PUBLIC ${CMAKE_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})
target_link_libraries(gaius_platform PUBLIC ${SDL2_LIBRARIES})
target_compile_options(gaius_platform PUBLIC ${SDL2_CFLAGS_OTHER})

# --- viewer app (Phase 1 deliverable) --------------------------------------
add_executable(gaius_viewer apps/viewer/main.cpp)
target_link_libraries(gaius_viewer PRIVATE gaius_formats gaius_model gaius_systems gaius_ui gaius_platform stb_image_write)

# --- tests -----------------------------------------------------------------
enable_testing()
add_executable(gaius_tests tests/test_formats.cpp)
target_link_libraries(gaius_tests PRIVATE gaius_formats gaius_model gaius_systems gaius_ui stb_image)
add_test(NAME gaius_tests COMMAND gaius_tests)
