# SPDX-FileCopyrightText: 2026 Suleyman Poyraz
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)
project(abitur VERSION 0.1.0 LANGUAGES C CXX)
configure_file(cmake/version.hpp.in generated/abitur/version.hpp @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(ABITUR_BUILD_GUI "Build Dear ImGui desktop application" ON)
option(ABITUR_BUILD_ANALYZER "Build the LLVM analyzer and application" ON)
include(CTest)

if(ABITUR_BUILD_ANALYZER)
  find_package(LLVM REQUIRED CONFIG)
  message(STATUS "LLVM ${LLVM_PACKAGE_VERSION}")
  add_library(abitur_core src/analyzer.cpp src/report.cpp)
  target_include_directories(abitur_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_include_directories(abitur_core SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
  target_compile_options(abitur_core PRIVATE ${LLVM_DEFINITIONS_LIST})
  if(MSVC)
    target_compile_options(abitur_core PRIVATE /W4)
  else()
    target_compile_options(abitur_core PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  llvm_map_components_to_libnames(ABITUR_LLVM_LIBS Object DebugInfoDWARF Support BinaryFormat)
  target_link_libraries(abitur_core PUBLIC ${ABITUR_LLVM_LIBS})
  add_executable(abitur src/cli.cpp)
  target_link_libraries(abitur PRIVATE abitur_core)
endif()

if(ABITUR_BUILD_GUI)
  include(FetchContent)
  find_package(glfw3 3.3 QUIET)
  if(NOT TARGET glfw)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    if(UNIX AND NOT APPLE)
      set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
      set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(glfw
      URL https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/3.4
      URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01)
    FetchContent_MakeAvailable(glfw)
  endif()
  FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.91.9b
    URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb)
  FetchContent_MakeAvailable(imgui)
  find_package(OpenGL REQUIRED)
  add_library(abitur_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(abitur_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(abitur_imgui PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})
  file(READ "${CMAKE_SOURCE_DIR}/assets/fonts/Cousine-Regular.ttf" ABITUR_FONT_HEX HEX)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/fonts/Cousine-Regular.ttf")
  string(REGEX REPLACE "(..)" "0x\\1," ABITUR_FONT_BYTES "${ABITUR_FONT_HEX}")
  configure_file(cmake/embedded_font.hpp.in generated/embedded_font.hpp @ONLY)
  add_library(abitur_desktop src/desktop/window.cpp)
  target_include_directories(abitur_desktop PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(abitur_desktop PUBLIC abitur_imgui)
  find_package(Threads REQUIRED)
  if(ABITUR_BUILD_ANALYZER)
    add_executable(abitur-gui src/gui.cpp)
    if(APPLE)
      set_target_properties(abitur-gui PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.abitur.workbench"
        MACOSX_BUNDLE_BUNDLE_NAME "Abitur")
    elseif(WIN32)
      set_target_properties(abitur-gui PROPERTIES WIN32_EXECUTABLE TRUE)
      if(MSVC)
        target_link_options(abitur-gui PRIVATE /ENTRY:mainCRTStartup)
      else()
        target_link_options(abitur-gui PRIVATE -Wl,-e,mainCRTStartup)
      endif()
    endif()
    target_link_libraries(abitur-gui PRIVATE abitur_core abitur_desktop Threads::Threads)
  endif()
  if(BUILD_TESTING)
    add_executable(abitur-window-smoke tests/window_smoke.cpp)
    target_link_libraries(abitur-window-smoke PRIVATE abitur_desktop)
  endif()
  foreach(desktop_target IN ITEMS abitur-gui abitur-window-smoke)
    if(TARGET ${desktop_target})
      add_custom_command(TARGET ${desktop_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${desktop_target}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE.txt" "$<TARGET_FILE_DIR:${desktop_target}>/licenses/Cousine-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/fonts/README.md" "$<TARGET_FILE_DIR:${desktop_target}>/licenses/Cousine-NOTICE.txt")
    endif()
  endforeach()
endif()

if(BUILD_TESTING)
  add_executable(abitur-window-geometry tests/window_geometry.cpp)
  target_include_directories(abitur-window-geometry PRIVATE include)
  add_test(NAME window-geometry COMMAND abitur-window-geometry)
  if(ABITUR_BUILD_ANALYZER AND UNIX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/integration.py
      --cli $<TARGET_FILE:abitur> --cxx ${CMAKE_CXX_COMPILER})
    add_test(NAME cli-metadata COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/cli_metadata.py
      $<TARGET_FILE:abitur> ${PROJECT_VERSION})
  endif()
endif()
