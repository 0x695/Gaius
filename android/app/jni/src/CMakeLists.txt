# SPDX-License-Identifier: GPL-3.0-or-later
# The game for Android: the repository's own CMake project, built as the shared
# library "main" that SDL's activity loads (GAIUS_GAME_LIBRARY in the root
# CMakeLists.txt). SDL2 is already a target here (android/app/jni/CMakeLists.txt
# adds its source first).
cmake_minimum_required(VERSION 3.16)

project(gaius_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GAIUS_GAME_LIBRARY ON CACHE BOOL "" FORCE)

# android/app/jni/src -> the repository root
get_filename_component(GAIUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../.. ABSOLUTE)
add_subdirectory(${GAIUS_ROOT} gaius)
