# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.6)

project(gaius_android_hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# android/app/jni/src -> repo root (android/app/jni/src/../../../../)
set(GAIUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

add_library(main SHARED
    ${GAIUS_ROOT}/apps/android_hello/android_main.cpp
    ${GAIUS_ROOT}/platform/window.cpp
    ${GAIUS_ROOT}/platform/input.cpp
    ${GAIUS_ROOT}/platform/paths.cpp
)
target_include_directories(main PRIVATE ${GAIUS_ROOT})
target_link_libraries(main PUBLIC SDL2::SDL2 SDL2::SDL2main)
