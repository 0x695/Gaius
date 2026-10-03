# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
# The version lives in one file, VERSION.txt (x.y.z), which this file, the Windows resources, the Android build, the web page
# and the release workflow all read. A release is the commit tagged v<VERSION> (docs/RELEASING.md).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" GAIUS_VERSION_FILE LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt")
project(gaius VERSION ${GAIUS_VERSION_FILE} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(MSVC)
  add_compile_options(/W4)
  # getenv is fine here: only test and tool configuration reads it.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

include_directories(${PROJECT_SOURCE_DIR})

# A static vcpkg triplet (x64-windows-static: the Windows release package, packaging/windows) means the static C++
# runtime too, so the program is one exe that needs nothing installed.
if(MSVC AND VCPKG_TARGET_TRIPLET MATCHES "-static$" AND NOT CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- version -------------------------------------------------------------------------------------------------------
# GAIUS_VERSION_STRING is "0.9.0" on the commit tagged v0.9.0 and "0.9.0-dev+<commit>" anywhere else (or what
# -DGAIUS_VERSION_STRING=... says). The generated files go under the build folder.
if(NOT GAIUS_VERSION_STRING)
  set(GAIUS_VERSION_STRING "${PROJECT_VERSION}")
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match --match "v${PROJECT_VERSION}"
                    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} RESULT_VARIABLE GAIUS_ON_TAG OUTPUT_QUIET ERROR_QUIET)
    if(NOT GAIUS_ON_TAG EQUAL 0)
      execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GAIUS_COMMIT
                      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(GAIUS_COMMIT)
        set(GAIUS_VERSION_STRING "${PROJECT_VERSION}-dev+${GAIUS_COMMIT}")
      endif()
    endif()
  endif()
endif()
set(GAIUS_VERSION_RC "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
configure_file(${PROJECT_SOURCE_DIR}/apps/viewer/gaius_version.hpp.in ${PROJECT_BINARY_DIR}/generated/gaius_version.hpp @ONLY)
add_library(gaius_version INTERFACE)
target_include_directories(gaius_version INTERFACE ${PROJECT_BINARY_DIR}/generated)
message(STATUS "Gaius ${GAIUS_VERSION_STRING}")

# GAIUS_GAME_LIBRARY: build the libraries and the game as a shared library
# named "main", which SDL's Android activity loads (android/app/jni/src). No
# tools, no tests.
option(GAIUS_GAME_LIBRARY "Build the game as libmain (Android) instead of executables" OFF)

# GAIUS_WEB: the browser build (Emscripten). Its own SDL2 comes from Emscripten's port; the game is one page, web/.
if(EMSCRIPTEN)
  set(GAIUS_WEB ON)
  # The code throws and catches FormatError and friends; Emscripten leaves exceptions off unless asked.
  add_compile_options(-fexceptions -sUSE_SDL=2)
  add_link_options(-fexceptions -sUSE_SDL=2)
endif()

# --- SDL2 (Phase 1+: platform layer and viewer app) -----------------------
if(GAIUS_WEB)
  set(SDL2_LIBRARIES "")
  set(SDL2_INCLUDE_DIRS "")
elseif(TARGET SDL2::SDL2)
  # Built alongside (Android's jni CMakeLists adds SDL's source first).
  set(SDL2_LIBRARIES SDL2::SDL2)
else()
  find_package(SDL2 QUIET)
  if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()
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
  formats/vas/vas.cpp
  formats/voc/voc.cpp
  formats/xmi/xmi.cpp
  formats/gtl/gtl.cpp
  formats/screen_data/screen_data.cpp
)
target_include_directories(gaius_formats PUBLIC ${PROJECT_SOURCE_DIR})

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
  systems/month.cpp
  systems/actors.cpp
  systems/economy.cpp
  systems/military.cpp
  systems/battle.cpp
  systems/province.cpp
  systems/plebs.cpp
  systems/administration.cpp
  systems/campaign.cpp
  systems/forum.cpp
  systems/messages.cpp
  systems/sounds.cpp
)
target_link_libraries(gaius_systems PUBLIC gaius_model)

# --- ui library (Phase 5: scalable toolbar, masterplan 5a point 3) ---------
# Deliberately free of SDL and of apps/: pure geometry + pixel writes into
# an RGB24 buffer, so layout/hit-testing is unit-testable headlessly.
add_library(gaius_ui STATIC
  ui/metrics.cpp
  ui/font.cpp
  ui/game_font.cpp
  ui/battle_screen.cpp
  ui/maps_screen.cpp
  ui/name_entry.cpp
  ui/interface.cpp
  ui/buttons.cpp
  ui/options_screen.cpp
  ui/forum_screens.cpp
  ui/toolbar.cpp
  ui/panel.cpp
  ui/settings.cpp
  ui/strings.cpp
)
target_link_libraries(gaius_ui PUBLIC gaius_systems)

# --- render library: the city view drawn from the game's own sprites -------
# Pure, like gaius_ui: renders into an indexed image, no SDL.
add_library(gaius_render STATIC
  render/city_render.cpp
  render/province_render.cpp
  render/empire_map.cpp
)
target_link_libraries(gaius_render PUBLIC gaius_systems)

# --- vendored ymfm (BSD-3-Clause): the YM3812 the music plays on -----------
add_library(ymfm STATIC
  third_party/ymfm/ymfm_opl.cpp
  third_party/ymfm/ymfm_adpcm.cpp
  third_party/ymfm/ymfm_pcm.cpp
)
target_include_directories(ymfm PUBLIC third_party/ymfm)
if(MSVC)
  target_compile_options(ymfm PRIVATE /W0)
else()
  target_compile_options(ymfm PRIVATE -w)
endif()

# --- audio library (Phase 9): the game's sound driver and sound layer --------
# Pure, like gaius_ui: produces samples, no SDL.
add_library(gaius_audio STATIC
  audio/ail_xmidi.cpp
  audio/game_audio.cpp
)
target_link_libraries(gaius_audio PUBLIC gaius_formats ymfm)

# --- platform layer (Phase 1: window/input/paths) --------------------------
# The platform rules that need no SDL (where Gaius's folders go), so the tests
# can link them.
add_library(gaius_platform_rules STATIC platform/path_rules.cpp)
target_include_directories(gaius_platform_rules PUBLIC ${PROJECT_SOURCE_DIR})

add_library(gaius_platform STATIC
  platform/console.cpp
  platform/game_detect.cpp
  platform/window.cpp
  platform/input.cpp
  platform/paths.cpp
  platform/game_import.cpp
  platform/web.cpp
)
target_link_libraries(gaius_platform PUBLIC gaius_platform_rules)
target_include_directories(gaius_platform PUBLIC ${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})
target_link_libraries(gaius_platform PUBLIC ${SDL2_LIBRARIES})
if(WIN32)
  # The registry, the folder dialog and the title bar's theme (platform/game_detect.cpp, window.cpp).
  target_link_libraries(gaius_platform PUBLIC advapi32 ole32 shell32 uuid)
endif()
target_compile_options(gaius_platform PUBLIC ${SDL2_CFLAGS_OTHER})

set(GAIUS_GAME_LIBS gaius_formats gaius_model gaius_systems gaius_ui gaius_render gaius_audio gaius_platform
    stb_image_write)

if(GAIUS_GAME_LIBRARY)
  # --- the game for Android: libmain.so --------------------------------------
  add_library(main SHARED apps/viewer/main.cpp)
  target_link_libraries(main PRIVATE ${GAIUS_GAME_LIBS} gaius_version)
  return()
endif()

if(GAIUS_WEB)
  # --- the game in a browser: gaius.html + gaius.js + gaius.wasm + gaius.data ----------------------------
  # Asyncify lets the viewer's own blocking frame loop yield to the browser (SDL_Delay and SDL_RenderPresent do it),
  # so the same main() runs unchanged. No threads: the page needs no special headers and can be served anywhere,
  # GitHub Pages included. The player's game files are never built in; web/gaius-web.js imports them into
  # /persist/game (IndexedDB) at run time. See web/README.md.
  add_executable(gaius_web apps/viewer/main.cpp)
  set_target_properties(gaius_web PROPERTIES OUTPUT_NAME gaius SUFFIX ".html")
  target_link_libraries(gaius_web PRIVATE ${GAIUS_GAME_LIBS} gaius_version idbfs.js)
  configure_file(${PROJECT_SOURCE_DIR}/web/gaius-version.js.in ${PROJECT_BINARY_DIR}/gaius-version.js @ONLY)
  target_link_options(gaius_web PRIVATE
    -sASYNCIFY -sASYNCIFY_STACK_SIZE=1048576
    -sSTACK_SIZE=2097152 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864
    -sFORCE_FILESYSTEM=1 -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sMODULARIZE=0
    "-sEXPORTED_RUNTIME_METHODS=['callMain','FS','IDBFS','ccall']"
    "-sEXPORTED_FUNCTIONS=['_main']"
    --shell-file ${PROJECT_SOURCE_DIR}/web/shell.html
    --preload-file ${PROJECT_SOURCE_DIR}/lang@/lang)
  set_target_properties(gaius_web PROPERTIES LINK_DEPENDS ${PROJECT_SOURCE_DIR}/web/shell.html)
  # The page's own files go beside the build (a custom target, so editing them needs no relink); index.html is the
  # shell's output under the name a web server serves by default.
  add_custom_target(gaius_web_files ALL
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/web/gaius-web.js ${PROJECT_SOURCE_DIR}/web/gaius-web.css
            ${PROJECT_SOURCE_DIR}/docs/images/banner.svg ${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt
            $<TARGET_FILE_DIR:gaius_web>
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE_DIR:gaius_web>/gaius.html $<TARGET_FILE_DIR:gaius_web>/index.html
    DEPENDS gaius_web)
  return()
endif()

# --- viewer app (Phase 1 deliverable) --------------------------------------
add_executable(gaius_viewer apps/viewer/main.cpp)
target_link_libraries(gaius_viewer PRIVATE ${GAIUS_GAME_LIBS} gaius_version)
if(WIN32)
  # The icon and the version strings (apps/viewer/gaius.rc.in; the icon is drawn by packaging/icon/make_icon.py).
  enable_language(RC)
  configure_file(${PROJECT_SOURCE_DIR}/apps/viewer/gaius.rc.in ${PROJECT_BINARY_DIR}/generated/gaius.rc @ONLY)
  target_sources(gaius_viewer PRIVATE ${PROJECT_BINARY_DIR}/generated/gaius.rc)
  # A GUI program: no console window behind the game (platform/console.hpp borrows the terminal's when there is
  # one). The entry stays `main`, which SDL's SDL2main or the CRT provides either way.
  set_target_properties(gaius_viewer PROPERTIES WIN32_EXECUTABLE ON)
  if(MSVC)
    target_link_options(gaius_viewer PRIVATE /ENTRY:mainCRTStartup)
  else()
    target_link_options(gaius_viewer PRIVATE -mwindows)
  endif()
endif()
# The language files sit beside the executable (ui/strings.hpp).
add_custom_command(TARGET gaius_viewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/lang $<TARGET_FILE_DIR:gaius_viewer>/lang)

# --- CLI tools -----------------------------------------------------------
add_executable(dump_vpx tools/dump_vpx.cpp)
target_link_libraries(dump_vpx PRIVATE gaius_formats)
target_link_libraries(dump_vpx PRIVATE stb_image_write)

add_executable(dump_vas tools/dump_vas.cpp)
target_link_libraries(dump_vas PRIVATE gaius_formats stb_image_write)

add_executable(xmi2mid tools/xmi2mid.cpp)
target_link_libraries(xmi2mid PRIVATE gaius_formats)

add_executable(dump_pl8 tools/dump_pl8.cpp)
target_link_libraries(dump_pl8 PRIVATE gaius_formats)
target_link_libraries(dump_pl8 PRIVATE stb_image_write)

add_executable(empire_view tools/empire_view.cpp)
target_link_libraries(empire_view PRIVATE gaius_formats)
target_link_libraries(empire_view PRIVATE stb_image_write)

add_executable(save_inspect tools/save_inspect.cpp)
target_link_libraries(save_inspect PRIVATE gaius_formats gaius_model gaius_systems)

add_executable(save_diff tools/save_diff.cpp)
target_link_libraries(save_diff PRIVATE gaius_formats gaius_model gaius_systems)

add_executable(dump_voc tools/dump_voc.cpp)
target_link_libraries(dump_voc PRIVATE gaius_formats)

add_executable(bindiff_exe tools/bindiff_exe.cpp)
target_link_libraries(bindiff_exe PRIVATE gaius_formats)

add_executable(sim_check tools/sim_check.cpp)
target_link_libraries(sim_check PRIVATE gaius_formats gaius_model gaius_systems)

add_executable(month_check tools/month_check.cpp)
target_link_libraries(month_check PRIVATE gaius_formats gaius_model gaius_systems)

add_executable(playtest tools/playtest.cpp)
target_link_libraries(playtest PRIVATE gaius_formats gaius_model gaius_systems)

add_executable(render_city tools/render_city.cpp)
target_link_libraries(render_city PRIVATE gaius_render stb_image_write)

add_executable(render_audio tools/render_audio.cpp)
target_link_libraries(render_audio PRIVATE gaius_audio)

# --- tests -----------------------------------------------------------------
enable_testing()
add_executable(gaius_tests tests/test_formats.cpp)
target_link_libraries(gaius_tests PRIVATE gaius_formats gaius_model gaius_systems gaius_ui gaius_render gaius_audio gaius_platform_rules gaius_version stb_image)
add_test(NAME gaius_tests COMMAND gaius_tests)
